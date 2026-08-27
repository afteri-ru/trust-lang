#ifndef LEXER_TEST_FIXTURE_HPP
#define LEXER_TEST_FIXTURE_HPP
// Shared Lexer fixture for lexer unit tests (lexer_test.cpp, lexer_macro_test.cpp,
// lexer_trust_test.cpp). Tokenizes a source string via Scanner and exposes helpers.
#include "syntax/warning_push.h"
#include <gtest/gtest.h>
#include "syntax/warning_pop.h"

#include "syntax/term.h"
#include "syntax/lexer.h"
#include "syntax/parser.h"
#include "syntax/macro.h"

#include <string>
#include <vector>

using namespace trust;

class Lexer : public ::testing::Test {
  protected:
    std::vector<TermPtr> tokens;
    trust::Context ctx;

    void SetUp() { ctx.diag().clear(); }

    void TearDown() {}

    int64_t TokenParse(const char* str) {
        trust::MapperFile src = ctx.source().add_source("test", str);

        Scanner lexer(ctx, src);

        tokens.clear();
        TermPtr tok;
        while (lexer.lex(&tok) != parser::token::END) {
            tokens.push_back(tok);
        }
        return tokens.size();
    }

    int Count(TermID token_id) {
        int result = 0;
        for (size_t i = 0; i < tokens.size(); i++) {
            if (tokens[i]->getTermID() == token_id) {
                result++;
            }
        }
        return result;
    }

    std::string Dump() {
        std::string result;
        for (int i = 0; i < tokens.size(); i++) {
            result += tokens[i]->getText();
            result += ":";
            result += toString(tokens[i]->m_id);
            result += " ";
        }
        return result;
    }
};

#endif // LEXER_TEST_FIXTURE_HPP
