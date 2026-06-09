#pragma once
#include "common/types.hpp"
#include <string>
#include <vector>
#include <functional>

class GDALDataset;

namespace rs
{

    struct TileGrid
    {
        int tile_size;
        int overlap;
        int stride; // = tile_size - overlap
        int cols;
        int rows;
        int image_width;
        int image_height;
        int band_count;
    };

    struct ImageMetadata
    {
        int width;
        int height;
        int band_count;
        std::string crs_wkt;
        double geo_transform[6];
        std::string filepath;

        // CRS có phải EPSG:4326 không
        // false = cần reproject trước khi lưu PostGIS
        bool is_geographic;
    };

    // Callback báo tiến độ: (tiles_done, tiles_total)
    using ProgressCallback = std::function<void(int, int)>;

    class TilingEngine
    {
    public:
        explicit TilingEngine(int tile_size = 512, int overlap = 64);
        ~TilingEngine();

        TilingEngine(const TilingEngine &) = delete;
        TilingEngine &operator=(const TilingEngine &) = delete;

        // Mở file GeoTIFF, đọc metadata
        bool open(const std::string &filepath);
        void close();

        // Đọc 1 tile theo (row, col)
        bool readTile(int row, int col, int64_t session_id, TileData &out);

        // Đọc tất cả tile, gọi callback cho từng tile
        // Dùng cho single-thread producer
        using TileCallback = std::function<bool(TileData)>;
        void iterateTiles(int64_t session_id, TileCallback cb);

        // Coordinate mapping
        GeoPoint pixelToGeo(int pixel_x, int pixel_y) const;

        // 4 góc của 1 tile → 4 GeoPoint (polygon)
        std::vector<GeoPoint> tileToGeoPolygon(const TileData &tile) const;

        // Getter
        const ImageMetadata &metadata() const { return metadata_; }
        const TileGrid &grid() const { return grid_; }
        int totalTiles() const { return grid_.rows * grid_.cols; }
        bool isOpen() const { return dataset_ != nullptr; }

        // Validate file trước khi xử lý
        static bool validateFile(const std::string &filepath,
                                 std::string &error_msg);

        // Progress callback
        void setProgressCallback(ProgressCallback cb) { progress_cb_ = cb; }

    private:
        GDALDataset *dataset_ = nullptr;
        ImageMetadata metadata_;
        TileGrid grid_;
        int tile_size_;
        int overlap_;
        ProgressCallback progress_cb_;

        void computeGrid();
        void detectCRS();
    };

} // namespace rs
