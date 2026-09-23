#pragma once

// include/diag/flag_values.hpp
// Единый разбор значений boolean-поведенческих флагов (компиляторных `-f<name>`/`-fno-<name>`).
// Драйвер передаёт значение как ""/"on" (включено) или "off" (выключено); иное значение -
// ошибка (no silent fallback). Используется и в applyOption (pipeline/cli.cpp), и в валидаторах
// реестра флагов (transpiler/semantic) для пути @__OPTION__.
#include <optional>
#include <string_view>

namespace trust {

/// ""|"on" -> true; "off" -> false; иное -> nullopt (недопустимое значение).
[[nodiscard]] inline std::optional<bool> parseBoolFlagValue(std::string_view v) noexcept {
    if (v.empty() || v == "on") {
        return true;
    }
    if (v == "off") {
        return false;
    }
    return std::nullopt;
}

} // namespace trust
