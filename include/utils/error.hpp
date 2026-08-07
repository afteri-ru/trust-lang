#pragma once

#include "trust/assert.hpp"
#include "utils/backtrace.hpp"

#include <cassert>
#include <exception>
#include <format>
#include <string_view>

#ifndef FAULT_AS
#define FAULT_AS(exception_type, ...) throw exception_type(trust::formatMessage(__FILE__, __LINE__, __VA_ARGS__))
#endif

#ifndef FAULT
#define FAULT(...) FAULT_AS(std::runtime_error, __VA_ARGS__)
#endif

// clang-format off
#ifndef EXPECT
#define EXPECT(expr)  do { \
    if (!static_cast<bool>(expr)) { \
        auto _trust_bt_ = trust::utils::backtrace_string(3); \
        FAULT("EXPECTED: '{}'\n{}", #expr, _trust_bt_); \
    } \
} while (0)
#endif
// clang-format on

#ifndef ASSERT
#define ASSERT(...) assert(__VA_ARGS__)
#endif

#ifdef NDEBUG // For RELEASE build
#ifndef VERIFY
#define VERIFY(...) (void)(__VA_ARGS__)
#endif
#else // For DEBUG build
#ifndef VERIFY
#define VERIFY(...) assert(__VA_ARGS__)
#endif
#endif