// CLI-команда форматирования: --format / --format-check.
#pragma once

#include "driver/options.hpp"

#include <string>
#include <vector>

namespace trust {

class Context;

/// Форматирует opts.input_file (или проверяет формат при opts.format_check).
/// keywords - эффективный список (CLI > .trust-format > dsl), вычисляет вызывающий.
int runFormatCommand(Context& ctx, const PipelineOpts& opts, const std::vector<std::string>& keywords);

} // namespace trust
