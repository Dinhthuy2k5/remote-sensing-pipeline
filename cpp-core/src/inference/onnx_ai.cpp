#include "inference/onnx_ai.hpp"
#include "common/logger.hpp"
#include <algorithm>
#include <cstring>
#include <cmath>
#include <stdexcept>
#include <array>

namespace rs
{

    // ─── Constructor ──────────────────────────────────────────────
    OnnxAI::OnnxAI(Ort::Env &env,
                   const std::string &model_path,
                   float conf_thresh,
                   int intra_op_threads)
        : conf_thresh_(conf_thresh)
    {
        Ort::SessionOptions opts;
        opts.SetIntraOpNumThreads(intra_op_threads);
        opts.SetInterOpNumThreads(1);
        opts.SetGraphOptimizationLevel(
            GraphOptimizationLevel::ORT_ENABLE_ALL);

        session_ = std::make_unique<Ort::Session>(
            env, model_path.c_str(), opts);

        // Log input/output info
        size_t n_inputs = session_->GetInputCount();
        size_t n_outputs = session_->GetOutputCount();

        for (size_t i = 0; i < n_inputs; i++)
        {
            auto name = session_->GetInputNameAllocated(i, allocator_);
            input_names_str_.push_back(name.get());
            auto info = session_->GetInputTypeInfo(i);
            auto shape = info.GetTensorTypeAndShapeInfo().GetShape();
            std::string shape_str;
            for (auto d : shape)
                shape_str += std::to_string(d) + "×";
            LOG_INFO("OnnxAI", "Input[" + std::to_string(i) + "] " + input_names_str_.back() + " shape=" + shape_str);
        }
        for (size_t i = 0; i < n_outputs; i++)
        {
            auto name = session_->GetOutputNameAllocated(i, allocator_);
            output_names_str_.push_back(name.get());
            auto info = session_->GetOutputTypeInfo(i);
            auto shape = info.GetTensorTypeAndShapeInfo().GetShape();
            std::string shape_str;
            for (auto d : shape)
                shape_str += std::to_string(d) + "×";
            LOG_INFO("OnnxAI", "Output[" + std::to_string(i) + "] " + output_names_str_.back() + " shape=" + shape_str);
        }

        for (auto &s : input_names_str_)
            input_names_.push_back(s.c_str());
        for (auto &s : output_names_str_)
            output_names_.push_back(s.c_str());

        LOG_INFO("OnnxAI", "Model loaded: " + model_path + " intra_op_threads=" + std::to_string(intra_op_threads));
    }

    // ─── computeLetterbox ─────────────────────────────────────────
    LetterboxInfo OnnxAI::computeLetterbox(int sw, int sh,
                                           int dw, int dh)
    {
        LetterboxInfo info;
        info.scale = std::min((float)dw / sw, (float)dh / sh);
        info.new_w = (int)std::round(sw * info.scale);
        info.new_h = (int)std::round(sh * info.scale);
        info.pad_x = (dw - info.new_w) / 2;
        info.pad_y = (dh - info.new_h) / 2;
        return info;
    }

    // ─── resizeBilinear ───────────────────────────────────────────
    void OnnxAI::resizeBilinear(const uint8_t *src, int sw, int sh,
                                uint8_t *dst, int dw, int dh)
    {
        float sx = (float)sw / dw;
        float sy = (float)sh / dh;
        for (int y = 0; y < dh; y++)
        {
            float fy = y * sy;
            int y0 = std::min((int)fy, sh - 1);
            int y1 = std::min(y0 + 1, sh - 1);
            float dy = fy - y0;
            for (int x = 0; x < dw; x++)
            {
                float fx = x * sx;
                int x0 = std::min((int)fx, sw - 1);
                int x1 = std::min(x0 + 1, sw - 1);
                float dx = fx - x0;
                float v = src[y0 * sw + x0] * (1 - dx) * (1 - dy) + src[y0 * sw + x1] * dx * (1 - dy) + src[y1 * sw + x0] * (1 - dx) * dy + src[y1 * sw + x1] * dx * dy;
                dst[y * dw + x] = (uint8_t)std::min(255.0f, v);
            }
        }
    }

    // ─── preprocess ───────────────────────────────────────────────
    std::vector<float> OnnxAI::preprocess(const TileData &tile,
                                          LetterboxInfo &info) const
    {
        const int MODEL_W = 640, MODEL_H = 640;
        info = computeLetterbox(tile.width, tile.height, MODEL_W, MODEL_H);

        // Resize tile → new_w × new_h (grayscale)
        std::vector<uint8_t> resized(info.new_w * info.new_h);
        resizeBilinear(tile.pixels.data(), tile.width, tile.height,
                       resized.data(), info.new_w, info.new_h);

        // Tạo canvas 640×640, fill 114
        std::vector<uint8_t> canvas(MODEL_W * MODEL_H, 114);
        for (int y = 0; y < info.new_h; y++)
        {
            for (int x = 0; x < info.new_w; x++)
            {
                canvas[(y + info.pad_y) * MODEL_W + (x + info.pad_x)] = resized[y * info.new_w + x];
            }
        }

        // HWC gray → NCHW float (duplicate 3 channels)
        // Layout: [C0:all pixels][C1:all pixels][C2:all pixels]
        const int N = MODEL_W * MODEL_H;
        std::vector<float> tensor(3 * N);
        for (int i = 0; i < N; i++)
        {
            float v = canvas[i] / 255.0f;
            tensor[0 * N + i] = v; // R
            tensor[1 * N + i] = v; // G
            tensor[2 * N + i] = v; // B
        }
        return tensor;
    }

