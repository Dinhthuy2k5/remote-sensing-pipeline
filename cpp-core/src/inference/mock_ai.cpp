#include "inference/mock_ai.hpp"
#include "common/logger.hpp"
#include <cstdlib>

namespace rs
{

    MockAI::MockAI(int num_detections_per_tile, unsigned int seed)
        : num_detections_(num_detections_per_tile), seed_(seed)
    {
    }

    std::vector<Detection> MockAI::infer(const TileData &tile)
    {
        std::vector<Detection> results;
        results.reserve(num_detections_);

        // Seed theo tile_index để mỗi tile có kết quả khác nhau
        // nhưng reproducible (chạy lại cùng kết quả)
        unsigned int local_seed = seed_ + (unsigned int)tile.tile_index;

        for (int i = 0; i < num_detections_; i++)
        {
            Detection det;

            // Tạo bbox ngẫu nhiên nằm trong tile
            // rand_r thread-safe hơn rand()
            float rx = (float)(rand_r(&local_seed) % 100) / 100.0f;
            float ry = (float)(rand_r(&local_seed) % 100) / 100.0f;
            float rw = (float)(rand_r(&local_seed) % 30 + 10) / 100.0f;
            float rh = (float)(rand_r(&local_seed) % 30 + 10) / 100.0f;

            // Clamp để bbox không vượt ra ngoài tile
            float max_x = (float)(tile.width - 1);
            float max_y = (float)(tile.height - 1);

            det.bbox.x = rx * max_x;
            det.bbox.y = ry * max_y;
            det.bbox.width = std::min(rw * max_x, max_x - det.bbox.x);
            det.bbox.height = std::min(rh * max_y, max_y - det.bbox.y);

            // class_id: 0=vegetation, 1=water, 2=building, 3=road
            det.class_id = rand_r(&local_seed) % 4;
            det.confidence = 0.5f + (float)(rand_r(&local_seed) % 50) / 100.0f;

            results.push_back(det);
        }

        LOG_DEBUG("MockAI",
                  "tile_index=" + std::to_string(tile.tile_index) + " → " + std::to_string(results.size()) + " detections");

        return results;
    }

} // namespace rs