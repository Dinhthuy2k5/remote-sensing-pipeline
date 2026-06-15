#pragma once

#include "inference/ai_interface.hpp"
#include <onnxruntime_cxx_api.h>
#include <memory>
#include <string>
#include <vector>

namespace rs
{

    struct ObbLetterboxInfo
    {
        float scale;
        int pad_x;
        int pad_y;
        int new_w;
        int new_h;
    };

    class OnnxObbAI : public AIInterface
    {
    public:
        OnnxObbAI(Ort::Env &env,
                  const std::string &model_path,
                  float conf_thresh = 0.25f,
                  int intra_op_threads = 1);

        ~OnnxObbAI() override = default;

        std::vector<Detection> infer(const TileData &tile) override;
        std::string name() const override { return "OnnxObbAI(yolo11n-obb-dota)"; }

    private:
        std::unique_ptr<Ort::Session> session_;
        Ort::AllocatorWithDefaultOptions allocator_;
        float conf_thresh_;

        std::vector<std::string> input_names_str_;
        std::vector<std::string> output_names_str_;
        std::vector<const char *> input_names_;
        std::vector<const char *> output_names_;

        std::vector<float> preprocess(const TileData &tile,
                                      ObbLetterboxInfo &info) const;

        static ObbLetterboxInfo computeLetterbox(int src_w, int src_h,
                                                 int dst_w, int dst_h);
        static void resizeBilinearRgb(const TileData &tile,
                                      uint8_t *dst,
                                      int dst_w,
                                      int dst_h);

        std::vector<Detection> postprocess(const float *data,
                                           int channels,
                                           int num_anchors,
                                           const ObbLetterboxInfo &info,
                                           int tile_w,
                                           int tile_h) const;

        static std::vector<Detection> tileNMS(const std::vector<Detection> &dets,
                                              float iou_thresh = 0.45f);
        static float iou(const BoundingBox &a, const BoundingBox &b);
    };

} // namespace rs