    // ─── infer ────────────────────────────────────────────────────
    std::vector<Detection> OnnxAI::infer(const TileData &tile)
    {
        if (!session_ || tile.pixels.empty())
            return {};

        LetterboxInfo info;
        auto tensor_data = preprocess(tile, info);

        // Tạo input tensor
        std::array<int64_t, 4> shape{1, 3, 640, 640};
        auto memory_info = Ort::MemoryInfo::CreateCpu(
            OrtArenaAllocator, OrtMemTypeDefault);

        Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
            memory_info,
            tensor_data.data(), tensor_data.size(),
            shape.data(), shape.size());

        // Run inference
        const char *output0_names[] = {output_names_[0]};
        auto outputs = session_->Run(
            Ort::RunOptions{nullptr},
            input_names_.data(),
            &input_tensor,
            1,
            output0_names,
            1); // chỉ lấy output0

        // Parse output0: shape 1×116×8400
        auto &out = outputs[0];
        auto out_info = out.GetTensorTypeAndShapeInfo();
        auto out_shape = out_info.GetShape(); // [1, 116, 8400]

        int num_anchors = (int)out_shape[2]; // 8400
        const float *data = out.GetTensorData<float>();

        // Log raw bbox đầu tiên để verify normalized hay pixel
        static bool logged = false;
        if (!logged)
        {
            logged = true;
            // output layout: [1][116][8400] → data[ch * 8400 + anchor]
            float cx = data[0 * num_anchors + 0];
            float cy = data[1 * num_anchors + 0];
            float w = data[2 * num_anchors + 0];
            float h = data[3 * num_anchors + 0];
            LOG_INFO("OnnxAI",
                     "Raw bbox[0]: cx=" + std::to_string(cx) + " cy=" + std::to_string(cy) + " w=" + std::to_string(w) + " h=" + std::to_string(h) + " (if ~320 = pixel coords, if ~0.5 = normalized)");
        }

        return postprocess(data, num_anchors, info,
                           tile.width, tile.height);
    }

    // ─── postprocess ──────────────────────────────────────────────
    std::vector<Detection> OnnxAI::postprocess(
        const float *data,
        int num_anchors,
        const LetterboxInfo &info,
        int tile_w,
        int tile_h) const
    {
        const int NUM_CLASSES = 80;
        std::vector<Detection> dets;

        for (int a = 0; a < num_anchors; a++)
        {
            // output layout: data[channel * num_anchors + anchor]
            float cx = data[0 * num_anchors + a];
            float cy = data[1 * num_anchors + a];
            float w = data[2 * num_anchors + a];
            float h = data[3 * num_anchors + a];

            // Tìm max class score
            float max_score = 0.0f;
            int class_id = 0;
            for (int c = 0; c < NUM_CLASSES; c++)
            {
                float s = data[(4 + c) * num_anchors + a];
                if (s > max_score)
                {
                    max_score = s;
                    class_id = c;
                }
            }

            if (max_score < conf_thresh_)
                continue;

            // Inverse letterbox: model pixel coords → tile pixel coords
            float x0 = (cx - w * 0.5f - info.pad_x) / info.scale;
            float y0 = (cy - h * 0.5f - info.pad_y) / info.scale;
            float x1 = (cx + w * 0.5f - info.pad_x) / info.scale;
            float y1 = (cy + h * 0.5f - info.pad_y) / info.scale;

            // Clamp
            x0 = std::max(0.0f, std::min(x0, (float)tile_w));
            y0 = std::max(0.0f, std::min(y0, (float)tile_h));
            x1 = std::max(0.0f, std::min(x1, (float)tile_w));
            y1 = std::max(0.0f, std::min(y1, (float)tile_h));

            float bw = x1 - x0;
            float bh = y1 - y0;
            if (bw <= 0 || bh <= 0)
                continue;

            Detection det;
            det.bbox = {x0, y0, bw, bh};
            det.class_id = class_id;
            det.confidence = max_score;
            dets.push_back(det);
        }

        return tileNMS(dets);
    }

    // ─── tileNMS ──────────────────────────────────────────────────
    std::vector<Detection> OnnxAI::tileNMS(
        const std::vector<Detection> &dets, float iou_thresh)
    {
        if (dets.empty())
            return {};

        std::vector<size_t> idx(dets.size());
        for (size_t i = 0; i < idx.size(); i++)
            idx[i] = i;
        std::sort(idx.begin(), idx.end(), [&](size_t a, size_t b)
                  { return dets[a].confidence > dets[b].confidence; });

        std::vector<bool> suppressed(dets.size(), false);
        std::vector<Detection> result;

        for (size_t i = 0; i < idx.size(); i++)
        {
            if (suppressed[idx[i]])
                continue;
            result.push_back(dets[idx[i]]);
            for (size_t j = i + 1; j < idx.size(); j++)
            {
                if (suppressed[idx[j]])
                    continue;
                if (dets[idx[i]].class_id != dets[idx[j]].class_id)
                    continue;
                if (iou(dets[idx[i]].bbox, dets[idx[j]].bbox) > iou_thresh)
                    suppressed[idx[j]] = true;
            }
        }
        return result;
    }

    // ─── iou ──────────────────────────────────────────────────────
    float OnnxAI::iou(const BoundingBox &a, const BoundingBox &b)
    {
        float ix0 = std::max(a.x, b.x);
        float iy0 = std::max(a.y, b.y);
        float ix1 = std::min(a.x + a.width, b.x + b.width);
        float iy1 = std::min(a.y + a.height, b.y + b.height);
        if (ix1 <= ix0 || iy1 <= iy0)
            return 0.0f;
        float inter = (ix1 - ix0) * (iy1 - iy0);
        float uni = a.width * a.height + b.width * b.height - inter;
        return (uni <= 0) ? 0.0f : inter / uni;
    }

} // namespace rs
