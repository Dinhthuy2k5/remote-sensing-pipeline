#include "pipeline/tiling_engine.hpp"
#include "common/logger.hpp"

#include <gdal_priv.h>
#include <ogr_spatialref.h>
#include <algorithm>
#include <stdexcept>
#include <sys/stat.h>

namespace rs
{

    TilingEngine::TilingEngine(int tile_size, int overlap)
        : tile_size_(tile_size), overlap_(overlap)
    {
        GDALAllRegister();
    }

    TilingEngine::~TilingEngine() { close(); }

    // ─── validateFile ─────────────────────────────────────────────
    bool TilingEngine::validateFile(const std::string &filepath,
                                    std::string &error_msg)
    {
        // Kiểm tra file tồn tại
        struct stat st;
        if (stat(filepath.c_str(), &st) != 0)
        {
            error_msg = "File not found: " + filepath;
            return false;
        }

        // Kiểm tra extension
        auto ext = filepath.substr(filepath.find_last_of('.') + 1);
        for (auto &c : ext)
            c = tolower(c);
        if (ext != "tif" && ext != "tiff")
        {
            error_msg = "Unsupported format: ." + ext + " (expected .tif/.tiff)";
            return false;
        }

        // Thử mở bằng GDAL
        GDALAllRegister();
        GDALDataset *ds = (GDALDataset *)GDALOpen(filepath.c_str(), GA_ReadOnly);
        if (!ds)
        {
            error_msg = "GDAL cannot open file: " + filepath;
            return false;
        }
        GDALClose(ds);
        return true;
    }

