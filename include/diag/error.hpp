#pragma once

#include <cassert>
#include <exception>
#include <format>
#include <string_view>

#include "diag/location.hpp"

namespace trust {

template <typename... Args>
static std::string Message(std::string_view file, int line, std::format_string<Args...> fmt, Args &&...args) {
    // Извлекаем имя файла из полного пути (ищем последний '/')
    std::string_view fname = file;
    if (auto pos = file.rfind('/'); pos != std::string_view::npos) {
        fname = file.substr(pos + 1);
    }
    return std::format("{}:{}: {}", fname, line, std::format(fmt, std::forward<Args>(args)...));
}

#ifndef ASSERT
#define ASSERT(...) assert(__VA_ARGS__)
#endif

#ifndef FAULT
#define FAULT(...) throw std::runtime_error(Message(__FILE__, __LINE__, __VA_ARGS__))
#endif

class SyntaxError : public std::runtime_error {
  public:
    SourceLoc location;

    explicit SyntaxError(const char *msg, SourceLoc loc = {}) : std::runtime_error(msg), location(loc) {}
};

class ConversionError : public std::runtime_error {
  public:
    explicit ConversionError(std::string const &msg) : std::runtime_error(msg) {}
    explicit ConversionError(const char *msg) : std::runtime_error(msg) {}
};

class ParseError : public std::runtime_error {
  public:
    explicit ParseError(std::string const &msg) : std::runtime_error(msg) {}
    explicit ParseError(const char *msg) : std::runtime_error(msg) {}
};

#ifndef FAULT_AS
#define FAULT_AS(exception_type, ...) throw exception_type(Message(__FILE__, __LINE__, __VA_ARGS__))
#endif

} // namespace trust