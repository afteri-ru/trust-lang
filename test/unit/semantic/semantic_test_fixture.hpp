#ifndef SEMANTIC_TEST_FIXTURE_HPP
#define SEMANTIC_TEST_FIXTURE_HPP
// Shared fixtures for semantic unit tests
// (semantic_test.cpp, semantic_table_test.cpp, semantic_funcdecl_test.cpp).
#include "utils/io.hpp"
#include "semantic/pass_runner.hpp"
#include "semantic/name_resolution.hpp"
#include "semantic/inline_hook.hpp"
#include "semantic/symbol_table.hpp"
#include "ast/ast_nodes.hpp"
#include "ast/attr_builtin.hpp"
#include "ast/ident_name.hpp"
#include "ast/token.hpp"
#include "ast/token_type.hpp"
#include "diag/context.hpp"
#include "diag/diag.hpp"
#include "types/registry.hpp"
#include "types/type_names.hpp"
#include "syntax/term.h"
#include "syntax/term_types.h"
#include "gtest/gtest.h"

#include <memory>
#include <ostream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace trust {

class ErrsFixture : public ::testing::Test {
  protected:
    void SetUp() override {
        m_stream.str("");
        m_prev_err = setErrs(&m_stream);
        m_types = std::make_unique<TypeRegistry>(m_ctx.diag(), m_ctx.opts());
        m_ctx.setTypes(m_types.get());
    }

    void TearDown() override { setErrs(m_prev_err); }

    std::ostream* m_prev_err = nullptr;
    std::ostringstream m_stream;
    Context m_ctx;
    std::unique_ptr<TypeRegistry> m_types;
};

class SemanticTest : public ErrsFixture {};
class SymbolTableTest : public ErrsFixture {};
class FuncDeclTest : public ErrsFixture {};

} // namespace trust
#endif // SEMANTIC_TEST_FIXTURE_HPP
