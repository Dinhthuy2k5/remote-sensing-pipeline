#include "common/logger.hpp"
#include "common/types.hpp"
#include "pipeline/tiling_engine.hpp"
#include <iostream>

int main(int argc, char *argv[])
{
    LOG_INFO("main", "Remote Sensing Pipeline v0.1.0");

    // Nhận path file từ argument, hoặc dùng file test mặc định
    std::string filepath = (argc > 1)
                               ? argv[1]
                               : "/app/data/test.tif";

    rs::TilingEngine engine(512, 64);

    // Ngày 3: test mở file và đọc metadata
    if (!engine.open(filepath))
    {
        LOG_ERROR("main", "Failed to open GeoTIFF. Exiting.");
        return 1;
    }

    const auto &meta = engine.metadata();
    const auto &grid = engine.grid();

    std::cout << "\n=== IMAGE METADATA ===\n";
    std::cout << "File    : " << meta.filepath << "\n";
    std::cout << "Width   : " << meta.width << " px\n";
    std::cout << "Height  : " << meta.height << " px\n";
    std::cout << "Bands   : " << meta.band_count << "\n";
    std::cout << "GeoTransform: ["
              << meta.geo_transform[0] << ", "
              << meta.geo_transform[1] << ", "
              << meta.geo_transform[3] << ", "
              << meta.geo_transform[5] << "]\n";

    std::cout << "\n=== TILE GRID ===\n";
    std::cout << "Tile size : " << grid.tile_size << " px\n";
    std::cout << "Overlap   : " << grid.overlap << " px\n";
    std::cout << "Grid      : " << grid.cols << " x " << grid.rows << "\n";
    std::cout << "Total     : " << engine.totalTiles() << " tiles\n";

    // Ngày 4: test đọc tile đầu tiên (0,0)
    rs::TileData tile;
    if (engine.readTile(0, 0, 1, tile))
    {
        std::cout << "\n=== TILE (0,0) ===\n";
        std::cout << "Size   : " << tile.width << "x" << tile.height << "\n";
        std::cout << "Offset : (" << tile.pixel_x_offset
                  << ", " << tile.pixel_y_offset << ")\n";
        std::cout << "Buffer : " << tile.pixels.size() << " bytes\n";

        // Test pixelToGeo — góc trên trái tile (0,0)
        rs::GeoPoint geo = engine.pixelToGeo(
            tile.pixel_x_offset,
            tile.pixel_y_offset);
        std::cout << "Geo    : lat=" << geo.lat
                  << " lon=" << geo.lon << "\n";
    }

    engine.close();
    return 0;
}