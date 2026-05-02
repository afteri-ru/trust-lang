#pragma once

#include <cassert>
#include <exception>
#include <format>
#include <string_view>

namespace trust::utils {

template <typename... Args>
static std::string Message(std::string_view file, int line, std::format_string<Args...> fmt, Args &&...args) {
    std::string_view fname = file;
    if (auto pos = file.rfind('/'); pos != std::string_view::npos) {
        fname = file.substr(pos + 1);
    }
    return std::format("{}:{}: {}", fname, line, std::format(fmt, std::forward<Args>(args)...));
}

} // namespace trust::utils

#ifndef FAULT_AS
#define FAULT_AS(exception_type, ...) throw exception_type(trust::utils::Message(__FILE__, __LINE__, __VA_ARGS__))
#endif

#ifndef FAULT
#define FAULT(...) FAULT_AS(std::runtime_error, __VA_ARGS__)
#endif

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