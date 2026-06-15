#include "inference/onnx_segformer_ai.hpp"
#include "common/logger.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace rs
{
    namespace
    {
        constexpr int MODEL_W = 512;
        constexpr int MODEL_H = 512;
        constexpr int LOVE_DA_CLASSES = 8;
        constexpr int IGNORE_CLASS = 0;
        constexpr int BACKGROUND_CLASS = 1;
        constexpr int LOCAL_BOX_CELLS = 4;
        constexpr int MIN_LOCAL_PIXELS = 2;
        constexpr int MAX_DETECTIONS_PER_TILE = 256;
        constexpr float MEAN[3] = {0.485f, 0.456f, 0.406f};
        constexpr float STD[3] = {0.229f, 0.224f, 0.225f};

        struct GridPoint
        {
            int x;
            int y;
        };

        struct GridEdge
        {
            GridPoint a;
            GridPoint b;
        };

        bool samePoint(const GridPoint &a, const GridPoint &b)
        {
            return a.x == b.x && a.y == b.y;
        }

        double polygonArea(const std::vector<GridPoint> &ring)
        {
            if (ring.size() < 3)
                return 0.0;

            double area = 0.0;
            for (size_t i = 0; i < ring.size(); i++)
            {
                const auto &a = ring[i];
                const auto &b = ring[(i + 1) % ring.size()];
                area += (double)a.x * b.y - (double)b.x * a.y;
            }
            return std::abs(area) * 0.5;
        }

        std::vector<GridPoint> largestBoundaryRing(std::vector<GridEdge> edges)
        {
            std::vector<GridPoint> best;
            double best_area = 0.0;

            while (!edges.empty())
            {
                GridEdge first = edges.back();
                edges.pop_back();

                std::vector<GridPoint> ring;
                ring.push_back(first.a);
                GridPoint current = first.b;

                int guard = 0;
                while (!samePoint(current, ring.front()) && guard++ < 4096)
                {
                    ring.push_back(current);

                    auto it = std::find_if(edges.begin(), edges.end(),
                                           [&](const GridEdge &e)
                                           { return samePoint(e.a, current); });
                    if (it == edges.end())
                    {
                        it = std::find_if(edges.begin(), edges.end(),
                                          [&](const GridEdge &e)
                                          { return samePoint(e.b, current); });
                        if (it == edges.end())
                            break;
                        current = it->a;
                    }
                    else
                    {
                        current = it->b;
                    }
                    edges.erase(it);
                }

                if (ring.size() >= 4)
                {
                    double area = polygonArea(ring);
                    if (area > best_area)
                    {
                        best_area = area;
                        best = ring;
                    }
                }
            }

            return best;
        }
    }

    OnnxSegFormerAI::OnnxSegFormerAI(Ort::Env &env,
                                     const std::string &model_path,
                                     float conf_thresh,
                                     int intra_op_threads)
        : conf_thresh_(conf_thresh)
    {
        Ort::SessionOptions opts;
        opts.SetIntraOpNumThreads(intra_op_threads);
        opts.SetInterOpNumThreads(1);
        opts.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
        opts.DisableCpuMemArena();
        opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

        session_ = std::make_unique<Ort::Session>(env, model_path.c_str(), opts);

        for (size_t i = 0; i < session_->GetInputCount(); i++)
        {
            auto name = session_->GetInputNameAllocated(i, allocator_);
            input_names_str_.push_back(name.get());
        }
        for (size_t i = 0; i < session_->GetOutputCount(); i++)
        {
            auto name = session_->GetOutputNameAllocated(i, allocator_);
            output_names_str_.push_back(name.get());
            auto shape = session_->GetOutputTypeInfo(i).GetTensorTypeAndShapeInfo().GetShape();
            std::string shape_str;
            for (auto d : shape)
                shape_str += std::to_string(d) + "x";
            LOG_INFO("OnnxSegFormerAI", "Output[" + std::to_string(i) + "] " +
                                             output_names_str_.back() + " shape=" + shape_str);
        }

        for (auto &s : input_names_str_)
            input_names_.push_back(s.c_str());
        for (auto &s : output_names_str_)
            output_names_.push_back(s.c_str());

        LOG_INFO("OnnxSegFormerAI", "Model loaded: " + model_path);
    }

    SegFormerLetterboxInfo OnnxSegFormerAI::computeLetterbox(int sw, int sh, int dw, int dh)
    {
        SegFormerLetterboxInfo info;
        info.scale = std::min((float)dw / sw, (float)dh / sh);
        info.new_w = (int)std::round(sw * info.scale);
        info.new_h = (int)std::round(sh * info.scale);
        info.pad_x = (dw - info.new_w) / 2;
        info.pad_y = (dh - info.new_h) / 2;
        return info;
    }

    void OnnxSegFormerAI::resizeBilinearRgb(const TileData &tile,
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

    std::vector<float> OnnxSegFormerAI::preprocess(const TileData &tile,
                                                   SegFormerLetterboxInfo &info) const
    {
        info = computeLetterbox(tile.width, tile.height, MODEL_W, MODEL_H);

        std::vector<uint8_t> resized((size_t)info.new_w * info.new_h * 3);
        resizeBilinearRgb(tile, resized.data(), info.new_w, info.new_h);

        std::vector<uint8_t> canvas((size_t)MODEL_W * MODEL_H * 3, 0);
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
            for (int c = 0; c < 3; c++)
            {
                float v = canvas[i * 3 + c] / 255.0f;
                tensor[c * N + i] = (v - MEAN[c]) / STD[c];
            }
        }
        return tensor;
    }

    std::vector<Detection> OnnxSegFormerAI::infer(const TileData &tile)
    {
        if (!session_ || tile.pixels.empty())
            return {};

        SegFormerLetterboxInfo info;
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
        if (out_shape.size() != 4)
            throw std::runtime_error("Unexpected SegFormer output rank");

        int classes = (int)out_shape[1];
        int out_h = (int)out_shape[2];
        int out_w = (int)out_shape[3];
        const float *logits = out.GetTensorData<float>();

        return postprocess(logits, classes, out_h, out_w, info, tile.width, tile.height);
    }

    std::vector<Detection> OnnxSegFormerAI::postprocess(const float *logits,
                                                        int classes,
                                                        int out_h,
                                                        int out_w,
                                                        const SegFormerLetterboxInfo &info,
                                                        int tile_w,
                                                        int tile_h) const
    {
        if (classes != LOVE_DA_CLASSES)
            throw std::runtime_error("Unexpected SegFormer class count");

        const int cells = out_w * out_h;
        std::vector<int> labels(cells, BACKGROUND_CLASS);
        std::vector<float> confidences(cells, 0.0f);

        for (int i = 0; i < cells; i++)
        {
            float max_logit = logits[i];
            for (int c = 1; c < classes; c++)
                max_logit = std::max(max_logit, logits[c * cells + i]);

            float sum = 0.0f;
            int best_class = 0;
            float best_prob = 0.0f;
            for (int c = 0; c < classes; c++)
            {
                float p = std::exp(logits[c * cells + i] - max_logit);
                sum += p;
                if (p > best_prob)
                {
                    best_prob = p;
                    best_class = c;
                }
            }

            labels[i] = best_class;
            confidences[i] = sum > 0.0f ? best_prob / sum : 0.0f;
        }

        std::vector<Detection> dets;
        const float cell_w = (float)MODEL_W / out_w;
        const float cell_h = (float)MODEL_H / out_h;

        for (int by = 0; by < out_h; by += LOCAL_BOX_CELLS)
        {
            for (int bx = 0; bx < out_w; bx += LOCAL_BOX_CELLS)
            {
                int x_end = std::min(bx + LOCAL_BOX_CELLS, out_w);
                int y_end = std::min(by + LOCAL_BOX_CELLS, out_h);

                int class_votes[LOVE_DA_CLASSES] = {};
                float class_conf[LOVE_DA_CLASSES] = {};

                for (int y = by; y < y_end; y++)
                {
                    for (int x = bx; x < x_end; x++)
                    {
                        int idx = y * out_w + x;
                        int cls = labels[idx];
                        if (cls == IGNORE_CLASS || cls == BACKGROUND_CLASS ||
                            confidences[idx] < conf_thresh_)
                        {
                            continue;
                        }
                        class_votes[cls]++;
                        class_conf[cls] += confidences[idx];
                    }
                }

                int best_class = BACKGROUND_CLASS;
                int best_votes = 0;
                for (int cls = 0; cls < LOVE_DA_CLASSES; cls++)
                {
                    if (class_votes[cls] > best_votes)
                    {
                        best_class = cls;
                        best_votes = class_votes[cls];
                    }
                }

                if (best_class == BACKGROUND_CLASS || best_votes < MIN_LOCAL_PIXELS)
                    continue;

                float conf_sum = 0.0f;
                int count = 0;
                bool selected[LOCAL_BOX_CELLS][LOCAL_BOX_CELLS] = {};

                for (int y = by; y < y_end; y++)
                {
                    for (int x = bx; x < x_end; x++)
                    {
                        int idx = y * out_w + x;
                        if (labels[idx] != best_class || confidences[idx] < conf_thresh_)
                            continue;

                        selected[y - by][x - bx] = true;
                        conf_sum += confidences[idx];
                        count++;
                    }
                }

                if (count < MIN_LOCAL_PIXELS)
                    continue;

                float avg_conf = conf_sum / count;

                auto isSelected = [&](int gx, int gy)
                {
                    if (gx < bx || gx >= x_end || gy < by || gy >= y_end)
                        return false;
                    return selected[gy - by][gx - bx];
                };

                std::vector<GridEdge> edges;
                edges.reserve(count * 4);
                for (int y = by; y < y_end; y++)
                {
                    for (int x = bx; x < x_end; x++)
                    {
                        if (!isSelected(x, y))
                            continue;

                        if (!isSelected(x, y - 1))
                            edges.push_back({{x, y}, {x + 1, y}});
                        if (!isSelected(x + 1, y))
                            edges.push_back({{x + 1, y}, {x + 1, y + 1}});
                        if (!isSelected(x, y + 1))
                            edges.push_back({{x + 1, y + 1}, {x, y + 1}});
                        if (!isSelected(x - 1, y))
                            edges.push_back({{x, y + 1}, {x, y}});
                    }
                }

                std::vector<GridPoint> ring = largestBoundaryRing(std::move(edges));
                if (ring.size() < 4)
                    continue;

                Detection det;
                det.class_id = best_class;
                det.confidence = avg_conf;

                float min_tx = (float)tile_w;
                float min_ty = (float)tile_h;
                float max_tx = 0.0f;
                float max_ty = 0.0f;
                det.polygon.reserve(ring.size());

                for (const auto &p : ring)
                {
                    float mx = p.x * cell_w;
                    float my = p.y * cell_h;
                    float tx = (mx - info.pad_x) / info.scale;
                    float ty = (my - info.pad_y) / info.scale;

                    tx = std::clamp(tx, 0.0f, (float)tile_w);
                    ty = std::clamp(ty, 0.0f, (float)tile_h);

                    min_tx = std::min(min_tx, tx);
                    min_ty = std::min(min_ty, ty);
                    max_tx = std::max(max_tx, tx);
                    max_ty = std::max(max_ty, ty);
                    det.polygon.push_back({tx, ty});
                }

                if (max_tx <= min_tx || max_ty <= min_ty || det.polygon.size() < 4)
                    continue;

                det.bbox = {min_tx, min_ty, max_tx - min_tx, max_ty - min_ty};
                dets.push_back(det);
            }
        }

        std::sort(dets.begin(), dets.end(), [](const Detection &a, const Detection &b)
                  { return a.confidence > b.confidence; });
        if ((int)dets.size() > MAX_DETECTIONS_PER_TILE)
            dets.resize(MAX_DETECTIONS_PER_TILE);

        return dets;
    }

} // namespace rs
