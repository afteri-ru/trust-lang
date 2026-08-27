#include "semantic/pass_runner.hpp"
#include "semantic/symbol_collector.hpp"
#include "semantic/symbol_index.hpp"
#include "ast/ast_nodes.hpp"
#include "ast/ident_name.hpp"
#include "ast/token_type.hpp"
#include "diag/context.hpp"
#include "diag/diag.hpp"
#include "types/registry.hpp"
#include "types/type_names.hpp"
#include "syntax/term.h"
#include "utils/io.hpp"
#include "gtest/gtest.h"

#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

namespace trust {
namespace {

class SymbolCollectorTest : public ::testing::Test {
  protected:
    void SetUp() override {
        m_stream.str("");
        m_prev_err = setErrs(&m_stream);
        m_types = std::make_unique<TypeRegistry>(m_ctx.diag(), m_ctx.opts());
        m_ctx.setTypes(m_types.get());
        m_ctx.opts().set_enabled(semantic::FlagKind::Symbols, true);
    }
    void TearDown() override { setErrs(m_prev_err); }

    std::ostream* m_prev_err = nullptr;
    std::ostringstream m_stream;
    Context m_ctx;
    std::unique_ptr<TypeRegistry> m_types;
};

TEST_F(SymbolCollectorTest, CollectsTypedAndInferred) {
    // x : Int32 := 42;  (явный тип)
    auto x = std::make_shared<VarDecl>(Term::Create(TermID::NAME, "x", {}, parser::token_type::NAME), std::make_shared<IdentType>("Int32"),
                                       std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "42"));
    // y := 42;  (инференс)
    auto y = std::make_shared<VarDecl>(Term::Create(TermID::NAME, "y", {}, parser::token_type::NAME), nullptr,
                                       std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "42"));

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(x));
    seq.push_back(std::move(y));

    SemanticPassRunner runner(m_ctx);
    runner.run(seq);

    EXPECT_TRUE(m_ctx.opts().is_enabled(semantic::FlagKind::Symbols));
    EXPECT_EQ(runner.analysis().symbols().globalSize(), 2u); // семантика обработала обе переменные

    const auto& idx = runner.analysis().symbolIndex();
    ASSERT_EQ(idx.size(), 2u);

    auto it = std::find_if(idx.begin(), idx.end(), [](const SymbolInfo& s) { return s.name == "x"; });
    ASSERT_NE(it, idx.end());
    EXPECT_EQ(it->typeName, "Int32");
    EXPECT_NE(it->type, INVALID_TYPE_ID);

    auto it2 = std::find_if(idx.begin(), idx.end(), [](const SymbolInfo& s) { return s.name == "y"; });
    ASSERT_NE(it2, idx.end());
    EXPECT_NE(it2->type, INVALID_TYPE_ID);
    EXPECT_FALSE(it2->typeName.empty());
}

TEST_F(SymbolCollectorTest, FlagDisabledCollectsNothing) {
    m_ctx.opts().set_enabled(semantic::FlagKind::Symbols, false);
    auto x = std::make_shared<VarDecl>(Term::Create(TermID::NAME, "x", {}, parser::token_type::NAME), nullptr,
                                       std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "42"));
    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(x));
    SemanticPassRunner runner(m_ctx);
    runner.run(seq);
    EXPECT_TRUE(runner.analysis().symbolIndex().empty());
}

} // namespace
} // namespace trust
