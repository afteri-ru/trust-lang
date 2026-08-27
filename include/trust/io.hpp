// trust/io.hpp - I/O helpers for the Trust runtime.
//
// Public runtime header: self-contained (standard headers only) so that
// generated C++ programs can include it without depending on the compiler's
// include tree. At build time it is embedded into trust-runtime.so/.a (via
// #embed, in an ELF section named "trust/io.hpp"); the pipeline extracts it
// into a temporary `trust/` directory and links the runtime library when a
// generated program actually uses one of its symbols (trust::trust__print__).
//
// The backing symbol trust::outs() is provided by the linked trust-runtime
// library (src/utils/io.cpp), which is why it is only forward-declared here.

#pragma once

#include <format>
#include <ostream>
#include <utility>

namespace trust {

/// Глобальный stdout-поток (предоставляется trust-runtime библиотекой).
std::ostream& outs();

/// Форматирует аргументы в стиле std::format и выводит результат в trust::outs().
/// Бэкенд для DSL-макроса `print(fmt, args...)`. Имя `trust__print__` (а не
/// `print`) выбрано по образцу `trust__abort__`/`trust__assert__`, чтобы тело
/// макроса `print(...)` не пересекалось с нативным именем в replacement.
template <typename... Args>
void trust__print__(std::format_string<Args...> fmt, Args&&... args) {
    outs() << std::format(fmt, std::forward<Args>(args)...);
}

} // namespace trust
