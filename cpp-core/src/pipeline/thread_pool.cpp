#include "pipeline/thread_pool.hpp"
#include "common/logger.hpp"
#include <thread>

namespace rs
{

    ThreadPool::ThreadPool(int worker_count)
    {
        if (worker_count <= 0)
        {
            // Tự detect số core, để lại 1 core cho main thread
            int hw = (int)std::thread::hardware_concurrency();
            worker_count_ = std::max(1, hw - 1);
        }
        else
        {
            worker_count_ = worker_count;
        }
        LOG_INFO("ThreadPool", "Initialized with " + std::to_string(worker_count_) + " workers.");
    }

    ThreadPool::~ThreadPool()
    {
        // Đảm bảo queue closed và threads joined khi destructor gọi
        queue_.close();
        for (auto &t : workers_)
        {
            if (t.joinable())
                t.join();
        }
    }

    // ─── start ────────────────────────────────────────────────────
    void ThreadPool::start(WorkerFn fn)
    {
        worker_fn_ = fn;
        workers_.reserve(worker_count_);

        for (int i = 0; i < worker_count_; i++)
        {
            workers_.emplace_back([this, i]()
                                  { workerLoop(i); });
        }

        LOG_INFO("ThreadPool", "All " + std::to_string(worker_count_) + " workers started.");
    }

    // ─── submit ───────────────────────────────────────────────────
    void ThreadPool::submit(TileData tile)
    {
        tiles_submitted_++;
        queue_.push(std::move(tile));
    }

    // ─── waitAll ──────────────────────────────────────────────────
    void ThreadPool::waitAll()
    {
        LOG_INFO("ThreadPool", "Waiting for all workers to finish...");
        queue_.close();

        for (auto &t : workers_)
        {
            if (t.joinable())
                t.join();
        }
        workers_.clear();
        queue_.reset();

        LOG_INFO("ThreadPool", "All workers done. Processed: " + std::to_string(tiles_processed_.load()) + " tiles.");
    }

    // ─── workerLoop ───────────────────────────────────────────────
    void ThreadPool::workerLoop(int worker_id)
    {
        LOG_DEBUG("ThreadPool", "Worker " + std::to_string(worker_id) + " ready.");

        while (true)
        {
            auto item = queue_.pop();

            // nullopt = queue closed & empty → thoát
            if (!item.has_value())
            {
                LOG_DEBUG("ThreadPool", "Worker " + std::to_string(worker_id) + " exiting.");
                break;
            }

            TileData &tile = item.value();

            // Gọi hàm xử lý được inject từ bên ngoài
            worker_fn_(tile);

            tiles_processed_++;
        }
    }

} // namespace rs