#include "pipeline/tiling_engine.hpp"
#include "common/logger.hpp"

#include <gdal_priv.h>
#include <ogr_spatialref.h>
#include <stdexcept>
#include <cstring>
#include <algorithm>

namespace rs
{

    TilingEngine::TilingEngine(int tile_size, int overlap)
        : tile_size_(tile_size), overlap_(overlap)
    {
        // Đăng ký tất cả GDAL driver (GeoTIFF, PNG, ...)
        GDALAllRegister();
    }

    TilingEngine::~TilingEngine()
    {
        close();
    }

    // ─── open ─────────────────────────────────────────────────────
    bool TilingEngine::open(const std::string &filepath)
    {
        close(); // đóng file cũ nếu có

        dataset_ = (GDALDataset *)GDALOpen(filepath.c_str(), GA_ReadOnly);
        if (!dataset_)
        {
            LOG_ERROR("TilingEngine", "Cannot open file: " + filepath);
            return false;
        }

        // Đọc metadata
        metadata_.filepath = filepath;
        metadata_.width = dataset_->GetRasterXSize();
        metadata_.height = dataset_->GetRasterYSize();
        metadata_.band_count = dataset_->GetRasterCount();

        // Geotransform: 6 tham số affine
        // [0] = top-left x (longitude)
        // [1] = pixel width (độ/pixel)
        // [2] = rotation (thường = 0)
        // [3] = top-left y (latitude)
        // [4] = rotation (thường = 0)
        // [5] = pixel height (âm vì y giảm xuống)
        dataset_->GetGeoTransform(metadata_.geo_transform);

        const char *wkt = dataset_->GetProjectionRef();
        metadata_.crs_wkt = wkt ? std::string(wkt) : "";

        computeGrid();

        LOG_INFO("TilingEngine", "Opened: " + filepath);
        LOG_INFO("TilingEngine", "Size: " + std::to_string(metadata_.width) + "x" + std::to_string(metadata_.height) + " | Bands: " + std::to_string(metadata_.band_count));
        LOG_INFO("TilingEngine", "Grid: " + std::to_string(grid_.cols) + "x" + std::to_string(grid_.rows) + " tiles (" + std::to_string(totalTiles()) + " total)");

        return true;
    }

    // ─── close ────────────────────────────────────────────────────
    void TilingEngine::close()
    {
        if (dataset_)
        {
            GDALClose(dataset_);
            dataset_ = nullptr;
            LOG_DEBUG("TilingEngine", "Dataset closed.");
        }
    }

    // ─── computeGrid ──────────────────────────────────────────────
    void TilingEngine::computeGrid()
    {
        grid_.tile_size = tile_size_;
        grid_.overlap = overlap_;
        grid_.image_width = metadata_.width;
        grid_.image_height = metadata_.height;
        grid_.band_count = metadata_.band_count;

        // Stride = tile_size - overlap (bước nhảy giữa các tile)
        int stride = tile_size_ - overlap_;

        // Số tile theo mỗi chiều (làm tròn lên để phủ hết ảnh)
        grid_.cols = (metadata_.width + stride - 1) / stride;
        grid_.rows = (metadata_.height + stride - 1) / stride;
    }

    // ─── readTile ─────────────────────────────────────────────────
    bool TilingEngine::readTile(int row, int col, int64_t session_id, TileData &out)
    {
        if (!dataset_)
        {
            LOG_ERROR("TilingEngine", "readTile called but no dataset open.");
            return false;
        }

        int stride = tile_size_ - overlap_;

        // Tính pixel offset của tile này trong ảnh gốc
        int x_off = col * stride;
        int y_off = row * stride;

        // Clamp để không đọc ngoài biên ảnh
        int actual_width = std::min(tile_size_, metadata_.width - x_off);
        int actual_height = std::min(tile_size_, metadata_.height - y_off);

        if (actual_width <= 0 || actual_height <= 0)
        {
            LOG_WARN("TilingEngine", "Tile (" + std::to_string(row) + "," + std::to_string(col) + ") out of bounds, skipping.");
            return false;
        }

        int bands = metadata_.band_count;
        size_t buffer_size = (size_t)actual_width * actual_height * bands;
        out.pixels.resize(buffer_size);

        // Đọc từng band và interleave vào buffer
        // GDAL đọc band-by-band, ta cần pixel-interleaved (HWC format)
        std::vector<uint8_t> band_buf(actual_width * actual_height);

        for (int b = 1; b <= bands; b++)
        {
            GDALRasterBand *band = dataset_->GetRasterBand(b);
            CPLErr err = band->RasterIO(
                GF_Read,
                x_off, y_off,                // offset trong ảnh gốc
                actual_width, actual_height, // vùng đọc
                band_buf.data(),             // buffer nhận data
                actual_width, actual_height, // kích thước buffer
                GDT_Byte,                    // kiểu dữ liệu
                0, 0                         // pixel/line spacing (0 = auto)
            );

            if (err != CE_None)
            {
                LOG_ERROR("TilingEngine", "RasterIO failed at band " + std::to_string(b));
                return false;
            }

            // Interleave: pixel[y][x][b] = band_buf[y*w + x]
            for (int i = 0; i < actual_width * actual_height; i++)
            {
                out.pixels[i * bands + (b - 1)] = band_buf[i];
            }
        }

        // Điền metadata tile
        out.tile_row = row;
        out.tile_col = col;
        out.pixel_x_offset = x_off;
        out.pixel_y_offset = y_off;
        out.width = actual_width;
        out.height = actual_height;
        out.session_id = session_id;

        return true;
    }

    // ─── pixelToGeo ───────────────────────────────────────────────
    GeoPoint TilingEngine::pixelToGeo(int pixel_x, int pixel_y) const
    {
        // Affine transform:
        // lon = gt[0] + pixel_x * gt[1] + pixel_y * gt[2]
        // lat = gt[3] + pixel_x * gt[4] + pixel_y * gt[5]
        const double *gt = metadata_.geo_transform;

        GeoPoint p;
        p.lon = gt[0] + pixel_x * gt[1] + pixel_y * gt[2];
        p.lat = gt[3] + pixel_x * gt[4] + pixel_y * gt[5];
        return p;
    }

} // namespace rs