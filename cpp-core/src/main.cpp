#include "common/logger.hpp"
#include "common/types.hpp"
#include "pipeline/tiling_engine.hpp"
#include "pipeline/thread_pool.hpp"
#include "pipeline/coordinate_mapper.hpp"
#include "inference/mock_ai.hpp"
#include <iostream>
#include <atomic>
#include <mutex>
#include <vector>
#include <iomanip>

int main(int argc, char *argv[])
{
    LOG_INFO("main", "Remote Sensing Pipeline v0.1.0");

    std::string filepath = (argc > 1) ? argv[1] : "/app/data/test.tif";

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

    // ─── Coordinate Mapper ────────────────────────────────────
    rs::CoordinateMapper mapper(engine.metadata());
    if (!mapper.isReady())
    {
        LOG_ERROR("main", "CoordinateMapper failed to initialize.");
        return 1;
    }

    // ─── Test nhanh 4 góc ảnh → verify trên Google Maps ──────
    std::cout << std::fixed << std::setprecision(6);
    std::cout << "\n=== IMAGE CORNERS (WGS84) ===\n";

    const auto &meta = engine.metadata();
    struct Corner
    {
        std::string name;
        int px, py;
    };
    std::vector<Corner> corners = {
        {"Top-Left    ", 0, 0},
        {"Top-Right   ", meta.width - 1, 0},
        {"Bottom-Left ", 0, meta.height - 1},
        {"Bottom-Right", meta.width - 1, meta.height - 1}};

    for (const auto &c : corners)
    {
        auto geo = mapper.pixelToWGS84(c.px, c.py);
        std::cout << c.name << ": lat=" << geo.lat
                  << " lon=" << geo.lon << "\n";
    }

    std::cout << "\n→ Copy 1 tọa độ vào Google Maps để verify\n";
    std::cout << "→ URL: https://maps.google.com/?q=<lat>,<lon>\n\n";

    // ─── Thread Pool + MockAI + CoordinateMapper ──────────────
    rs::ThreadPool pool;
    std::atomic<int> total_geo_detections{0};
    std::mutex results_mutex;
    std::vector<rs::GeoDetection> all_geo_detections;

    pool.start([&](rs::TileData &tile)
               {
        rs::MockAI ai(3, 42);
        auto detections = ai.infer(tile);

        // Map sang WGS84
        auto geo_dets = mapper.mapDetections(detections, tile);

        {
            std::lock_guard<std::mutex> lock(results_mutex);
            for (auto& g : geo_dets)
                all_geo_detections.push_back(g);
        }
        total_geo_detections += (int)geo_dets.size(); });

    engine.iterateTiles(1, [&pool](rs::TileData tile)
                        { pool.submit(std::move(tile)); });

    pool.waitAll();

    // ─── Summary ──────────────────────────────────────────────
    std::cout << "=== PIPELINE SUMMARY ===\n";
    std::cout << "Tiles     : " << pool.tilesProcessed() << "\n";
    std::cout << "Geo dets  : " << total_geo_detections.load() << "\n";

    // In 3 GeoDetection đầu tiên để verify tọa độ
    std::cout << "\n=== SAMPLE GEO DETECTIONS (first 3) ===\n";
    const char *class_names[] = {"vegetation", "water", "building", "road"};
    int show = std::min(3, (int)all_geo_detections.size());

    for (int i = 0; i < show; i++)
    {
        const auto &g = all_geo_detections[i];
        std::cout << "Detection " << i << ":\n";
        std::cout << "  class=" << class_names[g.class_id]
                  << " conf=" << g.confidence
                  << " tile=" << g.tile_index << "\n";
        std::cout << "  polygon (WGS84):\n";
        const char *labels[] = {"TL", "TR", "BR", "BL"};
        for (int j = 0; j < 4; j++)
        {
            std::cout << "    " << labels[j]
                      << ": lat=" << g.polygon[j].lat
                      << " lon=" << g.polygon[j].lon << "\n";
        }
    }

    engine.close();
    return 0;
}