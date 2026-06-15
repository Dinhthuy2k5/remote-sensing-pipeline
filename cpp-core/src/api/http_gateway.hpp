#pragma once
// Là lính gác cổng. Nhờ có nó, dù file này bị include ở 10 file .cpp khác nhau,
// trình biên dịch cũng chỉ nạp nó đúng 1 lần, giúp tránh lỗi trùng lặp định nghĩa (redefinition).
#include "common/types.hpp"
#include "common/logger.hpp"
#include <string>
#include <functional>
#include <memory>
#include <mutex>

namespace httplib
{
    class Server;
}

namespace rs
{

    struct PipelineConfig
    {
        int tile_size = 512;
        int overlap = 64;
        std::string model = "mock";
        std::string model_path = "/app/models/yolov8n-seg.onnx";
        int max_workers = 0;
        float conf_thresh = 0.5f;
    };

    // Bước 1: Gateway xin ID từ hệ thống (chưa có file)
    using UploadInitCallback = std::function<int64_t()>;

    // Bước 2: Gateway báo file đã ghi xong, chốt filepath
    using UploadCompleteCallback = std::function<void(int64_t session_id,
                                                      const std::string &filepath,
                                                      const std::string &filename,
                                                      size_t file_size)>;

    using ConfigCallback = std::function<bool(int64_t, const PipelineConfig &)>;
    using StartCallback = std::function<bool(int64_t)>;
    using CancelCallback = std::function<bool(int64_t)>;
    using StatusCallback = std::function<SessionInfo(int64_t)>;
    using ResultsCallback = std::function<std::string(int64_t)>;

    class HttpGateway
    {
    public:
        explicit HttpGateway(int port = 8080);
        ~HttpGateway();

        void onUploadInit(UploadInitCallback cb) { upload_init_cb_ = cb; }
        void onUploadComplete(UploadCompleteCallback cb) { upload_complete_cb_ = cb; }
        void onConfig(ConfigCallback cb) { config_cb_ = cb; }
        void onStart(StartCallback cb) { start_cb_ = cb; }
        void onCancel(CancelCallback cb) { cancel_cb_ = cb; }
        void onStatus(StatusCallback cb) { status_cb_ = cb; }
        void onResults(ResultsCallback cb) { results_cb_ = cb; }

        void start();
        void stop();
        bool isRunning() const { return running_; }

    private:
        int port_;
        bool running_ = false;
        std::unique_ptr<httplib::Server> svr_;

        UploadInitCallback upload_init_cb_;
        UploadCompleteCallback upload_complete_cb_;
        ConfigCallback config_cb_;
        StartCallback start_cb_;
        CancelCallback cancel_cb_;
        StatusCallback status_cb_;
        ResultsCallback results_cb_;

        void setupRoutes();
        static std::string sessionStoragePath();
        static PipelineConfig parseConfig(const std::string &body);
        static bool validateConfig(const PipelineConfig &cfg, std::string &error);
        static std::string sessionInfoToJson(const SessionInfo &info);
        // static ở trong private báo hiệu rằng: Các hàm này dùng để hỗ trợ nội bộ cho HttpGateway
        // (ví dụ: bóc tách chuỗi JSON) mà không cần phải phụ thuộc vào dữ liệu của một instance (đối tượng) cụ thể nào.
    };

} // namespace rs
