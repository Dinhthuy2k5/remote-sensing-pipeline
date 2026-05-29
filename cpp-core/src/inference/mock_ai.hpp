#pragma once
#include "inference/ai_interface.hpp"

namespace rs
{

    class MockAI : public AIInterface
    {
    public:
        // num_detections_per_tile: số detection giả mỗi tile
        // seed: để kết quả reproducible khi test
        explicit MockAI(int num_detections_per_tile = 3, unsigned int seed = 42);

        std::vector<Detection> infer(const TileData &tile) override;
        std::string name() const override { return "MockAI"; }

    private:
        int num_detections_;
        unsigned int seed_;
    };

} // namespace rs