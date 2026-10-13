#pragma once

#include <cstdarg>
#include <cstdio>
#include <stdexcept>
#include <string>

namespace trust {

/// Исключение синтаксической фазы. Сообщение форматируется printf-подобной строкой.
class ParserError : public std::runtime_error {
  public:
    ParserError(const std::string& msg)
    : runtime_error(msg) {}

    ParserError(const char* format, ...)
    : runtime_error(formatMessage(format)) {}

  private:
    static std::string formatMessage(const char* format, ...) {
        va_list args;
        va_start(args, format);
        int size = std::vsnprintf(nullptr, 0, format, args);
        va_end(args);
        std::string result(size + 1, '\0');
        va_start(args, format);
        std::vsnprintf(&result[0], size + 1, format, args);
        va_end(args);
        result.resize(size);
        return result;
    }
};

} // namespace trust
