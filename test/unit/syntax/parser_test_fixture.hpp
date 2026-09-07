#ifndef PARSER_TEST_FIXTURE_HPP
#define PARSER_TEST_FIXTURE_HPP
// Shared ParserTest fixture for parser unit tests
// (parser_test.cpp, parser_stmt_test.cpp, parser_expr_test.cpp,
//  parser_decl_test.cpp, parser_trust_test.cpp).
#include "syntax/warning_push.h"
#include <gtest/gtest.h>
#include "syntax/warning_pop.h"

#include "ast/ast_nodes.hpp"
#include "ast/attr_builtin.hpp"
#include "ast/token_type.hpp"
#include "ast/term_to_ast.hpp"
#include "syntax/parser.h"
#include "syntax/term.h"
#include "trust/version.h"
#include "syntax/macro.h"
#include "module_loader/module_loader.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

using namespace trust;

namespace {
// Рекурсивный подсчёт DOCUMENT-термов в дереве Term (используется doc-тестами).
inline int countDocTerms(const trust::TermPtr& t) {
    if (!t) {
        return 0;
    }
    int n = (t->getTermID() == TermID::DOCUMENT) ? 1 : 0;
    for (const auto& c : t->m_sequence) {
        n += countDocTerms(c);
    }
    if (t->m_left) {
        n += countDocTerms(t->m_left);
    }
    if (t->m_right) {
        n += countDocTerms(t->m_right);
    }
    if (t->m_args) {
        for (const auto& [name, v] : *t->m_args) {
            (void)name, n += countDocTerms(v);
        }
    }
    return n;
}

// Рекурсивный подсчёт доков, привязанных грамматикой к термам-идентификаторам (m_docs).
inline int countDeclDocs(const trust::TermPtr& t) {
    if (!t) {
        return 0;
    }
    int n = static_cast<int>(t->m_docs.size());
    for (const auto& c : t->m_sequence) {
        n += countDeclDocs(c);
    }
    if (t->m_left) {
        n += countDeclDocs(t->m_left);
    }
    if (t->m_right) {
        n += countDeclDocs(t->m_right);
    }
    if (t->m_args) {
        for (const auto& [name, v] : *t->m_args) {
            (void)name, n += countDeclDocs(v);
        }
    }
    return n;
}
} // namespace

class ParserTest : public ::testing::Test {
  protected:
    trust::Context m_ctx;
    std::unique_ptr<ModuleLoader> m_loader;
    std::vector<std::string> m_postlex;

    std::string m_output;

    void SetUp() {
        m_ctx.diag().clear();
        // Парсер может обрабатывать import-модули через ctx.loader().
        m_loader = std::make_unique<ModuleLoader>(m_ctx);
        m_ctx.setLoader(m_loader.get());
    }

    void TearDown() {}

    TermPtr Parse(std::string str, MacroPtr buffer = nullptr) {
        m_postlex.clear();
        if (buffer) {
            m_ctx.setMacro(buffer);
        }
        Parser p(m_ctx, &m_postlex);
        ast = p.ParseText(str);
        return ast;
    }

    int Count(TermID token_id) {
        int result = 0;
        for (int c = 0; c < ast->size(); c++) {
            if (ast->at(c).second->m_id == token_id) {
                result++;
            }
        }
        return result;
    }

    std::string LexOut() {
        std::string result;
        for (int i = 0; i < m_postlex.size(); i++) {
            if (!result.empty()) {
                result += " ";
            }
            result += m_postlex[i];
        }
        trim(result);
        return result;
    }

    TermPtr ast;
};

#endif // PARSER_TEST_FIXTURE_HPP
