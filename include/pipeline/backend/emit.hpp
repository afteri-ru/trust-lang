// include/pipeline/backend/emit.hpp
// Вывод результата emit-режимов (tokens/ast/macros) в stdout.
#pragma once

#include "pipeline/run/types.hpp"

namespace trust {

class Context;
struct PipelineOpts;

/// Печатает результат согласно opts.emit_flags. FAULT для неподдержанной комбинации.
int printEmitOutput(const PipelineOpts& opts, Context& ctx, const PipelineResult& result);

} // namespace trust
