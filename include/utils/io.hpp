#pragma once

#include <ostream>

namespace trust {

/// Возвращает глобальный stdout-поток (по умолчанию std::cout).
/// Может быть переназначен через setOuts().
std::ostream& outs();

/// Возвращает глобальный stderr-поток (по умолчанию std::cerr).
/// Может быть переназначен через setErrs().
std::ostream& errs();

/// Переназначает глобальный stdout-поток. Возвращает предыдущее значение.
std::ostream* setOuts(std::ostream* os);

/// Переназначает глобальный stderr-поток. Возвращает предыдущее значение.
std::ostream* setErrs(std::ostream* os);

} // namespace trust