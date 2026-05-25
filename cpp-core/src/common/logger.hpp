#pragma once
#include <iostream>
#include <mutex>
#include <string>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace rs
{

    class Logger
    {
    public:
        enum class Level
        {
            DEBUG,
            INFO,
            WARN,
            ERROR
        };

        static Logger &instance()
        {
            static Logger inst;
            return inst;
        }

        void log(Level level, const std::string &module, const std::string &msg)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            std::cout << "[" << timestamp() << "] "
                      << levelStr(level) << " "
                      << "[" << module << "] "
                      << msg << "\n";
        }

    private:
        std::mutex mutex_;

        std::string timestamp()
        {
            auto now = std::chrono::system_clock::now();
            auto t = std::chrono::system_clock::to_time_t(now);
            std::ostringstream oss;
            oss << std::put_time(std::localtime(&t), "%H:%M:%S");
            return oss.str();
        }

        static std::string levelStr(Level l)
        {
            switch (l)
            {
            case Level::DEBUG:
                return "\033[37mDEBUG\033[0m";
            case Level::INFO:
                return "\033[32mINFO \033[0m";
            case Level::WARN:
                return "\033[33mWARN \033[0m";
            case Level::ERROR:
                return "\033[31mERROR\033[0m";
            }
            return "?????";
        }
    };

#define LOG_INFO(module, msg) rs::Logger::instance().log(rs::Logger::Level::INFO, module, msg)
#define LOG_WARN(module, msg) rs::Logger::instance().log(rs::Logger::Level::WARN, module, msg)
#define LOG_ERROR(module, msg) rs::Logger::instance().log(rs::Logger::Level::ERROR, module, msg)
#define LOG_DEBUG(module, msg) rs::Logger::instance().log(rs::Logger::Level::DEBUG, module, msg)

} // namespace rs