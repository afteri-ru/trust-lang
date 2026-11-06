// Форматирование времени — единая точка для шапок артефактов и предопределённых макросов.
#pragma once

#include <ctime>
#include <string>
#include <string_view>

namespace trust::utils {

/// Время `timestamp` в локальной зоне по формату `strftime`. Размер буфера подбирается по факту;
/// формат, не поместившийся в предел, — FAULT (обрезанная строка не является результатом).
[[nodiscard]] std::string formatTimestamp(std::time_t timestamp, std::string_view format);

/// Текущая дата ("%Y-%m-%d") — стабильная в пределах суток шапка генерируемых файлов
/// (иначе повторные сборки неизменного кода теряли бы ccache-кэш и байт-детерминизм артефактов).
[[nodiscard]] std::string currentDate();

} // namespace trust::utils
