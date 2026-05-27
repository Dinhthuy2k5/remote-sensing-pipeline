#include "pipeline/state_machine.hpp"
#include <stdexcept>

namespace rs
{

    // ─── Bảng transition hợp lệ ──────────────────────────────────
    //
    //  IDLE ──────────────► LOADING
    //  LOADING ───────────► TILING | ERROR
    //  TILING ────────────► PROCESSING | ERROR
    //  PROCESSING ────────► STITCHING | ERROR
    //  STITCHING ─────────► SAVING | ERROR
    //  SAVING ────────────► DONE | ERROR
    //  ERROR ─────────────► RECOVERING
    //  RECOVERING ────────► IDLE
    //  DONE ──────────────► IDLE   (cho phép chạy lại)
    //
    const std::unordered_map<int, std::vector<int>> &StateMachine::validTransitions()
    {
        static const std::unordered_map<int, std::vector<int>> table = {
            {(int)SessionStatus::IDLE, {(int)SessionStatus::LOADING}},
            {(int)SessionStatus::LOADING, {(int)SessionStatus::TILING, (int)SessionStatus::ERROR}},
            {(int)SessionStatus::TILING, {(int)SessionStatus::PROCESSING, (int)SessionStatus::ERROR}},
            {(int)SessionStatus::PROCESSING, {(int)SessionStatus::STITCHING, (int)SessionStatus::ERROR}},
            {(int)SessionStatus::STITCHING, {(int)SessionStatus::SAVING, (int)SessionStatus::ERROR}},
            {(int)SessionStatus::SAVING, {(int)SessionStatus::DONE, (int)SessionStatus::ERROR}},
            {(int)SessionStatus::ERROR, {(int)SessionStatus::RECOVERING}},
            {(int)SessionStatus::RECOVERING, {(int)SessionStatus::IDLE}},
            {(int)SessionStatus::DONE, {
                                           (int)SessionStatus::IDLE // cho phép chạy lại session
                                       }},
        };
        return table;
    }

    // ─── Constructor ──────────────────────────────────────────────
    StateMachine::StateMachine(int64_t session_id)
        : session_id_(session_id), state_((int)SessionStatus::IDLE)
    {
        LOG_INFO("StateMachine",
                 "Session " + std::to_string(session_id_) + " created. State: IDLE");
    }

    // ─── transition ───────────────────────────────────────────────
    bool StateMachine::transition(SessionStatus to)
    {
        SessionStatus from = current();
        int from_int = (int)from;
        int to_int = (int)to;

        const auto &table = validTransitions();
        auto it = table.find(from_int);

        if (it == table.end())
        {
            LOG_WARN("StateMachine",
                     "No transitions defined from state: " + toString(from));
            return false;
        }

        const auto &allowed = it->second;
        bool valid = false;
        for (int s : allowed)
        {
            if (s == to_int)
            {
                valid = true;
                break;
            }
        }

        if (!valid)
        {
            LOG_WARN("StateMachine",
                     "Invalid transition: " + toString(from) + " → " + toString(to) + " (session " + std::to_string(session_id_) + ")");
            return false;
        }

        // atomic compare-and-swap: chỉ set nếu state vẫn là from
        // tránh race condition khi 2 thread cùng transition
        if (!state_.compare_exchange_strong(from_int, to_int))
        {
            LOG_WARN("StateMachine",
                     "Transition race detected on session " + std::to_string(session_id_) + ". State changed by another thread.");
            return false;
        }

        LOG_INFO("StateMachine",
                 "Session " + std::to_string(session_id_) + ": " + toString(from) + " → " + toString(to));

        notify(from, to);
        return true;
    }

    // ─── forceTransition ──────────────────────────────────────────
    void StateMachine::forceTransition(SessionStatus to)
    {
        SessionStatus from = current();
        state_.store((int)to);

        LOG_WARN("StateMachine",
                 "FORCE session " + std::to_string(session_id_) + ": " + toString(from) + " → " + toString(to));

        notify(from, to);
    }

    // ─── isTerminal ───────────────────────────────────────────────
    bool StateMachine::isTerminal() const
    {
        auto s = current();
        return s == SessionStatus::DONE || s == SessionStatus::ERROR;
    }

    // ─── canProcess ───────────────────────────────────────────────
    bool StateMachine::canProcess() const
    {
        auto s = current();
        return s == SessionStatus::TILING || s == SessionStatus::PROCESSING || s == SessionStatus::STITCHING;
    }

    // ─── notify ───────────────────────────────────────────────────
    void StateMachine::notify(SessionStatus from, SessionStatus to)
    {
        std::lock_guard<std::mutex> lock(cb_mutex_);
        for (auto &cb : callbacks_)
        {
            cb(from, to);
        }
    }

    // ─── toString ─────────────────────────────────────────────────
    std::string StateMachine::toString(SessionStatus s)
    {
        switch (s)
        {
        case SessionStatus::IDLE:
            return "IDLE";
        case SessionStatus::LOADING:
            return "LOADING";
        case SessionStatus::TILING:
            return "TILING";
        case SessionStatus::PROCESSING:
            return "PROCESSING";
        case SessionStatus::STITCHING:
            return "STITCHING";
        case SessionStatus::SAVING:
            return "SAVING";
        case SessionStatus::DONE:
            return "DONE";
        case SessionStatus::ERROR:
            return "ERROR";
        case SessionStatus::RECOVERING:
            return "RECOVERING";
        }
        return "UNKNOWN";
    }

} // namespace rs