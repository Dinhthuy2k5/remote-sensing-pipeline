#pragma once
#include "common/types.hpp"
#include "pipeline/tile_queue.hpp"
#include <vector>
#include <thread>
#include <functional>
#include <atomic>

namespace rs
{

    class ThreadPool
    {
    public:
        // worker_count: số thread, mặc định = số CPU core
        explicit ThreadPool(int worker_count = 0);
        ~ThreadPool();

        // Không cho copy
        ThreadPool(const ThreadPool &) = delete;
        ThreadPool &operator=(const ThreadPool &) = delete;

        // Khởi động workers, truyền vào hàm xử lý 1 tile
        using WorkerFn = std::function<void(TileData &)>;
        void start(WorkerFn fn);

        // Producer: đẩy tile vào queue
        void submit(TileData tile);

        // Báo không còn tile nào nữa, chờ workers xong
        void waitAll();

        // Getter
        int workerCount() const { return worker_count_; }
        int tilesSubmitted() const { return tiles_submitted_.load(); }
        int tilesProcessed() const { return tiles_processed_.load(); }

    private:
        int worker_count_;
        std::vector<std::thread> workers_;
        TileQueue queue_;
        WorkerFn worker_fn_;
        std::atomic<int> tiles_submitted_{0};
        std::atomic<int> tiles_processed_{0};
        // biến các phép cộng trừ thành các thao tác nguyên tử (không thể bị chia cắt bởi luồng khác)
        // ở cấp độ phần cứng CPU. Nhanh hơn dùng mutex rất nhiều.

        void workerLoop(int worker_id);
    };

} // namespace rs