    // ─── open ─────────────────────────────────────────────────────
    bool TilingEngine::open(const std::string &filepath)
    {
        close();

        std::string err;
        if (!validateFile(filepath, err))
        {
            LOG_ERROR("TilingEngine", err);
            return false;
        }

        dataset_ = (GDALDataset *)GDALOpen(filepath.c_str(), GA_ReadOnly);
        if (!dataset_)
        {
            LOG_ERROR("TilingEngine", "GDALOpen failed: " + filepath);
            return false;
        }

        metadata_.filepath = filepath;
        metadata_.width = dataset_->GetRasterXSize();
        metadata_.height = dataset_->GetRasterYSize();
        metadata_.band_count = dataset_->GetRasterCount();

        dataset_->GetGeoTransform(metadata_.geo_transform);

        const char *wkt = dataset_->GetProjectionRef();
        metadata_.crs_wkt = wkt ? std::string(wkt) : "";

        detectCRS();
        computeGrid();

        LOG_INFO("TilingEngine", "Opened: " + filepath);
        LOG_INFO("TilingEngine",
                 "Size: " + std::to_string(metadata_.width) + "x" + std::to_string(metadata_.height) + " | Bands: " + std::to_string(metadata_.band_count) + " | CRS geographic: " + (metadata_.is_geographic ? "YES (WGS84)" : "NO (projected, needs reproject)"));
        LOG_INFO("TilingEngine",
                 "Grid: " + std::to_string(grid_.cols) + "x" + std::to_string(grid_.rows) + " tiles (" + std::to_string(totalTiles()) + " total)" + " | stride=" + std::to_string(grid_.stride));

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

    // ─── detectCRS ────────────────────────────────────────────────
    void TilingEngine::detectCRS()
    {
        if (metadata_.crs_wkt.empty())
        {
            metadata_.is_geographic = false;
            return;
        }
        OGRSpatialReference srs;
        srs.importFromWkt(metadata_.crs_wkt.c_str());
        metadata_.is_geographic = (srs.IsGeographic() != 0);
    }

    // ─── computeGrid ──────────────────────────────────────────────
    void TilingEngine::computeGrid()
    {
        grid_.tile_size = tile_size_;
        grid_.overlap = overlap_;
        grid_.stride = tile_size_ - overlap_;
        grid_.image_width = metadata_.width;
        grid_.image_height = metadata_.height;
        grid_.band_count = metadata_.band_count;
        grid_.cols = (metadata_.width + grid_.stride - 1) / grid_.stride;
        grid_.rows = (metadata_.height + grid_.stride - 1) / grid_.stride;
    }

    // ─── readTile ─────────────────────────────────────────────────
    bool TilingEngine::readTile(int row, int col,
                                int64_t session_id, TileData &out)
    {
        if (!dataset_)
        {
            LOG_ERROR("TilingEngine", "readTile: no dataset open.");
            return false;
        }

        int x_off = col * grid_.stride;
        int y_off = row * grid_.stride;

        int actual_w = std::min(tile_size_, metadata_.width - x_off);
        int actual_h = std::min(tile_size_, metadata_.height - y_off);

        if (actual_w <= 0 || actual_h <= 0)
        {
            LOG_WARN("TilingEngine",
                     "Tile (" + std::to_string(row) + "," + std::to_string(col) + ") out of bounds.");
            return false;
        }

        int bands = metadata_.band_count;
        out.pixels.resize((size_t)actual_w * actual_h * bands);

        std::vector<uint8_t> band_buf(actual_w * actual_h);

        for (int b = 1; b <= bands; b++)
        {
            GDALRasterBand *band = dataset_->GetRasterBand(b);
            CPLErr err = band->RasterIO(
                GF_Read,
                x_off, y_off,
                actual_w, actual_h,
                band_buf.data(),
                actual_w, actual_h,
                GDT_Byte,
                0, 0);

            if (err != CE_None)
            {
                LOG_ERROR("TilingEngine",
                          "RasterIO failed band " + std::to_string(b));
                return false;
            }

            // Band-sequential → pixel-interleaved (HWC)
            for (int i = 0; i < actual_w * actual_h; i++)
            {
                out.pixels[i * bands + (b - 1)] = band_buf[i];
            }
        }

        out.tile_row = row;
        out.tile_col = col;
        out.tile_index = row * grid_.cols + col;
        out.pixel_x_offset = x_off;
        out.pixel_y_offset = y_off;
        out.width = actual_w;
        out.height = actual_h;
        out.band_count = bands;
        out.session_id = session_id;

        return true;
    }

    // ─── iterateTiles ─────────────────────────────────────────────
    void TilingEngine::iterateTiles(int64_t session_id, TileCallback cb)
    {
        int total = totalTiles();
        int done = 0;

        for (int row = 0; row < grid_.rows; row++)
        {
            for (int col = 0; col < grid_.cols; col++)
            {
                TileData tile;
                if (readTile(row, col, session_id, tile))
                {
                    cb(std::move(tile));
                }
                done++;
                if (progress_cb_)
                    progress_cb_(done, total);
            }
        }
    }

    // ─── pixelToGeo ───────────────────────────────────────────────
    GeoPoint TilingEngine::pixelToGeo(int pixel_x, int pixel_y) const
    {
        const double *gt = metadata_.geo_transform;
        GeoPoint p;
        p.lon = gt[0] + pixel_x * gt[1] + pixel_y * gt[2];
        p.lat = gt[3] + pixel_x * gt[4] + pixel_y * gt[5];
        return p;
    }

    // ─── tileToGeoPolygon ─────────────────────────────────────────
    std::vector<GeoPoint> TilingEngine::tileToGeoPolygon(
        const TileData &tile) const
    {
        int x0 = tile.pixel_x_offset;
        int y0 = tile.pixel_y_offset;
        int x1 = x0 + tile.width;
        int y1 = y0 + tile.height;

        // 4 góc theo chiều kim đồng hồ: TL, TR, BR, BL
        return {
            pixelToGeo(x0, y0),
            pixelToGeo(x1, y0),
            pixelToGeo(x1, y1),
            pixelToGeo(x0, y1)};
    }

} // namespace rs