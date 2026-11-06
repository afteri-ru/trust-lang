#pragma once

// Публичный API слоя доказательств: разрешение режима гейта из diag::Options и
// проверка отчёта (`--verify=all`: непроверенное обязательство — ошибка).

#include "diag/options.hpp"
#include "verify/model/obligation.hpp"

#include <optional>
#include <string_view>

namespace trust {
namespace verify {

/// Режим гейта доказательств. `nullopt` — флаг `--verify` не задан (гейт выключен).
enum class VerifyMode {
    kAll, ///< `--verify=all`: все обязательства обязаны быть доказаны
};

/// Разбор значения `--verify`. Неизвестное значение — `nullopt` (без тихого fallback: регистратор
/// ставит валидатор, поэтому сюда попадают только допустимые значения).
[[nodiscard]] std::optional<VerifyMode> parseVerifyMode(std::string_view value) noexcept;

/// Режим гейта из уже применённых опций (`nullopt` — не задан).
[[nodiscard]] std::optional<VerifyMode> verifyModeFromOptions(const Options& opts) noexcept;

} // namespace verify
} // namespace trust
