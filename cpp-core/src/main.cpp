#include "common/logger.hpp"
#include "common/types.hpp"
#include "pipeline/tiling_engine.hpp"
#include "pipeline/thread_pool.hpp"
#include "pipeline/coordinate_mapper.hpp"
#include "pipeline/state_machine.hpp"
#include "database/postgis_client.hpp"
#include "inference/mock_ai.hpp"
#include "api/http_gateway.hpp"
#include "monitoring/udp_broadcaster.hpp"
#include "stitching/stitcher.hpp"
#include "inference/onnx_ai.hpp"
#include "inference/onnx_obb_ai.hpp"
#include "inference/onnx_segformer_ai.hpp"
#include <onnxruntime_cxx_api.h>
#include <iostream>
#include <atomic>
#include <mutex>
#include <map>
#include <thread>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>

// ─── Session context — lưu state từng session ─────────────────
struct SessionContext
{
    rs::PipelineConfig config;
    std::string filepath;
    rs::SessionInfo info;
    std::atomic<bool> cancel_requested{false};
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
        std::shared_ptr<SessionContext> ctx;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = sessions_.find(id);
            if (it == sessions_.end())
                return false;
            ctx = it->second;
        }
        std::lock_guard<std::mutex> lock(ctx->mutex);
        ctx->config = cfg;
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
        ctx->cancel_requested.store(true);
        if (ctx->pool)
            ctx->pool->requestStop();
        ctx->info.status = rs::SessionStatus::ERROR;
        return true;
    }

private:
    std::mutex mutex_;
    std::map<int64_t, std::shared_ptr<SessionContext>> sessions_;
};

// ─── Run pipeline async ───────────────────────────────────────
bool dbUpdateProgress(rs::PostGISClient &db, std::mutex &db_mutex, int64_t session_id, int tile_done)
{
    std::lock_guard<std::mutex> lock(db_mutex);
    return db.updateSessionProgress(session_id, tile_done);
}

bool dbUpdateStatus(rs::PostGISClient &db, std::mutex &db_mutex, int64_t session_id, rs::SessionStatus status)
{
    std::lock_guard<std::mutex> lock(db_mutex);
    return db.updateSessionStatus(session_id, status);
}

bool dbInsertDetections(rs::PostGISClient &db, std::mutex &db_mutex, const std::vector<rs::GeoDetection> &detections)
{
    std::lock_guard<std::mutex> lock(db_mutex);
    return db.insertDetections(detections);
}

std::string dbQueryDetections(rs::PostGISClient &db, std::mutex &db_mutex, int64_t session_id)
{
    std::lock_guard<std::mutex> lock(db_mutex);
    return db.queryDetectionsGeoJSON(session_id);
}

