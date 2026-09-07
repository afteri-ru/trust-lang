#ifndef TRANSPILER_TEST_FIXTURE_HPP
#define TRANSPILER_TEST_FIXTURE_HPP
// Shared TranspilerTest fixture for transpiler codegen unit tests
// (codegen_literal_test.cpp, codegen_stmt_test.cpp, codegen_func_test.cpp).
#include "utils/io.hpp"
#include "transpiler/transpiler.hpp"
#include "transpiler/diag.hpp"
#include "semantic/pass_runner.hpp"
#include "pipeline/pipeline.hpp"
#include "ast/ast_nodes.hpp"
#include "ast/attr_builtin.hpp"
#include "ast/ident_name.hpp"
#include "ast/token.hpp"
#include "ast/token_type.hpp"
#include "diag/context.hpp"
#include "types/registry.hpp"
#include "syntax/term.h"
#include "syntax/term_types.h"
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
        m_types = std::make_unique<TypeRegistry>(m_ctx.diag(), m_ctx.opts());
        m_ctx.setTypes(m_types.get());
    }

    void TearDown() override { setErrs(m_prev_err); }

    std::ostream* m_prev_err = nullptr;
    std::ostringstream m_stream;
    Context m_ctx;
    std::unique_ptr<TypeRegistry> m_types;
};

} // namespace trust
#endif // TRANSPILER_TEST_FIXTURE_HPP
