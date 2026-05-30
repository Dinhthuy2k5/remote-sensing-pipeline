#include "common/logger.hpp"
#include "common/types.hpp"
#include "pipeline/tiling_engine.hpp"
#include "pipeline/thread_pool.hpp"
#include "pipeline/coordinate_mapper.hpp"
#include "pipeline/state_machine.hpp"
#include "database/postgis_client.hpp"
#include "inference/mock_ai.hpp"
#include <iostream>
#include <atomic>
#include <mutex>
#include <vector>
#include <cstdlib>
#include <thread>
#include <chrono>

int main(int argc, char *argv[])
{
    LOG_INFO("main", "Remote Sensing Pipeline v0.1.0");

    std::string filepath = (argc > 1) ? argv[1] : "/app/data/test.tif";

    // ─── Validate ─────────────────────────────────────────────
    std::string err;
    if (!rs::TilingEngine::validateFile(filepath, err))
    {
        LOG_ERROR("main", err);
        return 1;
    }

    // ─── PostGIS với retry ────────────────────────────────────────
    std::string conn_str =
        "host=" + std::string(std::getenv("POSTGRES_HOST") ? std::getenv("POSTGRES_HOST") : "localhost") +
        " port=" + std::string(std::getenv("POSTGRES_PORT") ? std::getenv("POSTGRES_PORT") : "5432") +
        " dbname=" + std::string(std::getenv("POSTGRES_DB") ? std::getenv("POSTGRES_DB") : "remote_sensing") +
        " user=" + std::string(std::getenv("POSTGRES_USER") ? std::getenv("POSTGRES_USER") : "rsuser") +
        " password=" + std::string(std::getenv("POSTGRES_PASSWORD") ? std::getenv("POSTGRES_PASSWORD") : "rspassword");

    rs::PostGISClient db(conn_str);

    // Retry connect tối đa 10 lần, mỗi lần chờ 2 giây
    int retry = 0;
    while (!db.isConnected() && retry < 10)
    {
        retry++;
        LOG_WARN("main", "PostGIS not ready, retrying " + std::to_string(retry) + "/10 in 2s...");
        std::this_thread::sleep_for(std::chrono::seconds(2));
        db = rs::PostGISClient(conn_str); // reconnect
    }

    bool db_available = db.isConnected();
    if (!db_available)
        LOG_WARN("main", "PostGIS unavailable after retries - running without DB.");
    else
        LOG_INFO("main", "PostGIS connected successfully.");

    // ─── Tiling Engine ────────────────────────────────────────
    rs::TilingEngine engine(512, 64);
    if (!engine.open(filepath))
        return 1;

    // ─── State Machine ────────────────────────────────────────
    int64_t session_id = db_available
                             ? db.createSession(filepath, engine.totalTiles())
                             : 1;

    rs::StateMachine sm(session_id);
    sm.onStateChange([&db, &db_available](rs::SessionStatus from, rs::SessionStatus to)
                     {
        if (db_available)
            db.updateSessionStatus(
                /* session_id captured via lambda */ 0, to); });

    sm.transition(rs::SessionStatus::LOADING);
    sm.transition(rs::SessionStatus::TILING);

    // ─── Coordinate Mapper ────────────────────────────────────
    rs::CoordinateMapper mapper(engine.metadata());

    // ─── Thread Pool ──────────────────────────────────────────
    sm.transition(rs::SessionStatus::PROCESSING);

    rs::ThreadPool pool;
    std::atomic<int> tiles_done{0};
    std::mutex results_mutex;
    std::vector<rs::GeoDetection> all_geo_dets;

    pool.start([&](rs::TileData &tile)
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
        if (db_available && done % 20 == 0)
            db.updateSessionProgress(session_id, done); });

    engine.iterateTiles(session_id, [&pool](rs::TileData tile)
                        { pool.submit(std::move(tile)); });
    pool.waitAll();

    // ─── Save to PostGIS ──────────────────────────────────────
    sm.transition(rs::SessionStatus::STITCHING);
    sm.transition(rs::SessionStatus::SAVING);

    if (db_available)
    {
        LOG_INFO("main", "Inserting " + std::to_string(all_geo_dets.size()) + " detections into PostGIS...");

        bool ok = db.insertDetections(all_geo_dets);

        if (ok)
        {
            db.updateSessionStatus(session_id, rs::SessionStatus::DONE);
            sm.transition(rs::SessionStatus::DONE);

            // Query lại GeoJSON để verify
            std::string geojson = db.queryDetectionsGeoJSON(session_id);
            std::cout << "\n=== GeoJSON preview (first 200 chars) ===\n";
            std::cout << geojson.substr(0, 200) << "...\n";
        }
        else
        {
            sm.transition(rs::SessionStatus::ERROR);
        }
    }
    else
    {
        sm.transition(rs::SessionStatus::DONE);
        LOG_INFO("main", "Skipped DB insert (no connection).");
    }

    // ─── Summary ──────────────────────────────────────────────
    std::cout << "\n=== FINAL SUMMARY ===\n";
    std::cout << "Session   : " << session_id << "\n";
    std::cout << "State     : "
              << rs::StateMachine::toString(sm.current()) << "\n";
    std::cout << "Tiles     : " << pool.tilesProcessed() << "\n";
    std::cout << "Detections: " << all_geo_dets.size() << "\n";

    engine.close();
    return 0;
}