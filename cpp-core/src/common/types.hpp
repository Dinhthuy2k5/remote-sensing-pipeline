#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace rs
{

    // ─── Geometry ─────────────────────────────────────────────────
    struct BoundingBox
    //     Định nghĩa vị trí của vật thể mà AI tìm thấy.
    // Lưu ý: x, y ở đây là tọa độ pixel nằm trong mảnh cắt (tile) đó, chứ không phải tọa độ của bức ảnh gốc khổng lồ.
    {
        float x, y; // pixel offset trong tile
        float width, height;
    };

    // ─── AI Output ────────────────────────────────────────────────
    struct Detection
    // Kết quả nguyên thủy trả về từ module Mock AI (hoặc ONNX sau này).
    // Nó bao gồm hộp giới hạn (BoundingBox), loại đối tượng AI nhận diện được (class_id), và độ tin cậy của AI (confidence).
    {
        BoundingBox bbox;
        int class_id;
        float confidence;
    };

    // ─── Tile ─────────────────────────────────────────────────────
    struct TileData
    {
        // Vị trí trong grid
        int tile_row;
        int tile_col;
        int tile_index; // row * grid_cols + col, dùng để sort kết quả

        // Vị trí trong ảnh gốc (pixel)
        int pixel_x_offset;
        int pixel_y_offset;

        // Kích thước thực tế (có thể nhỏ hơn tile_size ở biên)
        int width;
        int height;
        int band_count;

        // Pixel buffer — interleaved HWC format
        // pixels[y * width * band_count + x * band_count + b]
        std::vector<uint8_t> pixels;

        // Session context
        int64_t session_id;

        // Helper: tính size kỳ vọng của buffer
        size_t expectedBufferSize() const
        {
            return (size_t)width * height * band_count;
        }

        bool isValid() const
        {
            return width > 0 && height > 0 && band_count > 0 && pixels.size() == expectedBufferSize();
        }
    };

    // ─── Geo ──────────────────────────────────────────────────────
    struct GeoPoint
    // Lưu tọa độ thực tế lat (Vĩ độ) và lon (Kinh độ) kiểu double để đảm bảo độ chính xác (không bị lệch mét nào ngoài đời thực).
    {
        double lat;
        double lon;
    };

    struct GeoDetection
    // Bản nâng cấp của Detection. Nó đã chuyển đổi BoundingBox (pixel) thành một mảng gồm 4 điểm GeoPoint
    //  (tạo thành một hình đa giác - Polygon). Cấu trúc này đã sẵn sàng 100% để chuyển thành định dạng GeoJSON nạp vào PostGIS.
    {
        std::vector<GeoPoint> polygon; // 4 góc bbox → tọa độ thực
        int class_id;
        float confidence;
        int64_t session_id;
        int tile_index; // để trace về tile gốc
    };

    // ─── Session ──────────────────────────────────────────────────
    enum class SessionStatus
    // Máy trạng thái (State Machine) của bạn. Nó định nghĩa rõ ràng vòng đời của hệ thống.
    {
        IDLE,
        LOADING,
        TILING,
        PROCESSING,
        STITCHING,
        SAVING,
        DONE,
        ERROR,
        RECOVERING
    };

    struct SessionInfo
    // "Bảng điều khiển" tổng hợp trạng thái hiện tại. Cấu trúc này dùng để trả về cho Frontend qua API GET /status, giúp giao diện biết tiến độ cắt ảnh (tile_done / tile_total) để vẽ thanh tiến trình (progress bar).
    {
        int64_t id;
        std::string filename;
        SessionStatus status;
        int tile_total;
        int tile_done;
        std::vector<GeoPoint> footprint;
    };

} // namespace rs
