#include "stitching/stitcher.hpp"
#include "common/logger.hpp"
#include <algorithm>
#include <vector>
#include <limits>

namespace rs
{

    Stitcher::Stitcher(float iou_threshold)
        : iou_threshold_(iou_threshold)
    {
    }

    // ─── toBBox ───────────────────────────────────────────────────
    Stitcher::BBox Stitcher::toBBox(const GeoDetection &d)
    // dùng để biến một hình đa giác méo mó, phức tạp (Polygon) thành một hình chữ nhật ngay ngắn bao bọc lấy nó.
    {
        constexpr double MAX_DBL = std::numeric_limits<double>::max();
        constexpr double MIN_DBL = std::numeric_limits<double>::lowest();

        BBox b{MAX_DBL, MAX_DBL, MIN_DBL, MIN_DBL};
        for (const auto &p : d.polygon)
        {
            if (p.lon < b.min_lon)
                b.min_lon = p.lon;
            if (p.lat < b.min_lat)
                b.min_lat = p.lat;
            if (p.lon > b.max_lon)
                b.max_lon = p.lon;
            if (p.lat > b.max_lat)
                b.max_lat = p.lat;
        }
        return b;
    }

    // ─── computeIoU ───────────────────────────────────────────────
    float Stitcher::computeIoU(const BBox &a, const BBox &b)
    {
        // Intersection
        double ix0 = std::max(a.min_lon, b.min_lon);
        double iy0 = std::max(a.min_lat, b.min_lat);
        double ix1 = std::min(a.max_lon, b.max_lon);
        double iy1 = std::min(a.max_lat, b.max_lat);

        if (ix1 <= ix0 || iy1 <= iy0)
            return 0.0f;

        double inter = (ix1 - ix0) * (iy1 - iy0);
        double uni = a.area() + b.area() - inter;

        return (uni <= 0.0) ? 0.0f : (float)(inter / uni);
    }

    // ─── runNMS ───────────────────────────────────────────────────
    std::vector<GeoDetection> Stitcher::runNMS(
        const std::vector<GeoDetection> &input)
    {
        last_input_ = (int)input.size();

        if (input.empty())
        {
            last_output_ = 0;
            last_removed_ = 0;
            return {};
        }

        // Pre-compute bboxes 1 lần — tránh tính lại O(n²) lần
        std::vector<BBox> boxes(input.size());
        for (size_t i = 0; i < input.size(); i++)
            boxes[i] = toBBox(input[i]);

        // Sort indices by confidence descending
        std::vector<size_t> idx(input.size());
        for (size_t i = 0; i < idx.size(); i++)
            idx[i] = i;
        std::sort(idx.begin(), idx.end(), [&](size_t a, size_t b)
                  { return input[a].confidence > input[b].confidence; });

        std::vector<bool> suppressed(input.size(), false);
        std::vector<GeoDetection> result;
        result.reserve(input.size());

        for (size_t i = 0; i < idx.size(); i++)
        {
            size_t ci = idx[i];
            if (suppressed[ci])
                continue;

            result.push_back(input[ci]); // giữ lại detection này

            // Suppress các detection cùng class có IoU > threshold
            for (size_t j = i + 1; j < idx.size(); j++)
            {
                size_t cj = idx[j];
                if (suppressed[cj])
                    continue;

                // Chỉ so sánh cùng class — khác class không thể trùng
                if (input[ci].class_id != input[cj].class_id)
                    continue;

                float iou = computeIoU(boxes[ci], boxes[cj]);
                if (iou > iou_threshold_)
                    suppressed[cj] = true;
            }
        }

        last_output_ = (int)result.size();
        last_removed_ = last_input_ - last_output_;

        LOG_INFO("Stitcher",
                 "NMS complete: " + std::to_string(last_input_) + " in → " + std::to_string(last_output_) + " out (" + std::to_string(last_removed_) + " duplicates removed" + ", iou_thresh=" + std::to_string(iou_threshold_) + ")");

        return result;
    }

} // namespace rs