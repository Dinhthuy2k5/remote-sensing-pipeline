#include "pipeline/coordinate_mapper.hpp"
#include "common/logger.hpp"

#include <ogr_spatialref.h>
#include <ogr_geometry.h>
#include <cmath>
#include <stdexcept>

namespace rs
{

    // ─── Constructor ──────────────────────────────────────────────
    CoordinateMapper::CoordinateMapper(const ImageMetadata &meta)
        : meta_(meta)
    {
        if (meta_.crs_wkt.empty())
        {
            LOG_WARN("CoordinateMapper",
                     "No CRS defined in image. Coordinates will be raw pixel offsets.");
            return;
        }

        // Kiểm tra nếu đã là geographic (WGS84 hoặc tương tự)
        OGRSpatialReference src_srs;
        src_srs.importFromWkt(meta_.crs_wkt.c_str());

        if (src_srs.IsGeographic())
        {
            is_geographic_ = true;
            LOG_INFO("CoordinateMapper",
                     "CRS is already geographic. No reprojection needed.");
            return;
        }

        // Setup reprojection: source CRS → WGS84
        OGRSpatialReference dst_srs;
        dst_srs.importFromEPSG(4326);
        dst_srs.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER); // lon, lat order

        src_srs.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);

        transform_ = OGRCreateCoordinateTransformation(&src_srs, &dst_srs);

        if (!transform_)
        {
            LOG_ERROR("CoordinateMapper",
                      "Failed to create coordinate transformation. Check PROJ data.");
            return;
        }

        LOG_INFO("CoordinateMapper",
                 "Reprojection ready: projected CRS → WGS84 (EPSG:4326)");
    }

    // ─── Destructor ───────────────────────────────────────────────
    CoordinateMapper::~CoordinateMapper()
    {
        if (transform_)
        {
            OGRCoordinateTransformation::DestroyCT(transform_);
            transform_ = nullptr;
        }
    }

    // ─── pixelToProjected ─────────────────────────────────────────
    // Áp dụng affine transform 6 tham số của GDAL:
    // proj_x = gt[0] + px * gt[1] + py * gt[2]
    // proj_y = gt[3] + px * gt[4] + py * gt[5]
    void CoordinateMapper::pixelToProjected(int px, int py,
                                            double &proj_x,
                                            double &proj_y) const
    {
        const double *gt = meta_.geo_transform;
        proj_x = gt[0] + px * gt[1] + py * gt[2];
        proj_y = gt[3] + px * gt[4] + py * gt[5];
    }

    // ─── projectedToWGS84 ─────────────────────────────────────────
    GeoPoint CoordinateMapper::projectedToWGS84(double proj_x,
                                                double proj_y) const
    {
        GeoPoint p;

        if (is_geographic_)
        {
            // Đã là geographic → proj_x = lon, proj_y = lat
            p.lon = proj_x;
            p.lat = proj_y;
            return p;
        }

        if (!transform_)
        {
            // Fallback: trả về projected coords nếu không có transform
            p.lon = proj_x;
            p.lat = proj_y;
            return p;
        }

        double x = proj_x;
        double y = proj_y;

        if (!transform_->Transform(1, &x, &y))
        {
            LOG_WARN("CoordinateMapper",
                     "Transform failed for point (" + std::to_string(proj_x) + ", " + std::to_string(proj_y) + ")");
            p.lon = proj_x;
            p.lat = proj_y;
            return p;
        }

        // Với OAMS_TRADITIONAL_GIS_ORDER: x=lon, y=lat
        p.lon = x;
        p.lat = y;
        return p;
    }

    // ─── pixelToWGS84 ─────────────────────────────────────────────
    GeoPoint CoordinateMapper::pixelToWGS84(int pixel_x, int pixel_y) const
    {
        double proj_x, proj_y;
        pixelToProjected(pixel_x, pixel_y, proj_x, proj_y);
        return projectedToWGS84(proj_x, proj_y);
    }

    std::vector<GeoPoint> CoordinateMapper::imageFootprint() const
    {
        return {
            pixelToWGS84(0, 0),
            pixelToWGS84(meta_.width, 0),
            pixelToWGS84(meta_.width, meta_.height),
            pixelToWGS84(0, meta_.height)
        };
    }

    // ─── detectionToPolygon ───────────────────────────────────────
    // Bbox trong tile (pixel coords) → 4 góc WGS84
    // Lưu ý: bbox.x, bbox.y là offset trong tile
    //        phải cộng thêm tile.pixel_x_offset để ra coords ảnh gốc
    std::vector<GeoPoint> CoordinateMapper::detectionToPolygon(
        const Detection &det,
        const TileData &tile) const
    {
        if (det.polygon.size() >= 4)
        {
            std::vector<GeoPoint> polygon;
            polygon.reserve(det.polygon.size());
            for (const auto &p : det.polygon)
            {
                int px = tile.pixel_x_offset + (int)std::round(p.x);
                int py = tile.pixel_y_offset + (int)std::round(p.y);
                polygon.push_back(pixelToWGS84(px, py));
            }
            return polygon;
        }

        // Tọa độ pixel trong ảnh gốc
        int x0 = tile.pixel_x_offset + (int)det.bbox.x;
        int y0 = tile.pixel_y_offset + (int)det.bbox.y;
        int x1 = x0 + (int)det.bbox.width;
        int y1 = y0 + (int)det.bbox.height;

        // 4 góc theo chiều kim đồng hồ: TL, TR, BR, BL
        return {
            pixelToWGS84(x0, y0), // Top-Left
            pixelToWGS84(x1, y0), // Top-Right
            pixelToWGS84(x1, y1), // Bottom-Right
            pixelToWGS84(x0, y1)  // Bottom-Left
        };
    }

    // ─── mapDetections ────────────────────────────────────────────
    std::vector<GeoDetection> CoordinateMapper::mapDetections(
        const std::vector<Detection> &detections,
        const TileData &tile) const
    {
        std::vector<GeoDetection> results;
        results.reserve(detections.size());

        for (const auto &det : detections)
        {
            GeoDetection geo;
            geo.polygon = detectionToPolygon(det, tile);
            geo.class_id = det.class_id;
            geo.confidence = det.confidence;
            geo.session_id = tile.session_id;
            geo.tile_index = tile.tile_index;
            results.push_back(geo);
        }

        return results;
    }

} // namespace rs
