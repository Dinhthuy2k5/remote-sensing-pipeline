#pragma once
#include "common/types.hpp"
#include <vector>

namespace rs
{

    // Abstract interface — MockAI và ONNX đều implement cái này
    // Tuần 4 chỉ cần swap implementation, ThreadPool không đổi
    class AIInterface
    {
    public:
        virtual ~AIInterface() = default;

        // Nhận 1 tile, trả về list detection trong tile đó
        virtual std::vector<Detection> infer(const TileData &tile) = 0;

        virtual std::string name() const = 0;
    };

} // namespace rs