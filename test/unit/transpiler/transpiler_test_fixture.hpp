#ifndef TRANSPILER_TEST_FIXTURE_HPP
#define TRANSPILER_TEST_FIXTURE_HPP
// Shared TranspilerTest fixture for transpiler codegen unit tests
// (codegen_literal_test.cpp, codegen_stmt_test.cpp, codegen_func_test.cpp).
#include "utils/utils.hpp"
#include "transpiler/transpiler.hpp"
#include "transpiler/emit/diag.hpp"
#include "transpiler/builtin_string_impl_test_util.hpp"
#include "transpiler/builtin_rational_impl_test_util.hpp"
#include "transpiler/builtin_dict_impl_test_util.hpp"
#include "transpiler/builtin_container_impl_test_util.hpp"
#include "transpiler/builtin_mem_impl_test_util.hpp"
#include "transpiler/builtin_interrupt_impl_test_util.hpp"
#include "transpiler/builtin_any_impl_test_util.hpp"
#include "semantic/core/pass_runner.hpp"
#include "ir/hir_module.hpp"
#include "pipeline/pipeline.hpp"
#include "support/cpp_facts_test_util.hpp"
#include "ast/ast_nodes.hpp"
#include "attrs/attr_builtin.hpp"
#include "ast/ident_name.hpp"
#include "ast/token.hpp"
#include "ast/token_type.hpp"
#include "session/context.hpp"
#include "target/target.hpp"
#include "types/type_system.hpp"
#include "support/codegen_services_test_util.hpp"
#include "syntax/term/term.h"
#include "syntax/term/term_types.h"
#include "gtest/gtest.h"

#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <vector>

namespace trust {

class TranspilerTest : public ::testing::Test {
  protected:
    void SetUp() override {
        m_stream.str("");
        m_prev_err = setErrs(&m_stream);
        m_types = std::make_unique<TypeSystem>(m_ctx.diag(), m_ctx.opts());
        m_ctx.setTypes(m_types.get());
        // Целевой контракт прогона (вне языковой модели) — как в боевом `Pipeline`.
        m_target = std::make_unique<TargetContract>();
        m_ctx.setTarget(m_target.get());
        // Seed целевых C++-фактов встроенных интринсиков (как `Pipeline`).
        test::seedIntrinsicCppFacts(*m_target, m_ctx.readTypes());
        // Реализации StrChar/StrWide и Rational/BigInteger (в реальной сборке — stdlib/builtin/*.src).
        seedStringRuntimeImpl(m_ctx);
        seedRationalRuntimeImpl(m_ctx);
        seedDictRuntimeImpl(m_ctx);
        seedArrayRuntimeImpl(m_ctx);
        seedRangeRuntimeImpl(m_ctx);
        seedMemRuntimeImpl(m_ctx);
        seedInterruptRuntimeImpl(m_ctx);
        seedAnyRuntimeImpl(m_ctx);
    }

    void TearDown() override { setErrs(m_prev_err); }

    /// HIR для кодогенерации: AST после анализа остаётся неизменённой, lowering выполняет
    /// HIR-билдер (клон + понижение). Возвращённый модуль держит построенное дерево живым.
    [[nodiscard]] HirModule hirOf(const std::vector<AstNodePtr>& analyzedAst) {
        // Семантическая фаза могла сбросить TypeSystem (reset() чистит per-instance определения
        // BuiltinRegistry), поэтому реализацию StrChar/StrWide пере-рееваем ПЕРЕД построением HIR.
        seedStringRuntimeImpl(m_ctx);
        seedRationalRuntimeImpl(m_ctx);
        seedDictRuntimeImpl(m_ctx);
        seedArrayRuntimeImpl(m_ctx);
        seedRangeRuntimeImpl(m_ctx);
        seedMemRuntimeImpl(m_ctx);
        seedInterruptRuntimeImpl(m_ctx);
        seedAnyRuntimeImpl(m_ctx);
        return buildHir(analyzedAst, m_ctx, *m_types);
    }

    std::ostream* m_prev_err = nullptr;
    std::ostringstream m_stream;
    Context m_ctx;
    std::unique_ptr<TypeSystem> m_types;
    std::unique_ptr<TargetContract> m_target;
};

} // namespace trust
#endif // TRANSPILER_TEST_FIXTURE_HPP
