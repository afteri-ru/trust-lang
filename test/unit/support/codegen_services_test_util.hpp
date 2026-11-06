#ifndef CODEGEN_SERVICES_TEST_UTIL_HPP
#define CODEGEN_SERVICES_TEST_UTIL_HPP
// Тестовая сборка порта кодогенерации `CodegenServices` из `Context` (R12/R39): кодоген в
// проде получает порты от оркестратора (`Pipeline`), в тестах их собирает эта функция.
#include "session/context.hpp"
#include "transpiler/emit/emit_ctx.hpp"

namespace trust {

[[nodiscard]] inline CodegenServices codegenServices(Context& ctx) {
    return CodegenServices{ctx.readTypes(), ctx.diag(), ctx.source(), ctx.attrs(), ctx.opts(), ctx.declCppFacts(), ctx.target(), ctx.moduleResolver()};
}

} // namespace trust

#endif
