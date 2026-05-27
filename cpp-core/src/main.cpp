#include "common/logger.hpp"
#include "common/types.hpp"
#include "pipeline/tiling_engine.hpp"
#include "pipeline/thread_pool.hpp"
#include <iostream>
#include <atomic>

int main(int argc, char *argv[])
{
    LOG_INFO("main", "Remote Sensing Pipeline v0.1.0");

    std::string filepath = (argc > 1)
                               ? argv[1]
                               : "/app/data/test.tif";

    // ─── Tiling Engine ────────────────────────────────────────
    rs::TilingEngine engine(512, 64);
    if (!engine.open(filepath))
    {
        LOG_ERROR("main", "Failed to open GeoTIFF. Exiting.");
        return 1;
    }

    const auto &grid = engine.grid();
    std::cout << "\nTotal tiles: " << engine.totalTiles() << "\n\n";

    // ─── Thread Pool ──────────────────────────────────────────
    rs::ThreadPool pool; // tự detect số core

    // Worker function: hiện tại chỉ log tile index
    // Tuần 2 sẽ thay bằng AI inference thật
    std::atomic<int> processed{0};

    pool.start([&processed](rs::TileData &tile)
               {
        // TODO Tuần 2: thay bằng MockAI::infer(tile)
        LOG_INFO("Worker",
            "Processing tile (" + std::to_string(tile.tile_row)
            + "," + std::to_string(tile.tile_col) + ")"
            + " offset=(" + std::to_string(tile.pixel_x_offset)
            + "," + std::to_string(tile.pixel_y_offset) + ")"
            + " size=" + std::to_string(tile.width)
            + "x" + std::to_string(tile.height)
        );
        processed++; });

    // ─── Producer: đẩy tiles vào queue ───────────────────────
    LOG_INFO("main", "Submitting tiles to queue...");

    for (int row = 0; row < grid.rows; row++)
    {
        for (int col = 0; col < grid.cols; col++)
        {
            rs::TileData tile;
            if (engine.readTile(row, col, /*session_id=*/1, tile))
            {
                pool.submit(std::move(tile));
            }
        }
    }

    LOG_INFO("main", "All tiles submitted: " + std::to_string(pool.tilesSubmitted()));

    // ─── Chờ tất cả worker xong ──────────────────────────────
    pool.waitAll();

    std::cout << "\n=== RESULT ===\n";
    std::cout << "Submitted : " << pool.tilesSubmitted() << "\n";
    std::cout << "Processed : " << processed.load() << "\n";

    engine.close();
    return 0;
}