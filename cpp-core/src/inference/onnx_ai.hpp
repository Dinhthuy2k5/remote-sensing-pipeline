#pragma once
#include "inference/ai_interface.hpp"
#include <onnxruntime_cxx_api.h>
#include <string>
#include <vector>
#include <memory>

namespace rs
{

    struct LetterboxInfo
    {
        float scale;
        int pad_x;
        int pad_y;
        int new_w;
        int new_h;
    };

    class OnnxAI : public AIInterface
    {
    public:
        OnnxAI(Ort::Env &env,
               const std::string &model_path,
               float conf_thresh = 0.5f,
               int intra_op_threads = 1);

        ~OnnxAI() override = default;

        std::vector<Detection> infer(const TileData &tile) override;
        std::string name() const override { return "OnnxAI(yolov8n-seg)"; }

    private:
        std::unique_ptr<Ort::Session> session_;
        Ort::AllocatorWithDefaultOptions allocator_;
        float conf_thresh_;

        // Input/output names (phải giữ alive trong suốt session)
        std::vector<std::string> input_names_str_;
        std::vector<std::string> output_names_str_;
        std::vector<const char *> input_names_;
        std::vector<const char *> output_names_;

        // Preprocessing
        std::vector<float> preprocess(const TileData &tile,
                                      LetterboxInfo &info) const;

        // Letterbox resize
        static LetterboxInfo computeLetterbox(int src_w, int src_h,
                                              int dst_w, int dst_h);

        // Bilinear resize đơn giản
        static void resizeBilinear(const uint8_t *src, int sw, int sh,
                                   uint8_t *dst, int dw, int dh);

        // Postprocessing
        std::vector<Detection> postprocess(
            const float *output_data,
            int num_anchors,
            const LetterboxInfo &info,
            int tile_w,
            int tile_h) const;

        // Tile-level NMS
        static std::vector<Detection> tileNMS(
            const std::vector<Detection> &dets,
            float iou_thresh = 0.45f);

        static float iou(const BoundingBox &a, const BoundingBox &b);
    };

} // namespace rs