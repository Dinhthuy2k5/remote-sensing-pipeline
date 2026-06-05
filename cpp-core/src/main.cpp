#include "common/logger.hpp"
#include "common/types.hpp"
#include "pipeline/tiling_engine.hpp"
#include "pipeline/thread_pool.hpp"
#include "pipeline/coordinate_mapper.hpp"
#include "pipeline/state_machine.hpp"
#include "database/postgis_client.hpp"
#include "inference/mock_ai.hpp"
#include "api/http_gateway.hpp"
#include <iostream>
#include <atomic>
#include <mutex>
#include <map>
#include <thread>
#include <chrono>
#include <cstdlib>
#include <ctime>

// ─── Session context — lưu state từng session ─────────────────
struct SessionContext
{
    rs::PipelineConfig config;
    std::string filepath;
    rs::SessionInfo info;
    std::unique_ptr<rs::ThreadPool> pool; // còn trỏ chứa ThreadPool của phiên
    std::mutex mutex;                     // để bảo vệ dữ liệu nội bộ
};

// ─── Session Manager ──────────────────────────────────────────
class SessionManager
{
public:
    int64_t createSession(const std::string &filepath, int64_t id)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto ctx = std::make_shared<SessionContext>();
        ctx->filepath = filepath;
        ctx->info.id = id;
        ctx->info.filename = filepath;
        ctx->info.status = rs::SessionStatus::IDLE;
        sessions_[id] = ctx;
        return id;
    }

    bool setConfig(int64_t id, const rs::PipelineConfig &cfg)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = sessions_.find(id);
        if (it == sessions_.end())
            return false;
        it->second->config = cfg;
        return true;
    }

    std::shared_ptr<SessionContext> get(int64_t id)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = sessions_.find(id);
        return it != sessions_.end() ? it->second : nullptr;
    }

    rs::SessionInfo getInfo(int64_t id)
    {
        auto ctx = get(id);
        if (!ctx)
            return {};
        std::lock_guard<std::mutex> lock(ctx->mutex);
        return ctx->info;
    }

    bool cancel(int64_t id)
    {
        auto ctx = get(id);
        if (!ctx)
            return false;
        std::lock_guard<std::mutex> lock(ctx->mutex);
        if (ctx->pool)
            ctx->pool->waitAll();
        ctx->info.status = rs::SessionStatus::ERROR;
        return true;
    }

private:
    std::mutex mutex_;
    std::map<int64_t, std::shared_ptr<SessionContext>> sessions_;
};

// ─── Run pipeline async ───────────────────────────────────────
void runPipelineAsync(
    std::shared_ptr<SessionContext> ctx,
    rs::PostGISClient &db)
{
    auto &cfg = ctx->config;

    rs::TilingEngine engine(cfg.tile_size, cfg.overlap);
    std::string err;
    if (!rs::TilingEngine::validateFile(ctx->filepath, err))
    {
        LOG_ERROR("Pipeline", err);
        std::lock_guard<std::mutex> lock(ctx->mutex);
        ctx->info.status = rs::SessionStatus::ERROR;
        return;
    }
    if (!engine.open(ctx->filepath))
    {
        std::lock_guard<std::mutex> lock(ctx->mutex);
        ctx->info.status = rs::SessionStatus::ERROR;
        return;
    }

    // Dùng ID đã có từ lúc upload — KHÔNG tạo session mới
    int64_t session_id = ctx->info.id;

    {
        std::lock_guard<std::mutex> lock(ctx->mutex);
        ctx->info.tile_total = engine.totalTiles();
        ctx->info.status = rs::SessionStatus::TILING;
    }

    // Update tile_total vào DB
    if (db.isConnected())
        db.updateSessionProgress(session_id, 0);

    rs::CoordinateMapper mapper(engine.metadata());
    ctx->pool = std::make_unique<rs::ThreadPool>(cfg.max_workers);

    std::atomic<int> tiles_done{0};
    std::mutex results_mutex;
    std::vector<rs::GeoDetection> all_geo_dets;

    {
        std::lock_guard<std::mutex> lock(ctx->mutex);
        ctx->info.status = rs::SessionStatus::PROCESSING;
    }

    ctx->pool->start([&](rs::TileData &tile)
                     {
        rs::MockAI ai(3, 42);
        auto dets     = ai.infer(tile);
        auto geo_dets = mapper.mapDetections(dets, tile);

        {
            std::lock_guard<std::mutex> lock(results_mutex);
            for (auto& g : geo_dets) {
                g.session_id = session_id;
                all_geo_dets.push_back(g);
            }
        }

        int done = ++tiles_done;
        {
            std::lock_guard<std::mutex> lk(ctx->mutex);
            ctx->info.tile_done = done;
        }
        if (db.isConnected() && done % 20 == 0)
            db.updateSessionProgress(session_id, done); });

    engine.iterateTiles(session_id, [&](rs::TileData tile)
                        { ctx->pool->submit(std::move(tile)); });
    ctx->pool->waitAll();

    {
        std::lock_guard<std::mutex> lock(ctx->mutex);
        ctx->info.status = rs::SessionStatus::SAVING;
    }

    if (db.isConnected())
    {
        db.insertDetections(all_geo_dets);
        db.updateSessionStatus(session_id, rs::SessionStatus::DONE);
    }

    {
        std::lock_guard<std::mutex> lock(ctx->mutex);
        ctx->info.status = rs::SessionStatus::DONE;
        ctx->info.tile_done = ctx->info.tile_total;
    }

    LOG_INFO("Pipeline", "Session " + std::to_string(session_id) + " DONE. Detections: " + std::to_string(all_geo_dets.size()));
    engine.close();
}

