#pragma once

#include "inference/ai_interface.hpp"
#include <onnxruntime_cxx_api.h>
#include <memory>
#include <string>
#include <vector>

namespace rs
{

    struct SegFormerLetterboxInfo
    {
        float scale;
        int pad_x;
        int pad_y;
        int new_w;
        int new_h;
    };

    class OnnxSegFormerAI : public AIInterface
    {
    public:
        OnnxSegFormerAI(Ort::Env &env,
                        const std::string &model_path,
                        float conf_thresh = 0.45f,
                        int intra_op_threads = 1);

        ~OnnxSegFormerAI() override = default;

        std::vector<Detection> infer(const TileData &tile) override;
        std::string name() const override { return "OnnxSegFormerAI(LoveDA)"; }

    private:
        std::unique_ptr<Ort::Session> session_;
        Ort::AllocatorWithDefaultOptions allocator_;
        float conf_thresh_;

        std::vector<std::string> input_names_str_;
        std::vector<std::string> output_names_str_;
        std::vector<const char *> input_names_;
        std::vector<const char *> output_names_;

        std::vector<float> preprocess(const TileData &tile,
                                      SegFormerLetterboxInfo &info) const;

        static SegFormerLetterboxInfo computeLetterbox(int src_w, int src_h,
                                                       int dst_w, int dst_h);
        static void resizeBilinearRgb(const TileData &tile,
                                      uint8_t *dst,
                                      int dst_w,
                                      int dst_h);

        std::vector<Detection> postprocess(const float *logits,
                                           int classes,
                                           int out_h,
                                           int out_w,
                                           const SegFormerLetterboxInfo &info,
                                           int tile_w,
                                           int tile_h) const;
    };

} // namespace rs
