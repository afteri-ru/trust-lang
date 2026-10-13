// include/utils/proc/time.hpp
// Текущее время в ISO-подобном виде — единая точка для шапок автогенерируемых файлов и метаданных.
#pragma once

#include <string>

namespace trust::utils {

/// Текущее время в ISO-стиле ("%Y-%m-%d %H:%M:%S %z").
std::string currentTimestamp();

/// Текущая дата ("%Y-%m-%d") — стабильная в пределах суток шапка генерируемых файлов
/// (иначе повторные сборки неизменного кода теряли бы ccache-кэш).
std::string currentDate();

} // namespace trust::utils
