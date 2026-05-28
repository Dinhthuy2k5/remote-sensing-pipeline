#include "common/logger.hpp"
#include "common/types.hpp"
#include "pipeline/tiling_engine.hpp"
#include "pipeline/thread_pool.hpp"
#include <iostream>
#include <atomic>
#include <thread>

int main(int argc, char *argv[])
{
    LOG_INFO("main", "Remote Sensing Pipeline v0.1.0");

    std::string filepath = (argc > 1) ? argv[1] : "/app/data/test.tif";

    // ─── Validate file ────────────────────────────────────────
    std::string err;
    if (!rs::TilingEngine::validateFile(filepath, err))
    {
        LOG_ERROR("main", "Validation failed: " + err);
        return 1;
    }
    LOG_INFO("main", "File validated OK.");

    // ─── Tiling Engine ────────────────────────────────────────
    rs::TilingEngine engine(512, 64);

    engine.setProgressCallback([](int done, int total)
                               {
        if (done % 20 == 0 || done == total) {
            LOG_INFO("Progress",
                std::to_string(done) + "/" + std::to_string(total)
                + " tiles read ("
                + std::to_string(done * 100 / total) + "%)");
        } });

    if (!engine.open(filepath))
    {
        LOG_ERROR("main", "Failed to open GeoTIFF.");
        return 1;
    }

    // ─── Thread Pool ──────────────────────────────────────────
    rs::ThreadPool pool;
    std::atomic<int> processed{0};

    pool.start([&processed](rs::TileData &tile)
               {
        // Validate TileData struct đầy đủ
        if (!tile.isValid()) {
            LOG_ERROR("Worker", "Invalid tile at ("
                + std::to_string(tile.tile_row) + ","
                + std::to_string(tile.tile_col) + ")");
            return;
        }

        // TODO Ngày 10-11: thay bằng MockAI::infer(tile)
        LOG_DEBUG("Worker",
            "tile_index=" + std::to_string(tile.tile_index)
            + " (" + std::to_string(tile.tile_row)
            + "," + std::to_string(tile.tile_col) + ")"
            + " " + std::to_string(tile.width)
            + "x" + std::to_string(tile.height)
            + "x" + std::to_string(tile.band_count)
            + " buf=" + std::to_string(tile.pixels.size()) + "B");
        processed++; });

    // ─── Producer ─────────────────────────────────────────────
    LOG_INFO("main", "Starting tile producer...");

    engine.iterateTiles(/*session_id=*/1, [&pool](rs::TileData tile)
                        { pool.submit(std::move(tile)); });

    LOG_INFO("main", "All tiles submitted: " + std::to_string(pool.tilesSubmitted()));

    pool.waitAll();

    // ─── Summary ──────────────────────────────────────────────
    std::cout << "\n=== SUMMARY ===\n";
    std::cout << "Total tiles : " << engine.totalTiles() << "\n";
    std::cout << "Submitted   : " << pool.tilesSubmitted() << "\n";
    std::cout << "Processed   : " << processed.load() << "\n";

    // Test tileToGeoPolygon trên tile (0,0)
    rs::TileData sample;
    if (engine.readTile(0, 0, 1, sample))
    {
        auto polygon = engine.tileToGeoPolygon(sample);
        std::cout << "\n=== TILE (0,0) GEO POLYGON ===\n";
        const char *corners[] = {"TL", "TR", "BR", "BL"};
        for (int i = 0; i < 4; i++)
        {
            std::cout << corners[i] << ": lat=" << polygon[i].lat
                      << " lon=" << polygon[i].lon << "\n";
        }
        std::cout << "(Note: UTM coords - reproject to WGS84 on Day 12-13)\n";
    }

    engine.close();
    return 0;
}