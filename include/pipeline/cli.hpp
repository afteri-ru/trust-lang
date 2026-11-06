// Разбор и валидация командной строки драйвера `trust`.
#pragma once

#include "driver/options.hpp"

#include <span>

namespace trust {

/// Полный разбор argv: arity-aware опции + пост-валидация. Ошибки печатаются в errs(),
/// exit_code выставляется в ParseResult.
ParseResult parseCommandLine(std::span<char*> argv);

/// true, если прогон завершается без запуска конвейера (help/version/ошибка разбора).
bool isSpecialExit(const ParseResult& r);

} // namespace trust
