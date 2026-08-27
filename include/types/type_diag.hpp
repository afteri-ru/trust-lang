// include/types/type_diag.hpp
// Внутренний общий помощник отчёта диагностик для нескольких TU реестра типов
// (registry.cpp, structural.cpp). Воспроизводит логику Context::report: severity
// берётся из Options, подавленная диагностика (severity отсутствует) не выводится.
#pragma once

#include "diag/diag.hpp"
#include "diag/options.hpp"
#include "location/location.hpp"
#include <format>

namespace trust {

template <typename DiagT, typename... Args>
inline void reportTypeDiag(DiagnosticEngine& diag, const Options& opts, DiagT kind, MapperRange range, std::format_string<Args...> fmt, Args&&... args) {
    auto sev = opts.get(kind);
    if (!sev.has_value()) {
        return;
    }
    diag.report(*sev, range, std::move(fmt), std::forward<Args>(args)...);
}

} // namespace trust
