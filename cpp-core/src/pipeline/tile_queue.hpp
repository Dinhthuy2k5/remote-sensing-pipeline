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
        // capacity = 0 → unbounded (giữ backward compat)
        explicit ThreadSafeQueue(size_t capacity = 0)
            : capacity_(capacity)
        {
        }

        // Push item vào queue, block nếu queue đầy, notify 1 worker đang chờ
        bool push(T item)
        {
            std::unique_lock<std::mutex> lock(mutex_);

            // Chờ cho đến khi còn chỗ hoặc queue bị đóng
            cv_not_full_.wait(lock, [this]
                              { return closed_ || capacity_ == 0 || queue_.size() < capacity_; });

            if (closed_)
                return false;

            queue_.push(std::move(item));
            cv_not_empty_.notify_one();
            return true;
        }

        // Pop blocking — chờ cho đến khi có item hoặc queue bị đóng
        // Trả std::nullopt nếu queue đã closed và rỗng
        // Pop — block cho đến khi có item hoặc queue đóng
        std::optional<T> pop()
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_not_empty_.wait(lock, [this]
                               { return !queue_.empty() || closed_; });

            if (queue_.empty())
                return std::nullopt;

            T item = std::move(queue_.front());
            queue_.pop();

            // Notify producer đang chờ chỗ trống
            cv_not_full_.notify_one();
            return item;
        }

        // Đóng queue — unblock tất cả worker đang chờ
        void close()
        {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                closed_ = true;
            }
            cv_not_empty_.notify_all();
            cv_not_full_.notify_all(); // unblock producer đang chờ
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
        std::condition_variable cv_not_empty_;
        std::condition_variable cv_not_full_;
        size_t capacity_ = 0;
        std::queue<T> queue_;
        bool closed_ = false;
    };

    // Alias tiện dụng
    using TileQueue = ThreadSafeQueue<TileData>;
    // Chứa TileData (để gửi từ phần cắt ảnh sang phần AI).
    using ResultQueue = ThreadSafeQueue<std::pair<TileData, std::vector<Detection>>>;
    // Chứa kết quả AI (để gửi từ phần AI sang phần ghép ảnh Stitching). Bạn không cần phải viết lại logic khóa/mở khóa hai lần.

} // namespace rs
