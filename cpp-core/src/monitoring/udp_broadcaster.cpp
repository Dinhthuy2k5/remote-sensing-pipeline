#include "monitoring/udp_broadcaster.hpp"
#include "common/logger.hpp"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>

#include <fstream>
#include <sstream>
#include <chrono>
#include <cstring>
#include <iomanip>

namespace rs
{

    UdpBroadcaster::UdpBroadcaster(int port, int interval_ms)
        : port_(port), interval_ms_(interval_ms)
    {
    }

    UdpBroadcaster::~UdpBroadcaster() { stop(); }

    // ─── metricsToJson ────────────────────────────────────────────
    std::string UdpBroadcaster::metricsToJson(const SystemMetrics &m)
    {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(1);
        oss << "{"
            << "\"session_id\":" << m.session_id << ","
            << "\"state\":\"" << m.state << "\","
            << "\"cpu_percent\":" << m.cpu_percent << ","
            << "\"ram_used_mb\":" << m.ram_used_mb << ","
            << "\"ram_total_mb\":" << m.ram_total_mb << ","
            << "\"fps\":" << m.fps << ","
            << "\"tiles_done\":" << m.tiles_done << ","
            << "\"tiles_total\":" << m.tiles_total << ","
            << "\"queue_size\":" << m.queue_size // ← thêm
            << "}";
        return oss.str();
    }

    // ─── readMemInfo — đọc từ /proc/meminfo ───────────────────────
    void UdpBroadcaster::readMemInfo(int &used_mb, int &total_mb)
    {
        std::ifstream f("/proc/meminfo");
        if (!f)
        {
            used_mb = 0;
            total_mb = 0;
            return;
        }

        long mem_total = 0, mem_available = 0;
        std::string line;
        while (std::getline(f, line))
        {
            if (line.rfind("MemTotal:", 0) == 0)
                mem_total = std::stol(line.substr(9));
            else if (line.rfind("MemAvailable:", 0) == 0)
                mem_available = std::stol(line.substr(13));
        }
        total_mb = (int)(mem_total / 1024);
        used_mb = (int)((mem_total - mem_available) / 1024);
    }

    // ─── readCpuPercent — 2 sample từ /proc/stat ─────────────────
    float UdpBroadcaster::readCpuPercent()
    {
        auto readStat = []() -> std::pair<long, long>
        {
            std::ifstream f("/proc/stat");
            if (!f)
                return {0, 0};
            std::string cpu;
            long user, nice, system, idle, iowait, irq, softirq;
            f >> cpu >> user >> nice >> system >> idle >> iowait >> irq >> softirq;
            long total = user + nice + system + idle + iowait + irq + softirq;
            return {idle, total};
        };

        auto [idle1, total1] = readStat();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        auto [idle2, total2] = readStat();

        long d_total = total2 - total1;
        long d_idle = idle2 - idle1;
        if (d_total == 0)
            return 0.0f;
        return 100.0f * (1.0f - (float)d_idle / (float)d_total);
    }

    // ─── start ────────────────────────────────────────────────────
    void UdpBroadcaster::start()
    {
        // Tạo UDP socket
        sock_fd_ = socket(AF_INET, SOCK_DGRAM, 0);
        if (sock_fd_ < 0)
        {
            LOG_ERROR("UdpBroadcaster", "Failed to create socket.");
            return;
        }

        // Enable broadcast
        int broadcast = 1;
        setsockopt(sock_fd_, SOL_SOCKET, SO_BROADCAST,
                   &broadcast, sizeof(broadcast));

        running_ = true;
        thread_ = std::thread([this]()
                              { broadcastLoop(); });
        LOG_INFO("UdpBroadcaster",
                 "Broadcasting on UDP port " + std::to_string(port_) + " every " + std::to_string(interval_ms_) + "ms");
    }

    // ─── stop ─────────────────────────────────────────────────────
    void UdpBroadcaster::stop()
    {
        running_ = false;
        if (thread_.joinable())
            thread_.join();
        if (sock_fd_ >= 0)
        {
            close(sock_fd_);
            sock_fd_ = -1;
        }
        LOG_DEBUG("UdpBroadcaster", "Stopped.");
    }

    // ─── broadcastLoop ────────────────────────────────────────────
    void UdpBroadcaster::broadcastLoop()
    {
        struct sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons((uint16_t)port_);

        struct hostent *he = gethostbyname("host.docker.internal");
        if (he && he->h_addr_list[0])
        {
            addr.sin_addr = *(struct in_addr *)he->h_addr_list[0];
            LOG_INFO("UdpBroadcaster",
                     "Target: host.docker.internal = " + std::string(inet_ntoa(addr.sin_addr)));
        }
        else
        {
            addr.sin_addr.s_addr = inet_addr("255.255.255.255");
            LOG_WARN("UdpBroadcaster",
                     "host.docker.internal not found, fallback to 255.255.255.255");
        }

        // ← Khai báo ở đây, trước while
        auto last_time = std::chrono::steady_clock::now();
        int last_tiles = 0;

        while (running_)
        {
            SystemMetrics m;
            if (provider_)
                m = provider_();

            readMemInfo(m.ram_used_mb, m.ram_total_mb);
            m.cpu_percent = readCpuPercent();

            auto now = std::chrono::steady_clock::now();
            float dt_sec = std::chrono::duration<float>(now - last_time).count();
            if (dt_sec > 0 && m.tiles_done > last_tiles)
                m.fps = (m.tiles_done - last_tiles) / dt_sec;
            last_time = now;
            last_tiles = m.tiles_done;

            std::string payload = metricsToJson(m);
            sendto(sock_fd_, payload.c_str(), payload.size(),
                   0, (struct sockaddr *)&addr, sizeof(addr));

            LOG_DEBUG("UdpBroadcaster", "Sent: " + payload);

            int sleep_ms = (m.state != "IDLE" && m.state != "DONE")
                               ? 100
                               : interval_ms_;

            for (int i = 0; i < sleep_ms / 50 && running_; i++)
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
} // namespace rs