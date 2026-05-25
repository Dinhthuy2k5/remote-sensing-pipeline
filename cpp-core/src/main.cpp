#include "common/logger.hpp"
#include "common/types.hpp"

int main()
{
    LOG_INFO("main", "Remote Sensing Pipeline starting...");
    LOG_INFO("main", "Version 0.1.0");

    // Tuần 1 Ngày 3-4 : TilingEngine
    // Tuần 1 Ngày 5-6 : ThreadPool
    // Tuần 1 Ngày 7   : StateMachine
    // Tuần 2 Ngày 15  : API Gateway

    LOG_INFO("main", "Pipeline ready. Waiting for requests...");
    return 0;
}