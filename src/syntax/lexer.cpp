#include "syntax/lexer.h"

namespace trust {

Scanner::Scanner(trust::Context& ctx, trust::MapperFile src)
: NewLangFlexLexer(nullptr, &std::cout)
, m_ctx(ctx)
, m_srcIdx(src)
, m_offset(0)
, m_current_pos(0)
, m_macro_count(0)
, m_macro_del(0) {
    //        yy_flex_debug = true;
}

Scanner::~Scanner() {
}

int Scanner::LexerInput(char* buf, int max_size) {
    auto source = m_ctx.source().source(m_srcIdx);
    if (m_offset >= static_cast<int>(source.size())) {
        return 0;
    }
    int n = std::min(max_size, static_cast<int>(source.size()) - m_offset);
    std::memcpy(buf, source.data() + m_offset, n);
    m_offset += n;
    return n;
}

void Scanner::braceMismatchError(TermID expected, TermID got) {
    std::string msg = std::string("Brace mismatch: expected '") + trust::toString(expected) + "' but got '" + trust::toString(got) +
                      "' (stack depth: " + std::to_string(m_braceStack.size()) + ")";
    auto loc = trust::MapperLocation::makeLoc(m_srcIdx, static_cast<size_t>(std::max(1, m_current_pos + 1)));
    m_ctx.diag().report(trust::Severity::Error, trust::MapperRange(loc, loc), "{}", msg);
}

SequenceType Scanner::ParseLexem(trust::Context& ctx, const std::string str) {
    SequenceType result;
    trust::MapperFile src = ctx.source().add_source("parselexem", str);
    Scanner lexer(ctx, src);

    TermPtr tok;
    while (lexer.lex(&tok) != parser::token::END) {
        result.push_back(tok);
    }
    return result;
}

} // namespace trust

#ifdef yylex
#undef yylex
#endif

int NewLangFlexLexer::yylex() {
    std::cerr << "in NewLangFlexLexer::yylex() !" << std::endl;
    return 0;
}

/* When the scanner receives an end-of-file indication from YY_INPUT, it then
 * checks the yywrap() function. If yywrap() returns false (zero), then it is
 * assumed that the function has gone ahead and set up `yyin' to point to
 * another input file, and scanning continues. If it returns true (non-zero),
 * then the scanner terminates, returning 0 to its caller. */

int NewLangFlexLexer::yywrap() {
    return 1;
}