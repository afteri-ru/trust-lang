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

bool Scanner::macroSeqAction(TermPtr& out) {
    if (m_macro_count == 0) {
        m_macro_count = 1;
        m_inMacroSeq = true;
    } else if (m_macro_count == 1) {
        m_macro_count = 2;
    } else {
        LexerError("Nested '@@' inside macro seq body is not allowed");
        return false;
    }
    out = Term::Create(TermID::MACRO_SEQ, parser::token_type::MACRO_SEQ, "@@", 2, currentTokenRange(yyleng));
    return true;
}

bool Scanner::macroStrAction(TermPtr& out) {
    if (m_macro_count == 1) {
        // Открытие текстового тела: вход в state_MACRO_STR, токен не эмитится
        // (лексер продолжает сканирование внутри текстового тела).
        enterMacroStr();
        m_content_begin = m_current_pos;
        return false;
    }
    if (m_macro_count == 2) {
        LexerError("'@@@' inside macro seq body is not allowed: use '@@@@' to close the macro");
        return false;
    }
    LexerError("'@@@' is not allowed outside a macro definition");
    return false;
}

bool Scanner::macroDelAction(TermPtr& out) {
    if (m_macro_count == 1) {
        // Удаление макроса: `@@ имя @@@@`.
        m_macro_count = 0;
        m_inMacroSeq = false;
        out = Term::Create(TermID::MACRO_DEL, parser::token_type::MACRO_DEL, "@@@@", 4, currentTokenRange(yyleng));
        return true;
    }
    if (m_macro_count == 2) {
        // Универсальный терминатор `@@@@`: конец seq-тела.
        m_macro_count = 0;
        m_inMacroSeq = false;
        out = Term::Create(TermID::MACRO_DEL, parser::token_type::MACRO_DEL, "@@@@", 4, currentTokenRange(yyleng));
        return true;
    }
    LexerError("'@@@@' is not allowed outside a macro definition");
    return false;
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

namespace {
// Склейка строк в длинных литералах: обратный слэш '\' в самом конце физической строки,
// непосредственно перед переводом строки (LF или CRLF). Сам '\' и перевод строки
// удаляются, содержимое следующей строки продолжает текущий литерал. Пробелы между '\'
// и переводом строки недопустимы (обрабатывается правилами flex).

bool HasLineContinuation(const char* s, int n) {
    for (int i = 0; i + 1 < n; ++i) {
        if (s[i] == '\\') {
            if (s[i + 1] == '\n') return true;
            if (s[i + 1] == '\r' && i + 2 < n && s[i + 2] == '\n') return true;
        }
    }
    return false;
}

std::string StripLineContinuations(const char* s, int n) {
    std::string out;
    out.reserve(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        if (s[i] == '\\' && i + 1 < n && s[i + 1] == '\n') { ++i; continue; }
        if (s[i] == '\\' && i + 2 < n && s[i + 1] == '\r' && s[i + 2] == '\n') { i += 2; continue; }
        out.push_back(s[i]);
    }
    return out;
}

int CountDecimalDigits(const std::string& s) {
    int c = 0;
    for (char ch : s) {
        if (ch >= '0' && ch <= '9') ++c;
    }
    return c;
}
} // namespace

TermPtr Scanner::buildStringLiteralTerm(TermID id, parser::token_type tok, const char* text, int len) {
    auto src = m_ctx.source().source(m_srcIdx);
    const int idx = static_cast<int>(text - src.data());
    trust::MapperRange rng(m_srcIdx, idx + 1, idx + len + 1);
    if (!HasLineContinuation(text, len)) {
        // без склеек - терм view в исходник (без копии, round-trip не меняется)
        return Term::Create(id, tok, text, static_cast<size_t>(len), rng);
    }
    // со склейками - собираем однострочное содержимое без маркеров '\'+перевод строки
    return Term::Create(id, StripLineContinuations(text, len), rng, tok);
}

TermPtr Scanner::buildNumberTerm(TermID id, parser::token_type tok, const char* /*yytext*/, int len) {
    // ВАЖНО: view-терм обязан указывать на ПЕРСИСТЕНТНЫЙ исходный текст (SourceMapper),
    // а не на временный буфер flex (yytext). Длина совпадения len == yyleng.
    auto src = m_ctx.source().source(m_srcIdx);
    const char* text = src.data() + tokenStartOffset();
    trust::MapperRange rng = currentTokenRange(len);
    if (!HasLineContinuation(text, len)) {
        // без склеек - терм view в исходник
        return Term::Create(id, tok, text, static_cast<size_t>(len), rng);
    }
    std::string cleaned = StripLineContinuations(text, len);
    const int digits = CountDecimalDigits(cleaned);
    if (digits < kMinContinuedDigits) {
        m_ctx.diag().report(Severity::Warning, rng,
            "Numeric literal is split across lines but has only {} digit(s); line continuation is meant for numbers with at least {} digits.",
            digits, kMinContinuedDigits);
    }
    return Term::Create(id, std::move(cleaned), rng, tok);
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