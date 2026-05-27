#pragma once
#include "common/types.hpp"
#include <queue>
#include <mutex>
#include <condition_variable>
#include <optional>

namespace rs
{

    template <typename T>
    class ThreadSafeQueue
    {
    public:
        // Push item vào queue, notify 1 worker đang chờ
        void push(T item)
        {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                queue_.push(std::move(item));
            }
            cv_.notify_one();
        }

        // Pop blocking — chờ cho đến khi có item hoặc queue bị đóng
        // Trả std::nullopt nếu queue đã closed và rỗng
        std::optional<T> pop()
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this]
                     { return !queue_.empty() || closed_; });

            if (queue_.empty())
                return std::nullopt;

            T item = std::move(queue_.front());
            queue_.pop();
            return item;
        }

        // Đóng queue — unblock tất cả worker đang chờ
        void close()
        {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                closed_ = true;
            }
            cv_.notify_all();
        }

        void reset()
        {
            std::lock_guard<std::mutex> lock(mutex_);
            while (!queue_.empty())
                queue_.pop();
            closed_ = false;
        }

        size_t size() const
        {
            std::lock_guard<std::mutex> lock(mutex_);
            return queue_.size();
        }

        bool isClosed() const
        {
            std::lock_guard<std::mutex> lock(mutex_);
            return closed_;
        }

    private:
        mutable std::mutex mutex_;
        std::condition_variable cv_;
        std::queue<T> queue_;
        bool closed_ = false;
    };

    // Alias tiện dụng
    using TileQueue = ThreadSafeQueue<TileData>;
    // Chứa TileData (để gửi từ phần cắt ảnh sang phần AI).
    using ResultQueue = ThreadSafeQueue<std::pair<TileData, std::vector<Detection>>>;
    // Chứa kết quả AI (để gửi từ phần AI sang phần ghép ảnh Stitching). Bạn không cần phải viết lại logic khóa/mở khóa hai lần.

} // namespace rs