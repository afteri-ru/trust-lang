// trust/io.hpp - I/O helpers for the Trust runtime.
//
// Delivery: package file `include/trust/io.hpp` inside share/trust/stdlib.trust (available via -Istdlib/include).
//
// The backing symbol trust::outs() is provided by the linked trust-runtime
// library (src/utils/io/io.cpp), which is why it is only forward-declared here.

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
