#ifndef MACRO_TEST_FIXTURE_HPP
#define MACRO_TEST_FIXTURE_HPP
// Shared MacroTest fixture for macro/parser unit tests
// (macro_core_test.cpp, macro_options_test.cpp, macro_mapping_test.cpp).
#include "syntax/warning_push.h"
#include <gtest/gtest.h>
#include "syntax/warning_pop.h"

#include "syntax/macro.h"
#include "syntax/lexer.h"
#include "syntax/parser.h"
#include "transpiler/diag.hpp"

#include <string>
#include <vector>

using namespace trust;

class MacroTest : public ::testing::Test {
  protected:
    trust::Context m_ctx;
    std::vector<std::string> m_postlex;

    std::string m_output;

    void SetUp() { m_ctx.diag().clear(); }

    void TearDown() {}

    TermPtr Parse(std::string str, MacroPtr buffer = nullptr, std::string sourceName = "@input") {
        m_postlex.clear();
        if (buffer) {
            m_ctx.setMacro(buffer);
        }
        Parser p(m_ctx, &m_postlex);
        ast = p.ParseText(str, sourceName);
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

#endif // MACRO_TEST_FIXTURE_HPP
