#pragma once
#include "common/types.hpp"
#include "common/logger.hpp"
#include <atomic>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>

namespace rs
{

    // Callback khi state thay đổi
    using StateChangeCallback = std::function<void(SessionStatus from, SessionStatus to)>;

    class StateMachine
    {
    public:
        explicit StateMachine(int64_t session_id);

        // Thử chuyển sang state mới
        // Trả true nếu transition hợp lệ, false nếu bị từ chối
        bool transition(SessionStatus to);

        // Force chuyển state không cần kiểm tra (dùng cho recovery)
        void forceTransition(SessionStatus to);

        // Getter
        SessionStatus current() const
        {
            return static_cast<SessionStatus>(state_.load());
        }

        int64_t sessionId() const { return session_id_; }

        // Đăng ký callback khi state thay đổi
        void onStateChange(StateChangeCallback cb)
        {
            std::lock_guard<std::mutex> lock(cb_mutex_);
            callbacks_.push_back(cb);
        }

        // Helper
        static std::string toString(SessionStatus s);
        bool isTerminal() const; // DONE hoặc ERROR
        bool canProcess() const; // đang trong trạng thái xử lý

    private:
        int64_t session_id_;
        std::atomic<int> state_; // lưu int thay vì enum để dùng atomic
        mutable std::mutex cb_mutex_;
        std::vector<StateChangeCallback> callbacks_;

        // Bảng transition hợp lệ
        static const std::unordered_map<int, std::vector<int>> &validTransitions();

        void notify(SessionStatus from, SessionStatus to);
    };

} // namespace rs