#include "common/logger.hpp"
#include "common/types.hpp"
#include "pipeline/tiling_engine.hpp"
#include "pipeline/thread_pool.hpp"
#include "inference/mock_ai.hpp"
#include <iostream>
#include <atomic>
#include <mutex>
#include <vector>

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

    // ─── Tiling Engine ────────────────────────────────────────
    rs::TilingEngine engine(512, 64);
    engine.setProgressCallback([](int done, int total)
                               {
        if (done % 40 == 0 || done == total)
            LOG_INFO("Progress", std::to_string(done)
                + "/" + std::to_string(total)); });

    if (!engine.open(filepath))
        return 1;

    // ─── MockAI — 1 instance per worker thread ────────────────
    // Mỗi worker có AI riêng → không share state → không cần mutex
    rs::ThreadPool pool;

    std::atomic<int> total_detections{0};
    std::mutex results_mutex;
    std::vector<rs::Detection> all_detections; // gom kết quả

    pool.start([&](rs::TileData &tile)
               {
        // Mỗi lambda call là 1 thread riêng
        // MockAI tạo trên stack → thread-local, không race
        rs::MockAI ai(3, 42);

        auto detections = ai.infer(tile);

        // Ghi vào shared vector — cần lock
        {
            std::lock_guard<std::mutex> lock(results_mutex);
            for (auto& d : detections) {
                all_detections.push_back(d);
            }
        }

        total_detections += (int)detections.size(); });

    // ─── Producer ─────────────────────────────────────────────
    LOG_INFO("main", "Starting pipeline...");
    engine.iterateTiles(1, [&pool](rs::TileData tile)
                        { pool.submit(std::move(tile)); });

    pool.waitAll();

    // ─── Summary ──────────────────────────────────────────────
    std::cout << "\n=== PIPELINE SUMMARY ===\n";
    std::cout << "Tiles processed : " << pool.tilesProcessed() << "\n";
    std::cout << "Total detections: " << total_detections.load() << "\n";

    // Sample 5 detection đầu tiên
    std::cout << "\n=== SAMPLE DETECTIONS (first 5) ===\n";
    const char *class_names[] = {"vegetation", "water", "building", "road"};
    int show = std::min(5, (int)all_detections.size());
    for (int i = 0; i < show; i++)
    {
        const auto &d = all_detections[i];
        std::cout << "  class=" << class_names[d.class_id]
                  << " conf=" << d.confidence
                  << " bbox=(" << d.bbox.x << "," << d.bbox.y
                  << " " << d.bbox.width << "x" << d.bbox.height
                  << ")\n";
    }

    engine.close();
    return 0;
}