// Bootstrap фронтенда: загрузка DSL-макросов и эффективные keywords.
#pragma once

#include "ast/ast_nodes.hpp"
#include "driver/options.hpp"

#include <string>
#include <vector>

namespace trust {

class Context;

/// Загружает DSL-макросы в Context (stdlib/dsl.src по умолчанию, --dsl <file> заменяет,
/// --no-dsl отключает). Загружается один раз; повторные вызовы no-op.
void loadDslMacros(Context& ctx, const PipelineOpts& opts);

/// Возвращает эффективный список keywords (CLI --keywords > .trust-format > дефолт dsl.src).
std::vector<std::string> effectiveKeywords(Context& ctx, const PipelineOpts& opts);

} // namespace trust
