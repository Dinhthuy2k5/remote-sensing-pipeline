#pragma once
#include <string>
#include <thread>
#include <atomic>
#include <functional>
#include <cstdint>

namespace rs
{

    struct SystemMetrics
    {
        float cpu_percent = 0.0f;
        int ram_used_mb = 0;
        int ram_total_mb = 0;
        float fps = 0.0f; // tiles/sec
        int tiles_done = 0;
        int tiles_total = 0;
        int queue_size = 0;
        int64_t session_id = -1;
        std::string state = "IDLE";
    };

    // Callback để lấy metrics từ pipeline
    using MetricsProvider = std::function<SystemMetrics()>;

    class UdpBroadcaster
    {
    public:
        UdpBroadcaster(int port = 9090, int interval_ms = 500);
        ~UdpBroadcaster();

        UdpBroadcaster(const UdpBroadcaster &) = delete;
        UdpBroadcaster &operator=(const UdpBroadcaster &) = delete;

        // Đăng ký callback lấy metrics
        void setMetricsProvider(MetricsProvider fn) { provider_ = fn; }

        // Start broadcast thread
        void start();
        void stop();

        bool isRunning() const { return running_.load(); }

    private:
        int port_;
        int interval_ms_;
        std::atomic<bool> running_{false};
        std::thread thread_;
        MetricsProvider provider_;
        int sock_fd_ = -1;

        void broadcastLoop();
        static std::string metricsToJson(const SystemMetrics &m);

        // Đọc RAM từ /proc/meminfo
        static void readMemInfo(int &used_mb, int &total_mb);

        // Đọc CPU từ /proc/stat (2 sample, tính delta)
        static float readCpuPercent();
    };

} // namespace rs