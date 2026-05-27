#include "pipeline/state_machine.hpp"
#include "common/logger.hpp"
#include <cassert>
#include <iostream>

using namespace rs;

// Helper macro
#define TEST(name) std::cout << "\n[TEST] " << name << "\n"
#define ASSERT_EQ(a, b)                                                       \
    if ((a) != (b))                                                           \
    {                                                                         \
        std::cerr << "FAIL: expected " << StateMachine::toString(b)           \
                  << " got " << StateMachine::toString(a) << "\n";            \
        assert(false);                                                        \
    }                                                                         \
    else                                                                      \
    {                                                                         \
        std::cout << "  PASS: state = " << StateMachine::toString(a) << "\n"; \
    }
#define ASSERT_TRUE(x)                       \
    {                                        \
        if (!(x))                            \
        {                                    \
            std::cerr << "FAIL: " #x "\n";   \
            assert(false);                   \
        }                                    \
        else                                 \
        {                                    \
            std::cout << "  PASS: " #x "\n"; \
        }                                    \
    }
#define ASSERT_FALSE(x)                                    \
    {                                                      \
        if ((x))                                           \
        {                                                  \
            std::cerr << "FAIL: expected false: " #x "\n"; \
            assert(false);                                 \
        }                                                  \
        else                                               \
        {                                                  \
            std::cout << "  PASS: !" #x "\n";              \
        }                                                  \
    }

void test_happy_path()
{
    TEST("Happy path: IDLE → LOADING → TILING → PROCESSING → STITCHING → SAVING → DONE");

    StateMachine sm(1);
    ASSERT_EQ(sm.current(), SessionStatus::IDLE);

    ASSERT_TRUE(sm.transition(SessionStatus::LOADING));
    ASSERT_EQ(sm.current(), SessionStatus::LOADING);

    ASSERT_TRUE(sm.transition(SessionStatus::TILING));
    ASSERT_EQ(sm.current(), SessionStatus::TILING);

    ASSERT_TRUE(sm.transition(SessionStatus::PROCESSING));
    ASSERT_EQ(sm.current(), SessionStatus::PROCESSING);

    ASSERT_TRUE(sm.transition(SessionStatus::STITCHING));
    ASSERT_EQ(sm.current(), SessionStatus::STITCHING);

    ASSERT_TRUE(sm.transition(SessionStatus::SAVING));
    ASSERT_EQ(sm.current(), SessionStatus::SAVING);

    ASSERT_TRUE(sm.transition(SessionStatus::DONE));
    ASSERT_EQ(sm.current(), SessionStatus::DONE);

    ASSERT_TRUE(sm.isTerminal());
}

void test_invalid_transition()
{
    TEST("Invalid transition bị từ chối");

    StateMachine sm(2);

    // Không thể nhảy thẳng từ IDLE sang PROCESSING
    ASSERT_FALSE(sm.transition(SessionStatus::PROCESSING));
    ASSERT_EQ(sm.current(), SessionStatus::IDLE); // state không đổi

    // Không thể nhảy từ IDLE sang DONE
    ASSERT_FALSE(sm.transition(SessionStatus::DONE));
    ASSERT_EQ(sm.current(), SessionStatus::IDLE);
}

void test_error_recovery()
{
    TEST("Error recovery: PROCESSING → ERROR → RECOVERING → IDLE");

    StateMachine sm(3);
    sm.transition(SessionStatus::LOADING);
    sm.transition(SessionStatus::TILING);
    sm.transition(SessionStatus::PROCESSING);

    // Giả lập lỗi xảy ra
    ASSERT_TRUE(sm.transition(SessionStatus::ERROR));
    ASSERT_EQ(sm.current(), SessionStatus::ERROR);
    ASSERT_TRUE(sm.isTerminal());

    // Recovery
    ASSERT_TRUE(sm.transition(SessionStatus::RECOVERING));
    ASSERT_EQ(sm.current(), SessionStatus::RECOVERING);

    ASSERT_TRUE(sm.transition(SessionStatus::IDLE));
    ASSERT_EQ(sm.current(), SessionStatus::IDLE);
    ASSERT_FALSE(sm.isTerminal());
}

void test_callback()
{
    TEST("Callback được gọi khi state thay đổi");

    StateMachine sm(4);

    int callback_count = 0;
    SessionStatus last_from = SessionStatus::IDLE;
    SessionStatus last_to = SessionStatus::IDLE;

    sm.onStateChange([&](SessionStatus from, SessionStatus to)
                     {
        callback_count++;
        last_from = from;
        last_to   = to; });

    sm.transition(SessionStatus::LOADING);

    ASSERT_TRUE(callback_count == 1);
    ASSERT_EQ(last_from, SessionStatus::IDLE);
    ASSERT_EQ(last_to, SessionStatus::LOADING);

    sm.transition(SessionStatus::TILING);
    ASSERT_TRUE(callback_count == 2);
}

void test_rerun_after_done()
{
    TEST("Cho phép chạy lại sau DONE → IDLE");

    StateMachine sm(5);
    sm.transition(SessionStatus::LOADING);
    sm.transition(SessionStatus::TILING);
    sm.transition(SessionStatus::PROCESSING);
    sm.transition(SessionStatus::STITCHING);
    sm.transition(SessionStatus::SAVING);
    sm.transition(SessionStatus::DONE);

    // Reset để chạy lại
    ASSERT_TRUE(sm.transition(SessionStatus::IDLE));
    ASSERT_EQ(sm.current(), SessionStatus::IDLE);
    ASSERT_FALSE(sm.isTerminal());
}

void test_force_transition()
{
    TEST("forceTransition bỏ qua validation");

    StateMachine sm(6);
    // Force từ IDLE thẳng sang ERROR (bình thường không hợp lệ)
    sm.forceTransition(SessionStatus::ERROR);
    ASSERT_EQ(sm.current(), SessionStatus::ERROR);
}

int main()
{
    std::cout << "========================================\n";
    std::cout << "  State Machine Unit Tests\n";
    std::cout << "========================================\n";

    test_happy_path();
    test_invalid_transition();
    test_error_recovery();
    test_callback();
    test_rerun_after_done();
    test_force_transition();

    std::cout << "\n========================================\n";
    std::cout << "  ALL TESTS PASSED\n";
    std::cout << "========================================\n";
    return 0;
}