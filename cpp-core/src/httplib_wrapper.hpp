#pragma once

// Disable tất cả SSL/TLS trước khi include httplib
// Phải đặt TRƯỚC #include httplib.h
#undef CPPHTTPLIB_OPENSSL_SUPPORT
#define CPPHTTPLIB_OPENSSL_SUPPORT 0

#undef CPPHTTPLIB_USE_POLL
#define CPPHTTPLIB_USE_POLL 0

#include "httplib.h"