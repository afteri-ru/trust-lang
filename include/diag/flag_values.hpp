#pragma once

// include/diag/flag_values.hpp
// Единый разбор значений поведенческих опций (`-f<name>`/`-fno-<name>` и value-опций
// `-f<name>=<value>`/`--<name>=<value>`). Драйвер передаёт boolean-значение как
// ""/"on" (включено) или "off" (выключено); иное значение - ошибка (no silent fallback).
// Используется в applyOption (driver/cli.cpp) и в валидаторах реестра флагов
// (transpiler/semantic) для пути @__OPTION__.
#include <charconv>
#include <cstddef>
#include <optional>
#include <string_view>
#include <system_error>

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

/// Неотрицательное десятичное значение (напр. `-ferror-limit=<N>`). Пустая строка,
/// знак, нецифры и переполнение -> false (без тихого приведения).
[[nodiscard]] inline bool parseSizeValue(std::string_view text, std::size_t& out) noexcept {
    if (text.empty()) {
        return false;
    }
    std::size_t value = 0;
    const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc{} || ptr != text.data() + text.size()) {
        return false;
    }
    out = value;
    return true;
}

} // namespace trust
