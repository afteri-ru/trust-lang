#ifndef TRANSPILER_TEST_FIXTURE_HPP
#define TRANSPILER_TEST_FIXTURE_HPP
// Shared TranspilerTest fixture for transpiler codegen unit tests
// (codegen_literal_test.cpp, codegen_stmt_test.cpp, codegen_func_test.cpp).
#include "utils/utils.hpp"
#include "transpiler/transpiler.hpp"
#include "transpiler/emit/diag.hpp"
#include "semantic/core/pass_runner.hpp"
#include "ir/hir_module.hpp"
#include "pipeline/pipeline.hpp"
#include "ast/ast_nodes.hpp"
#include "attrs/attr_builtin.hpp"
#include "ast/ident_name.hpp"
#include "ast/token.hpp"
#include "ast/token_type.hpp"
#include "session/context.hpp"
#include "location/module_name.hpp"
#include "types/type_system.hpp"
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
    /// Юнит-тесты строят AST вручную (без загрузчика): модуль объявления — «неимпортируемый»
    /// (пустой C++-префикс), поэтому генерируемые имена не меняются.
    class StubModuleNames : public ModuleNameLookup {
      public:
        [[nodiscard]] std::string_view moduleLogicalName(ModuleIndex) const override { return "test"; }
    };

    void SetUp() override {
        m_stream.str("");
        m_prev_err = setErrs(&m_stream);
        m_types = std::make_unique<TypeSystem>(m_ctx.diag(), m_ctx.opts());
        m_types->setModuleNames(&m_moduleNames);
        m_ctx.setTypes(m_types.get());
    }

    void TearDown() override { setErrs(m_prev_err); }

    /// HIR для кодогенерации: AST после анализа остаётся неизменённой, lowering выполняет
    /// HIR-билдер (клон + понижение). Возвращённый модуль держит построенное дерево живым.
    [[nodiscard]] HirModule hirOf(const std::vector<AstNodePtr>& analyzedAst) { return buildHir(analyzedAst, m_ctx); }

    std::ostream* m_prev_err = nullptr;
    std::ostringstream m_stream;
    Context m_ctx;
    StubModuleNames m_moduleNames;
    std::unique_ptr<TypeSystem> m_types;
};

} // namespace trust
#endif // TRANSPILER_TEST_FIXTURE_HPP
