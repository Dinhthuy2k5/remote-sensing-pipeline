#pragma once
#include "common/types.hpp"
#include <vector>

namespace rs
{

    class Stitcher
    {
    public:
        // iou_threshold: 0.5 là chuẩn cho viễn thám
        explicit Stitcher(float iou_threshold = 0.5f);

        // Nhận ALL detections sau waitAll()
        // Trả về list đã dedup — gọi 1 lần duy nhất
        std::vector<GeoDetection> runNMS(
            const std::vector<GeoDetection> &input);

        int lastInputCount() const { return last_input_; }
        int lastOutputCount() const { return last_output_; }
        int lastRemovedCount() const { return last_removed_; }

    private:
        float iou_threshold_;
        int last_input_ = 0;
        int last_output_ = 0;
        int last_removed_ = 0;

        struct BBox
        {
            double min_lon, min_lat;
            double max_lon, max_lat;
            double area() const
            {
                return (max_lon - min_lon) * (max_lat - min_lat);
            }
        };

        static BBox toBBox(const GeoDetection &d);
        static float computeIoU(const BBox &a, const BBox &b);
    };

} // namespace rs