#include "inference/onnx_obb_ai.hpp"
#include "common/logger.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace rs
{
    namespace
    {
        constexpr int MODEL_W = 1024;
        constexpr int MODEL_H = 1024;
        constexpr int DOTA_CLASSES = 15;
    }

    OnnxObbAI::OnnxObbAI(Ort::Env &env,
                         const std::string &model_path,
                         float conf_thresh,
                         int intra_op_threads)
        : conf_thresh_(conf_thresh)
    {
        Ort::SessionOptions opts;
        opts.SetIntraOpNumThreads(intra_op_threads);
        opts.SetInterOpNumThreads(1);
        opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

        session_ = std::make_unique<Ort::Session>(env, model_path.c_str(), opts);

        size_t n_inputs = session_->GetInputCount();
        size_t n_outputs = session_->GetOutputCount();

        for (size_t i = 0; i < n_inputs; i++)
        {
            auto name = session_->GetInputNameAllocated(i, allocator_);
            input_names_str_.push_back(name.get());
        }
        for (size_t i = 0; i < n_outputs; i++)
        {
            auto name = session_->GetOutputNameAllocated(i, allocator_);
            output_names_str_.push_back(name.get());
            auto info = session_->GetOutputTypeInfo(i);
            auto shape = info.GetTensorTypeAndShapeInfo().GetShape();
            std::string shape_str;
            for (auto d : shape)
                shape_str += std::to_string(d) + "x";
            LOG_INFO("OnnxObbAI", "Output[" + std::to_string(i) + "] " +
                                     output_names_str_.back() + " shape=" + shape_str);
        }

        for (auto &s : input_names_str_)
            input_names_.push_back(s.c_str());
        for (auto &s : output_names_str_)
            output_names_.push_back(s.c_str());

        LOG_INFO("OnnxObbAI", "Model loaded: " + model_path);
    }

    ObbLetterboxInfo OnnxObbAI::computeLetterbox(int sw, int sh, int dw, int dh)
    {
        ObbLetterboxInfo info;
        info.scale = std::min((float)dw / sw, (float)dh / sh);
        info.new_w = (int)std::round(sw * info.scale);
        info.new_h = (int)std::round(sh * info.scale);
        info.pad_x = (dw - info.new_w) / 2;
        info.pad_y = (dh - info.new_h) / 2;
        return info;
    }

    void OnnxObbAI::resizeBilinearRgb(const TileData &tile,
                                      uint8_t *dst,
                                      int dst_w,
                                      int dst_h)
    {
        float sx = (float)tile.width / dst_w;
        float sy = (float)tile.height / dst_h;
        int bands = std::max(tile.band_count, 1);

        for (int y = 0; y < dst_h; y++)
        {
            float fy = y * sy;
            int y0 = std::min((int)fy, tile.height - 1);
            int y1 = std::min(y0 + 1, tile.height - 1);
            float dy = fy - y0;

            for (int x = 0; x < dst_w; x++)
            {
                float fx = x * sx;
                int x0 = std::min((int)fx, tile.width - 1);
                int x1 = std::min(x0 + 1, tile.width - 1);
                float dx = fx - x0;

                for (int c = 0; c < 3; c++)
                {
                    int band = bands >= 3 ? c : 0;
                    auto at = [&](int px, int py) -> float
                    {
                        return tile.pixels[(py * tile.width + px) * bands + band];
                    };

                    float v = at(x0, y0) * (1 - dx) * (1 - dy) +
                              at(x1, y0) * dx * (1 - dy) +
                              at(x0, y1) * (1 - dx) * dy +
                              at(x1, y1) * dx * dy;
                    dst[(y * dst_w + x) * 3 + c] = (uint8_t)std::clamp(v, 0.0f, 255.0f);
                }
            }
        }
    }

    std::vector<float> OnnxObbAI::preprocess(const TileData &tile,
                                             ObbLetterboxInfo &info) const
    {
        info = computeLetterbox(tile.width, tile.height, MODEL_W, MODEL_H);

        std::vector<uint8_t> resized((size_t)info.new_w * info.new_h * 3);
        resizeBilinearRgb(tile, resized.data(), info.new_w, info.new_h);

        std::vector<uint8_t> canvas((size_t)MODEL_W * MODEL_H * 3, 114);
        for (int y = 0; y < info.new_h; y++)
        {
            for (int x = 0; x < info.new_w; x++)
            {
                for (int c = 0; c < 3; c++)
                {
                    canvas[((y + info.pad_y) * MODEL_W + (x + info.pad_x)) * 3 + c] =
                        resized[(y * info.new_w + x) * 3 + c];
                }
            }
        }

        const int N = MODEL_W * MODEL_H;
        std::vector<float> tensor(3 * N);
        for (int i = 0; i < N; i++)
        {
            tensor[0 * N + i] = canvas[i * 3 + 0] / 255.0f;
            tensor[1 * N + i] = canvas[i * 3 + 1] / 255.0f;
            tensor[2 * N + i] = canvas[i * 3 + 2] / 255.0f;
        }
        return tensor;
    }

    std::vector<Detection> OnnxObbAI::infer(const TileData &tile)
    {
        if (!session_ || tile.pixels.empty())
            return {};

        ObbLetterboxInfo info;
        auto tensor_data = preprocess(tile, info);

        std::array<int64_t, 4> shape{1, 3, MODEL_H, MODEL_W};
        auto memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

        Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
            memory_info,
            tensor_data.data(),
            tensor_data.size(),
            shape.data(),
            shape.size());

        const char *output0_names[] = {output_names_[0]};
        auto outputs = session_->Run(
            Ort::RunOptions{nullptr},
            input_names_.data(),
            &input_tensor,
            1,
            output0_names,
            1);

        auto &out = outputs[0];
        auto out_shape = out.GetTensorTypeAndShapeInfo().GetShape();
        if (out_shape.size() != 3)
            throw std::runtime_error("Unexpected OBB output rank");

        int channels = (int)out_shape[1];
        int num_anchors = (int)out_shape[2];
        const float *data = out.GetTensorData<float>();

        return postprocess(data, channels, num_anchors, info, tile.width, tile.height);
    }

    std::vector<Detection> OnnxObbAI::postprocess(const float *data,
                                                  int channels,
                                                  int num_anchors,
                                                  const ObbLetterboxInfo &info,
                                                  int tile_w,
                                                  int tile_h) const
    {
        if (channels < 4 + DOTA_CLASSES + 1)
            throw std::runtime_error("Unexpected OBB output channels");

        std::vector<Detection> dets;
        for (int a = 0; a < num_anchors; a++)
        {
            float cx = data[0 * num_anchors + a];
            float cy = data[1 * num_anchors + a];
            float w = data[2 * num_anchors + a];
            float h = data[3 * num_anchors + a];

            float max_score = 0.0f;
            int class_id = 0;
            for (int c = 0; c < DOTA_CLASSES; c++)
            {
                float score = data[(4 + c) * num_anchors + a];
                if (score > max_score)
                {
                    max_score = score;
                    class_id = c;
                }
            }

            if (max_score < conf_thresh_)
                continue;

            float angle = data[(4 + DOTA_CLASSES) * num_anchors + a];
            float cs = std::cos(angle);
            float sn = std::sin(angle);
            float hx = w * 0.5f;
            float hy = h * 0.5f;

            float min_x = tile_w, min_y = tile_h, max_x = 0.0f, max_y = 0.0f;
            const float corners[4][2] = {
                {-hx, -hy}, {hx, -hy}, {hx, hy}, {-hx, hy}};

            for (const auto &corner : corners)
            {
                float mx = cx + corner[0] * cs - corner[1] * sn;
                float my = cy + corner[0] * sn + corner[1] * cs;
                float tx = (mx - info.pad_x) / info.scale;
                float ty = (my - info.pad_y) / info.scale;

                tx = std::clamp(tx, 0.0f, (float)tile_w);
                ty = std::clamp(ty, 0.0f, (float)tile_h);
                min_x = std::min(min_x, tx);
                min_y = std::min(min_y, ty);
                max_x = std::max(max_x, tx);
                max_y = std::max(max_y, ty);
            }

            float bw = max_x - min_x;
            float bh = max_y - min_y;
            if (bw <= 0.0f || bh <= 0.0f)
                continue;

            Detection det;
            det.bbox = {min_x, min_y, bw, bh};
            det.class_id = class_id;
            det.confidence = max_score;
            dets.push_back(det);
        }

        return tileNMS(dets);
    }

    std::vector<Detection> OnnxObbAI::tileNMS(const std::vector<Detection> &dets,
                                              float iou_thresh)
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

    float OnnxObbAI::iou(const BoundingBox &a, const BoundingBox &b)
    {
        float ix0 = std::max(a.x, b.x);
        float iy0 = std::max(a.y, b.y);
        float ix1 = std::min(a.x + a.width, b.x + b.width);
        float iy1 = std::min(a.y + a.height, b.y + b.height);
        if (ix1 <= ix0 || iy1 <= iy0)
            return 0.0f;
        float inter = (ix1 - ix0) * (iy1 - iy0);
        float uni = a.width * a.height + b.width * b.height - inter;
        return uni <= 0.0f ? 0.0f : inter / uni;
    }

} // namespace rs
