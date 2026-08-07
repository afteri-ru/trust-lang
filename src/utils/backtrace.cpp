// src/utils/backtrace.cpp
// Формирование стека вызовов (backtrace) в std::string и установка
// обработчиков аварийного завершения.

#include "utils/backtrace.hpp"

#include <cxxabi.h>
#include <execinfo.h>
#include <unistd.h>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <string>

namespace trust::utils {

// -----------------------------------------------------------------------
// backtrace_string  —  возвращает стек вызовов в std::string
// -----------------------------------------------------------------------
//
// Использует glibc backtrace()/backtrace_symbols() и demangling через
// __cxxabiv1::__cxa_demangle для C++ имён.
//
// Формат строки (после деманга):
//   #<num> <demangled_symbol> + <offset>
// -----------------------------------------------------------------------

std::string backtrace_string(int max_frames) {
    // Вычисляем размер буфера:
    //   max_frames <= 0  → 128 (выводим весь стек без ограничения)
    //   max_frames  > 0  → max_frames + 2 (фрейм 0 + небольшая страховка)
    int buffer_size = (max_frames <= 0) ? 128 : (max_frames + 2);

    void** buffer = static_cast<void**>(std::malloc(static_cast<std::size_t>(buffer_size) * sizeof(void*)));
    if (!buffer) {
        return "  (malloc for backtrace buffer failed)\n";
    }

    int frames = ::backtrace(buffer, buffer_size);

    if (frames <= 0) {
        std::free(buffer);
        return "  (no backtrace available)\n";
    }

    char** symbols = ::backtrace_symbols(buffer, frames);
    std::free(buffer); // больше не нужен

    if (!symbols) {
        return "  (backtrace_symbols failed)\n";
    }

    std::string result;

    // Пропускаем frame 0 (саму backtrace_string)
    int first = 1;
    int count = frames - first;
    if (max_frames > 0 && count > max_frames) {
        count = max_frames;
    }

    for (int idx = 0; idx < count; ++idx) {
        int i = first + idx;
        const char* sym = symbols[i];

        // Номер фрейма
        result += "  #";
        if (i < 10) {
            result += ' ';
        }
        result += std::to_string(i);
        result += "  ";

        // Парсим строку вида: ./program(function+0x42) [0x1234]
        const char* paren_open = std::strchr(sym, '(');
        const char* paren_close = std::strchr(sym, ')');
        const char* plus = paren_open ? std::strchr(paren_open, '+') : nullptr;

        if (paren_open && plus && paren_close && paren_open < plus && plus < paren_close) {
            std::size_t name_len = static_cast<std::size_t>(plus - paren_open - 1);
            char* mangled = static_cast<char*>(std::malloc(name_len + 1));
            if (mangled) {
                std::memcpy(mangled, paren_open + 1, name_len);
                mangled[name_len] = '\0';

                int status = 0;
                char* demangled = abi::__cxa_demangle(mangled, nullptr, nullptr, &status);
                if (status == 0 && demangled) {
                    result += demangled;
                    std::free(demangled);
                } else {
                    result += mangled;
                }
                std::free(mangled);

                // Смещение внутри символа
                result += " +";
                result += (plus + 1);
            } else {
                // malloc не удался — копируем как есть
                result.append(paren_open + 1, name_len);
            }
        } else {
            result += sym;
        }
        result += '\n';
    }

    std::free(symbols);
    return result;
}

// -----------------------------------------------------------------------
// Обработчики
// -----------------------------------------------------------------------

namespace {

// SIGABRT handler — выводит backtrace и завершает процесс
extern "C" void sigabrt_handler(int signum) {
    std::signal(signum, SIG_DFL);

    std::string bt = backtrace_string();
    // Используем write(2) чтобы не зависеть от FILE
    ::write(STDERR_FILENO, bt.data(), bt.size());
    ::write(STDERR_FILENO, "\n", 1);

    _exit(128 + signum);
}

// terminate_handler — выводит backtrace и завершает процесс
[[noreturn]] void terminate_handler() {
    std::string bt = backtrace_string();
    ::write(STDERR_FILENO, bt.data(), bt.size());
    ::write(STDERR_FILENO, "\n", 1);
    _exit(EXIT_FAILURE);
}

} // anonymous namespace

// -----------------------------------------------------------------------
// install_fault_handler
// -----------------------------------------------------------------------

void install_fault_handler() {
    std::signal(SIGABRT, sigabrt_handler);
    std::set_terminate(terminate_handler);
}

} // namespace trust::utils