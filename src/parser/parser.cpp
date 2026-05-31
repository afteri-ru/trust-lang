#include "parser/parser.hpp"
#include "parser.tab.hh"
#include "ast/token.hpp"
#include "ast/token_info.hpp"
#include "types/buildin.hpp"
#include <cstddef>
#include <string>

namespace trust {

int yylex(ParserAST::semantic_type* yylval, ParserContext& pc) {
    if (pc.pos >= pc.ts.size()) {
        return 0; // EOF
    }
    const TokenInfo* token = pc.ts[pc.pos].get();
    auto kind = token->kind;
    *yylval = pc.ts[pc.pos++];
    return static_cast<int>(kind);
}

void ParserAST::error(const std::string& msg) {
    // Error handling - can be extended to use diag context
}

std::string TokenInfo::dump(const TokenInfo* ptr, size_t indent) {
    std::string result = std::string(indent, ' ');
    if (!ptr) {
        result += "<nullptr>";
        return result;
    }
    if (ptr->kind != ParserToken::Kind::END) {
        result += "'";
        result += ptr->text;
        result += "':";
    }
    result += ParserToken::name(ptr->kind);
    if (ptr->m_left) {
        result += "\n";
        result += std::string(indent, ' ');
        result += "left->(";
        result += dump(ptr->m_left.get(), indent + 2);
        result += ") ";
    }
    if (ptr->m_right) {
        result += "\n";
        result += std::string(indent, ' ');
        result += "right->(";
        result += dump(ptr->m_right.get(), indent + 2);
        result += ") ";
    }
    if (!ptr->m_sequence.empty()) {
        result += "\n";
        result += std::string(indent, ' ');
        for (size_t i = 0; i < ptr->m_sequence.size(); i++) {
            result += dump(ptr->m_sequence[i].get(), indent + 2);
            if (i + 1 < ptr->m_sequence.size()) {
                result += std::string(indent, ' ');
                result += "\n ";
            }
        }
    }
    return result;
}

} // namespace trust