// ─── main ─────────────────────────────────────────────────────
int main()
{
    LOG_INFO("main", "Remote Sensing Pipeline v0.1.0");

    // PostGIS
    std::string conn_str =
        "host=" + std::string(std::getenv("POSTGRES_HOST") ? std::getenv("POSTGRES_HOST") : "localhost") +
        " port=" + std::string(std::getenv("POSTGRES_PORT") ? std::getenv("POSTGRES_PORT") : "5432") +
        " dbname=" + std::string(std::getenv("POSTGRES_DB") ? std::getenv("POSTGRES_DB") : "remote_sensing") +
        " user=" + std::string(std::getenv("POSTGRES_USER") ? std::getenv("POSTGRES_USER") : "rsuser") +
        " password=" + std::string(std::getenv("POSTGRES_PASSWORD") ? std::getenv("POSTGRES_PASSWORD") : "rspassword");

    rs::PostGISClient db(conn_str);
    int retry = 0;
    while (!db.isConnected() && retry < 10)
    {
        retry++;
        LOG_WARN("main", "Retrying PostGIS " + std::to_string(retry) + "/10...");
        std::this_thread::sleep_for(std::chrono::seconds(2));
        db = rs::PostGISClient(conn_str);
    }
    if (db.isConnected())
        LOG_INFO("main", "PostGIS connected.");
    else
        LOG_WARN("main", "Running without PostGIS.");

    // Session Manager
    SessionManager sessions;

    // HTTP Gateway
    int port = 8080;
    if (const char *p = std::getenv("HTTP_PORT"))
        port = std::stoi(p);
    rs::HttpGateway gateway(port);

    // ── Callbacks ─────────────────────────────────────────────

    // ── Upload Init: DB tạo session, trả ID chuẩn ─────────────────
    gateway.onUploadInit([&]() -> int64_t
                         {
    // DB cấp ID chính thức (BIGSERIAL: 1, 2, 3...)
    int64_t sid = db.isConnected()
        ? db.createSession("pending", 0)
        : (int64_t)std::time(nullptr);

    // SessionManager giữ slot với ID đó
    sessions.createSession("pending", sid);

    LOG_INFO("main", "Session pre-allocated: " + std::to_string(sid));
    return sid; });

    // ── Upload Complete: chốt filepath, session sẵn sàng ──────────
    gateway.onUploadComplete([&](int64_t sid,
                                 const std::string &filepath,
                                 const std::string &filename,
                                 size_t file_size)
                             {
    // Cập nhật filepath thực tế vào SessionContext
    auto ctx = sessions.get(sid);
    if (ctx) {
        std::lock_guard<std::mutex> lock(ctx->mutex);
        ctx->filepath      = filepath;
        ctx->info.filename = filepath;
    }

    LOG_INFO("main", "Session " + std::to_string(sid)
        + " file ready: " + filepath
        + " (" + std::to_string(file_size) + " bytes)"); });

    gateway.onConfig([&](int64_t sid,
                         const rs::PipelineConfig &cfg) -> bool
                     { return sessions.setConfig(sid, cfg); });

    gateway.onStart([&](int64_t sid) -> bool
                    {
        auto ctx = sessions.get(sid);
        if (!ctx) return false;

        {
            std::lock_guard<std::mutex> lock(ctx->mutex);
            if (ctx->info.status != rs::SessionStatus::IDLE) {
                LOG_WARN("main", "Session " + std::to_string(sid)
                    + " already started.");
                return false;
            }
            ctx->info.status = rs::SessionStatus::LOADING;
        }

        // Chạy pipeline trong thread riêng — không block HTTP
        std::thread([ctx, &db]() {
            runPipelineAsync(ctx, db);
        }).detach();

        return true; });

    gateway.onCancel([&](int64_t sid) -> bool
                     {
        auto ctx = sessions.get(sid);
        if (!ctx) return false;
        // Close queue → workers tự dừng ở tile tiếp theo
        if (ctx->pool) ctx->pool->waitAll();
        std::lock_guard<std::mutex> lock(ctx->mutex);
        ctx->info.status = rs::SessionStatus::ERROR;
        if (db.isConnected())
            db.updateSessionStatus(sid, rs::SessionStatus::ERROR);
        return true; });

    gateway.onStatus([&](int64_t sid) -> rs::SessionInfo
                     { return sessions.getInfo(sid); });

    gateway.onResults([&](int64_t sid) -> std::string
                      { return db.isConnected()
                                   ? db.queryDetectionsGeoJSON(sid)
                                   : "{}"; });

    // ── Log endpoints ─────────────────────────────────────────
    LOG_INFO("main", "API endpoints:");
    LOG_INFO("main", "  GET  /health");
    LOG_INFO("main", "  POST /upload");
    LOG_INFO("main", "  POST /sessions/{id}/config");
    LOG_INFO("main", "  POST /sessions/{id}/start");
    LOG_INFO("main", "  POST /sessions/{id}/cancel");
    LOG_INFO("main", "  GET  /sessions/{id}/status");
    LOG_INFO("main", "  GET  /sessions/{id}/results");

    gateway.start();
    return 0;
}