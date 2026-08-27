// trust/assert.hpp - runtime error helpers for the Trust runtime.
//
// Public runtime header: self-contained (standard headers only) so that
// generated C++ programs can include it without depending on the compiler's
// include tree. At build time it is embedded into trust-runtime.so/.a (via
// #embed, in an ELF section named "trust/assert.hpp"); the pipeline extracts it
// into a temporary `trust/` directory and links the runtime library when a
// generated program actually uses one of its symbols (trust__abort__ /
// trust::formatMessage).
//
// The backing symbols trust::errs() and trust::utils::backtrace_string() are
// provided by the linked trust-runtime library (src/utils/io.cpp and
// src/utils/backtrace.cpp), which is why they are only forward-declared here.

#pragma once

#include <cstdlib>
#include <format>
#include <ostream>
#include <string>
#include <string_view>

namespace trust {

/// Глобальный stderr-поток (предоставляется trust-runtime библиотекой).
std::ostream& errs();

namespace utils {
/// Стек вызовов (предоставляется trust-runtime библиотекой).
std::string backtrace_string(int max_frames);
} // namespace utils

/// Форматирует диагностическое сообщение с префиксом "файл:строка:".
template <typename... Args>
std::string formatMessage(std::string_view file, int line, std::format_string<Args...> fmt, Args&&... args) {
    std::string_view fname = file;
    if (auto pos = file.rfind('/'); pos != std::string_view::npos) {
        fname = file.substr(pos + 1);
    }
    return std::format("{}:{}: {}", fname, line, std::format(fmt, std::forward<Args>(args)...));
}

/// Проверка утверждения/завершение (единая точка abort для assert/verify и ручного abort).
/// Вызывается ТОЛЬКО при провале условия через short-circuit `cond || trust__abort__(...)`
/// в макросе: печатает сообщение вида `file:line: expr` в trust::errs() (и, если trace == true,
/// стек вызовов) и завершает процесс БЕЗ core-dump (std::_Exit(EXIT_FAILURE)).
/// `[[noreturn]]` и возврат `bool` нужны, чтобы использовать её как правый операнд `||`
/// (при ложном cond вызов происходит, при истинном - short-circuit пропускает вызов).
[[noreturn]] inline bool trust__abort__(std::string_view file, int line, std::string_view expr, bool trace = true) {
    std::string_view fname = file;
    if (auto pos = file.rfind('/'); pos != std::string_view::npos) {
        fname = file.substr(pos + 1);
    }
    errs() << std::format("{}:{}: {}", fname, line, expr) << "\n";
    if (trace) {
        errs() << utils::backtrace_string(3) << std::flush;
    }
    // Завершение БЕЗ core-dump: std::abort() порождает SIGABRT и (при ulimit -c) core-файл.
    // _Exit не запускает деструкторы/atexit, но вся диагностика уже выведена (std::cerr
    // небуферизован), а код возврата ненулевой (завершение «по ошибке»).
    std::_Exit(EXIT_FAILURE);
}

} // namespace trust
