// Классификация токенов и разбор зазоров (Gap) между токенами форматтера.

#include "formatter/impl/format_model.hpp"

namespace trust::formatter {

bool isOpen(TermID id) {
    return id == TermID::LBRACE || id == TermID::LPAREN || id == TermID::LBRACKET;
}
bool isClose(TermID id) {
    return id == TermID::RBRACE || id == TermID::RPAREN || id == TermID::RBRACKET;
}

bool isBinaryOperator(TermID id) {
    switch (id) {
    case TermID::EQ:
    case TermID::PLUS:
    case TermID::MINUS:
    case TermID::STAR:
    case TermID::SLASH:
    case TermID::AMP:
    case TermID::PIPE:
    case TermID::CARET:
    case TermID::LT:
    case TermID::GT:
    case TermID::ASSIGN:
    case TermID::APPEND:
    case TermID::SWAP:
    case TermID::CREATE_TYPE:
    case TermID::CREATE_NAME:
    case TermID::ALIAS_NAME:
    case TermID::ALIAS_TYPE:
    case TermID::OP_MATH:
    case TermID::OP_ASSIGN:
    case TermID::OP_COMPARE:
    case TermID::OP_BITWISE:
    case TermID::OP_LOGICAL:
    case TermID::RANGE:
    case TermID::ELLIPSIS:
        return true;
    default:
        return false;
    }
}

bool isUnaryOperator(TermID id) {
    switch (id) {
    case TermID::BANG:         // !
    case TermID::TILDE:        // ~
    case TermID::TAKE:         // * (deref)
    case TermID::OPERATOR_PTR: // & (ptr)
    case TermID::AT:           // @
        return true;
    default:
        return false;
    }
}

bool isDoc(TermID id) {
    return id == TermID::DOCUMENT;
}
bool isDocInline(TermID id) {
    return id == TermID::DOCUMENT_INLINE;
}

bool isNameLike(TermID id) {
    switch (id) {
    case TermID::NAME:
    case TermID::LOCAL:
    case TermID::MACRO:
    case TermID::MODULE:
    case TermID::INTEGER:
    case TermID::NUMBER:
    case TermID::COMPLEX:
    case TermID::RATIONAL:
    case TermID::STRWIDE:
    case TermID::STRCHAR:
    case TermID::REFLECTION:
    case TermID::ARGUMENT:
    case TermID::DOLLAR:
    case TermID::RPAREN:
    case TermID::RBRACKET:
        return true;
    default:
        return false;
    }
}

bool isNumericLiteral(TermID id) {
    switch (id) {
    case TermID::INTEGER:
    case TermID::NUMBER:
    case TermID::COMPLEX:
    case TermID::RATIONAL:
        return true;
    default:
        return false;
    }
}

void splitNegativeLiterals(std::vector<Token>& toks) {
    for (size_t i = 1; i < toks.size(); ++i) {
        const Token& t = toks[i];
        if (!t.text.empty() && t.text[0] == '-' && isNumericLiteral(t.id) && isNameLike(toks[i - 1].id)) {
            Token minus = t;
            minus.id = TermID::MINUS;
            minus.text = "-";
            minus.end = t.begin + 1;

            Token literal = t;
            literal.begin += 1;
            literal.text = t.text.substr(1);

            toks[i] = std::move(literal);
            toks.insert(toks.begin() + static_cast<std::ptrdiff_t>(i), std::move(minus));
            ++i; // пропускаем новый MINUS (и литерал — инкремент цикла)
        }
    }
}

Gap parseGap(std::string_view s) {
    Gap g;
    size_t i = 0;
    const size_t n = s.size();
    while (i < n) {
        const char c = s[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            size_t j = i;
            while (j < n && (s[j] == ' ' || s[j] == '\t' || s[j] == '\r' || s[j] == '\n')) {
                if (s[j] == '\n') {
                    ++g.newlines;
                }
                ++j;
            }
            g.segs.push_back({SegKind::Whitespace, std::string(s.substr(i, j - i))});
            i = j;
        } else if (c == '#') {
            // Строковый комментарий #... (## — отдельный токен DOCUMENT, в зазор не попадает).
            size_t j = i;
            while (j < n && s[j] != '\n') {
                ++j;
            }
            g.segs.push_back({SegKind::LineComment, std::string(s.substr(i, j - i))});
            i = j;
        } else if (c == '/' && i + 1 < n && s[i + 1] == '*') {
            size_t j = i + 2;
            while (j + 1 < n && !(s[j] == '*' && s[j + 1] == '/')) {
                ++j;
            }
            j = (j + 2 <= n) ? j + 2 : n;
            g.segs.push_back({SegKind::BlockComment, std::string(s.substr(i, j - i))});
            i = j;
        } else {
            g.verbatim = true;
            g.segs.clear();
            return g;
        }
    }
    return g;
}

} // namespace trust::formatter