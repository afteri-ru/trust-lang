#pragma once

// ЕДИНАЯ точка отчёта диагностики, severity которой задана опцией Options.
// Используется Context::report и компонентами, у которых нет доступа к session/context
// (например types), чтобы не дублировать разбор severity/Ignore.

#include "diag/diag.hpp"
#include "diag/options.hpp"
#include "location/location.hpp"

#include <format>
#include <type_traits>
#include <utility>

namespace trust::diag {

template <typename DiagT, typename... Args>
    requires std::is_enum_v<DiagT>
void reportWithOptions(DiagnosticEngine& diag, const Options& opts, DiagT kind, MapperRange range, std::format_string<Args...> fmt, Args&&... args) {
    const Severity sev = opts.get(kind);
    if (sev == Severity::Ignore) {
        return;
    }
    diag.report(sev, range, std::move(fmt), std::forward<Args>(args)...);
}

} // namespace trust::diag
