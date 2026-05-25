#pragma once
#include "common/types.hpp"
#include <string>
#include <vector>
#include <functional>

// Forward declare để không include gdal.h ra ngoài
class GDALDataset;

namespace rs
{

    struct TileGrid
    {
        int tile_size;
        int overlap;
        int cols; // số tile theo chiều ngang
        int rows; // số tile theo chiều dọc
        int image_width;
        int image_height;
        int band_count;
    };

    struct ImageMetadata
    {
        int width;
        int height;
        int band_count;
        std::string crs_wkt;     // coordinate reference system
        double geo_transform[6]; // GDAL affine transform
        std::string filepath;
    };

    class TilingEngine
    {
    public:
        TilingEngine(int tile_size = 512, int overlap = 64);
        ~TilingEngine();

        // Mở file, đọc metadata — trả false nếu thất bại
        bool open(const std::string &filepath);

        // Đóng file, giải phóng GDAL handle
        void close();

        // Getter metadata sau khi open()
        const ImageMetadata &metadata() const { return metadata_; }
        const TileGrid &grid() const { return grid_; }

        // Đọc 1 tile theo (row, col) vào TileData
        // Trả false nếu đọc thất bại
        bool readTile(int row, int col, int64_t session_id, TileData &out);

        // Pixel offset → tọa độ địa lý (lat, lon)
        GeoPoint pixelToGeo(int pixel_x, int pixel_y) const;

        // Tổng số tile
        int totalTiles() const { return grid_.rows * grid_.cols; }

        bool isOpen() const { return dataset_ != nullptr; }

    private:
        GDALDataset *dataset_ = nullptr;
        ImageMetadata metadata_;
        TileGrid grid_;

        int tile_size_;
        int overlap_;

        void computeGrid();
    };

} // namespace rs