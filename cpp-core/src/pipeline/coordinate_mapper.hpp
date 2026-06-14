#pragma once
#include "common/types.hpp"
#include "pipeline/tiling_engine.hpp"
#include <vector>
#include <string>

// Forward declare GDAL để không expose ra ngoài
class OGRCoordinateTransformation;
class OGRSpatialReference;

namespace rs
{

    class CoordinateMapper
    {
    public:
        // Khởi tạo từ metadata của TilingEngine
        explicit CoordinateMapper(const ImageMetadata &meta);
        ~CoordinateMapper();

        // Không cho copy vì giữ raw pointer GDAL
        CoordinateMapper(const CoordinateMapper &) = delete;
        CoordinateMapper &operator=(const CoordinateMapper &) = delete;

        // Pixel trong ảnh gốc → WGS84 (lat, lon)
        GeoPoint pixelToWGS84(int pixel_x, int pixel_y) const;
        std::vector<GeoPoint> imageFootprint() const;

        // Bbox detection trong tile → polygon WGS84 (4 góc)
        std::vector<GeoPoint> detectionToPolygon(
            const Detection &det,
            const TileData &tile) const;

        // Toàn bộ detections của 1 tile → GeoDetections
        std::vector<GeoDetection> mapDetections(
            const std::vector<Detection> &detections,
            const TileData &tile) const;

        bool isReady() const { return transform_ != nullptr || is_geographic_; }

    private:
        const ImageMetadata &meta_;
        OGRCoordinateTransformation *transform_ = nullptr; // UTM→WGS84
        bool is_geographic_ = false;                       // đã là WGS84 rồi

        // Bước 1: pixel → projected coords (dùng affine transform)
        void pixelToProjected(int px, int py,
                              double &proj_x, double &proj_y) const;

        // Bước 2: projected → WGS84 (dùng OGR reproject)
        GeoPoint projectedToWGS84(double proj_x, double proj_y) const;
    };

} // namespace rs
