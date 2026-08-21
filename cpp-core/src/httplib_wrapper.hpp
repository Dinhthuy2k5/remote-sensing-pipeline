#pragma once

#define CPPHTTPLIB_OPENSSL_SUPPORT

#undef CPPHTTPLIB_USE_POLL
#define CPPHTTPLIB_USE_POLL 0

#include "httplib.h"