void runPipelineAsync(
    std::shared_ptr<SessionContext> ctx,
    rs::PostGISClient &db,
    std::mutex &db_mutex,
    Ort::Env &ort_env)
{
    rs::PipelineConfig cfg;
    {
        std::lock_guard<std::mutex> lock(ctx->mutex);
        cfg = ctx->config;
    }

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
    dbUpdateProgress(db, db_mutex, session_id, 0);

    rs::CoordinateMapper mapper(engine.metadata());
    {
        std::lock_guard<std::mutex> lock(ctx->mutex);
        ctx->info.footprint = mapper.imageFootprint();
    }
    rs::ThreadPool *pool = nullptr;
    {
        std::lock_guard<std::mutex> lock(ctx->mutex);
        // worker_count * 2 = backpressure chuẩn
        // 7 workers → queue tối đa 14 tiles
        int queue_cap = (cfg.max_workers > 0 ? cfg.max_workers : 7) * 2;

        ctx->pool = std::make_unique<rs::ThreadPool>(
            cfg.max_workers,
            queue_cap);

        LOG_INFO("Pipeline",
                 "ThreadPool: workers=" + std::to_string(ctx->pool->workerCount()) + " queue_capacity=" + std::to_string(queue_cap));
        pool = ctx->pool.get();
    }

    std::atomic<int> tiles_done{0};
    std::mutex results_mutex;
    std::vector<rs::GeoDetection> all_geo_dets;

    {
        std::lock_guard<std::mutex> lock(ctx->mutex);
        ctx->info.status = rs::SessionStatus::PROCESSING;
    }

    // Tạo AI pool — mỗi worker 1 instance
    int n_workers = ctx->pool->workerCount();
    std::vector<std::unique_ptr<rs::AIInterface>> ai_pool;

    if (cfg.model == "onnx" || cfg.model == "dota_obb" || cfg.model == "segformer_loveda")
    {
        if (cfg.model == "dota_obb" && cfg.model_path == "/app/models/yolov8n-seg.onnx")
            cfg.model_path = "/app/models/yolo11n-obb.onnx";
        if (cfg.model == "segformer_loveda" && cfg.model_path == "/app/models/yolov8n-seg.onnx")
            cfg.model_path = "/app/models/segformer-loveda-b2.onnx";

        bool model_ok = std::filesystem::exists(cfg.model_path);
        for (int i = 0; i < n_workers; i++)
        {
            if (model_ok)
            {
                try
                {
                    if (cfg.model == "dota_obb")
                    {
                        ai_pool.push_back(std::make_unique<rs::OnnxObbAI>(
                            ort_env, cfg.model_path, cfg.conf_thresh));
                    }
                    else if (cfg.model == "segformer_loveda")
                    {
                        ai_pool.push_back(std::make_unique<rs::OnnxSegFormerAI>(
                            ort_env, cfg.model_path, cfg.conf_thresh));
                    }
                    else
                    {
                        ai_pool.push_back(std::make_unique<rs::OnnxAI>(
                            ort_env, cfg.model_path, cfg.conf_thresh));
                    }
                }
                catch (const std::exception &e)
                {
                    LOG_ERROR("Pipeline",
                              "OnnxAI init failed: " + std::string(e.what()) + " — falling back to MockAI");
                    ai_pool.push_back(
                        std::make_unique<rs::MockAI>(3, 42));
                }
            }
            else
            {
                LOG_WARN("Pipeline",
                         "Model not found: " + cfg.model_path + " — using MockAI");
                ai_pool.push_back(std::make_unique<rs::MockAI>(3, 42));
            }
        }
    }
    else
    {
        for (int i = 0; i < n_workers; i++)
            ai_pool.push_back(std::make_unique<rs::MockAI>(3, 42));
    }

    LOG_INFO("Pipeline",
             "AI backend: " + ai_pool[0]->name() + " × " + std::to_string(n_workers) + " workers");

    // Worker function — dùng worker_id index vào ai_pool
    ctx->pool->start([&](rs::TileData &tile, int worker_id)
                     {
    auto& ai      = ai_pool[worker_id];
    auto  dets    = ai->infer(tile);
    auto  geo_dets = mapper.mapDetections(dets, tile);

    {
        std::lock_guard<std::mutex> lk(results_mutex);
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
        dbUpdateProgress(db, db_mutex, session_id, done); });

    engine.iterateTiles(session_id, [&](rs::TileData tile)
                        {
                            if (ctx->cancel_requested.load())
                                return false;
                            return pool->submit(std::move(tile)); });
    // ─── Fan-In point: tất cả Worker đã xong ─────────────────────
    pool->waitAll();

    if (ctx->cancel_requested.load())
    {
        {
            std::lock_guard<std::mutex> lk(ctx->mutex);
            ctx->info.status = rs::SessionStatus::ERROR;
        }
        dbUpdateStatus(db, db_mutex, session_id, rs::SessionStatus::ERROR);
        engine.close();
        return;
    }

    // ─── Stitching: 1 luồng, global view ─────────────────────────
    {
        std::lock_guard<std::mutex> lk(ctx->mutex);
        ctx->info.status = rs::SessionStatus::STITCHING;
    }

    rs::Stitcher stitcher(0.5f);
    auto final_dets = stitcher.runNMS(all_geo_dets);

    // ─── Save to PostGIS ──────────────────────────────────────────
    {
        std::lock_guard<std::mutex> lk(ctx->mutex);
        ctx->info.status = rs::SessionStatus::SAVING;
    }

    dbInsertDetections(db, db_mutex, final_dets);
    dbUpdateStatus(db, db_mutex, session_id, rs::SessionStatus::DONE);

    {
        std::lock_guard<std::mutex> lk(ctx->mutex);
        ctx->info.status = rs::SessionStatus::DONE;
        ctx->info.tile_done = ctx->info.tile_total;
    }

    LOG_INFO("Pipeline",
             "Session " + std::to_string(session_id) + " DONE. Detections: " + std::to_string(final_dets.size()) + "/" + std::to_string(all_geo_dets.size()) + " (after NMS)");

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
    std::mutex db_mutex;
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

    // ─── ONNX Runtime Environment (global, 1 lần) ─────────────────
    Ort::Env ort_env(ORT_LOGGING_LEVEL_WARNING, "rs_pipeline");

    // ─── UDP Broadcaster ──────────────────────────────────────────
    int udp_port = 9090;
    if (const char *p = std::getenv("UDP_PORT"))
        udp_port = std::stoi(p);

    int udp_interval = 500;
    if (const char *p = std::getenv("UDP_BROADCAST_INTERVAL_MS"))
        udp_interval = std::stoi(p);

    rs::UdpBroadcaster broadcaster(udp_port, udp_interval);

    // active_session_id — track session đang chạy
    std::atomic<int64_t> active_session_id{-1};

    broadcaster.setMetricsProvider([&]() -> rs::SystemMetrics
                                   {
    rs::SystemMetrics m;
    int64_t sid = active_session_id.load();
    if (sid < 0) { m.state = "IDLE"; return m; }

    rs::SessionInfo info = sessions.getInfo(sid);
    static const char* names[] = {
        "IDLE","LOADING","TILING","PROCESSING",
        "STITCHING","SAVING","DONE","ERROR","RECOVERING"
    };
    m.session_id  = info.id;
    m.state       = names[(int)info.status];
    m.tiles_done  = info.tile_done;
    m.tiles_total = info.tile_total;

    // Queue size từ pool nếu đang chạy
    auto ctx = sessions.get(sid);
    if (ctx) {
        std::lock_guard<std::mutex> lock(ctx->mutex);
        if (ctx->pool)
            m.queue_size = (int)ctx->pool->queueSize();
    }

    return m; });

    broadcaster.start();

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
    int64_t sid = -1;
    {
        std::lock_guard<std::mutex> lock(db_mutex);
        sid = db.createSession("pending", 0);
    }
    if (sid <= 0)
        sid = (int64_t)std::time(nullptr);

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
        ctx->info.filename = filename;
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
        ctx->cancel_requested.store(false);
        ctx->info.status = rs::SessionStatus::LOADING;
    }

    active_session_id.store(sid);

    std::thread([ctx, &db, &db_mutex, &active_session_id, &ort_env]() {
        runPipelineAsync(ctx, db, db_mutex, ort_env);

        // Giữ session_id thêm 3 giây để broadcaster kịp gửi DONE
        std::this_thread::sleep_for(std::chrono::seconds(3));
        active_session_id.store(-1);
    }).detach();

    return true; });

    gateway.onCancel([&](int64_t sid) -> bool
                     {
        auto ctx = sessions.get(sid);
        if (!ctx) return false;
        // Close queue → workers tự dừng ở tile tiếp theo
        {
            std::lock_guard<std::mutex> lock(ctx->mutex);
            ctx->cancel_requested.store(true);
            if (ctx->pool)
                ctx->pool->requestStop();
            ctx->info.status = rs::SessionStatus::ERROR;
        }
        dbUpdateStatus(db, db_mutex, sid, rs::SessionStatus::ERROR);
        return true; });

    gateway.onStatus([&](int64_t sid) -> rs::SessionInfo
                     { return sessions.getInfo(sid); });

    gateway.onResults([&](int64_t sid) -> std::string
                      { return dbQueryDetections(db, db_mutex, sid); });

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
