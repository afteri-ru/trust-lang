#include "diag/diag.hpp"
#include "parser/mmproc.hpp"
#include "parser/token_info.hpp"
#include <stdexcept>
#include <format>
#include <string>

namespace trust {

// --- escape / unescape ---

std::string MMProcessor::escape(const std::string &s) {
    std::string out;
    out.reserve(s.size() + 4);
    for (char c : s) {
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        case '\0':
            out += "\\0";
            break;
        default:
            out += c;
            break;
        }
    }
    return out;
}

std::string MMProcessor::unescape(const std::string &s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            switch (s[i + 1]) {
            case '"':
                out += '"';
                i++;
                break;
            case '\\':
                out += '\\';
                i++;
                break;
            case 'n':
                out += '\n';
                i++;
                break;
            case 'r':
                out += '\r';
                i++;
                break;
            case 't':
                out += '\t';
                i++;
                break;
            case '0':
                out += '\0';
                i++;
                break;
            default:
                out += s[i];
                break;
            }
        } else {
            out += s[i];
        }
    }
    return out;
}

// --- Kind classification ---

static bool is_string_token(ParserToken::Kind k) noexcept {
    return k == ParserToken::Kind::STRWIDE || k == ParserToken::Kind::STRCHAR || k == ParserToken::Kind::STRWIDE_RAW || k == ParserToken::Kind::STRCHAR_RAW;
}

static bool is_embed_token(ParserToken::Kind k) noexcept {
    return k == ParserToken::Kind::EMBED;
}

static bool is_concatenatable_token(ParserToken::Kind k) noexcept {
    return is_string_token(k) || is_embed_token(k);
}

// Helper: create TokenPtr with concatenated text and range
static TokenPtr make_concatenatable_token(ParserToken::Kind kind, std::string text, SourceRange range) {
    bool is_raw = (kind == ParserToken::Kind::STRWIDE_RAW || kind == ParserToken::Kind::STRCHAR_RAW);
    if (!is_raw) {
        text = MMProcessor::unescape(text);
    }
    return TokenInfo::make(kind, std::move(text), std::move(range));
}

static bool is_unimplemented_token(ParserToken::Kind k) noexcept {
    switch (k) {
    case ParserToken::Kind::MACRO:
    case ParserToken::Kind::MACRO_ARGCOUNT:
    case ParserToken::Kind::MACRO_ARGNAME:
    case ParserToken::Kind::MACRO_ARGPOS:
    case ParserToken::Kind::MACRO_ARGUMENT:
    case ParserToken::Kind::MACRO_CONCAT:
    case ParserToken::Kind::MACRO_DEL:
    case ParserToken::Kind::MACRO_EXPR_BEGIN:
    case ParserToken::Kind::MACRO_EXPR_END:
    case ParserToken::Kind::MACRO_NAMESPACE:
    case ParserToken::Kind::MACRO_SEQ:
    case ParserToken::Kind::MACRO_STR:
    case ParserToken::Kind::MACRO_TOSTR:
    case ParserToken::Kind::MODULE:
        return true;
    default:
        return false;
    }
}

static bool is_namespace(ParserToken::Kind k) noexcept {
    return k == ParserToken::Kind::NAMESPACE;
}

static bool is_id_start(ParserToken::Kind k) noexcept {
    return k == ParserToken::Kind::NAME || k == ParserToken::Kind::LOCAL || k == ParserToken::Kind::NATIVE;
}

static bool is_id_continuation(ParserToken::Kind k) noexcept {
    return k == ParserToken::Kind::NAME;
}

static bool is_id_terminator(ParserToken::Kind k) noexcept {
    return k == ParserToken::Kind::LOCAL || k == ParserToken::Kind::NATIVE;
}

// --- Process ---

TokenSequence MMProcessor::process(Context &ctx, const LexemeSequence &lexemes) {
    TokenSequence result;
    std::size_t i = 0;

    while (i < lexemes.size()) {
        const Lexeme &lex = lexemes[i];

        if (is_unimplemented_token(lex.kind)) {
            ctx.diag().report(lex.pos, Severity::Error, "unimplemented token '{}' — macro/module processing is not implemented", ParserToken::name(lex.kind));
            ++i;
            continue;
        }

        if (is_concatenatable_token(lex.kind)) {
            std::string text(lex.data(), lex.size());
            SourceRange range{lex.pos, lex.pos};
            ParserToken::Kind kind = lex.kind;

            std::size_t j = i + 1;
            while (j < lexemes.size() && lexemes[j].kind == kind) {
                text.append(lexemes[j].data(), lexemes[j].size());
                range.end = lexemes[j].pos;
                ++j;
            }

            result.push_back(make_concatenatable_token(kind, std::move(text), range));
            i = j;
            continue;
        }

        if (lex.kind == ParserToken::Kind::MANGLED) {
            std::string text(lex.data(), lex.size());
            SourceRange range{lex.pos, lex.pos};
            result.push_back(TokenInfo::make(ParserToken::Kind::Ident, std::move(text), range));
            ++i;
            continue;
        }

        if (is_id_start(lex.kind) || is_namespace(lex.kind)) {
            std::string text;
            SourceRange range{lex.pos, lex.pos};
            bool has_main_part = false;

            std::size_t j = i;

            if (is_namespace(lexemes[j].kind)) {
                text.append(lexemes[j].data(), lexemes[j].size());
                range.end = lexemes[j].pos;
                ++j;
            }

            if (j < lexemes.size() && is_id_start(lexemes[j].kind)) {
                text.append(lexemes[j].data(), lexemes[j].size());
                range.end = lexemes[j].pos;
                has_main_part = true;
                ++j;
            }

            while (j < lexemes.size() && is_namespace(lexemes[j].kind)) {
                std::size_t ns_pos = j;
                ++j;
                if (j < lexemes.size() && is_id_continuation(lexemes[j].kind)) {
                    text.append(lexemes[ns_pos].data(), lexemes[ns_pos].size());
                    text.append(lexemes[j].data(), lexemes[j].size());
                    range.end = lexemes[j].pos;
                    ++j;
                } else {
                    break;
                }
            }

            if (j < lexemes.size() && is_id_terminator(lexemes[j].kind)) {
                text.append(lexemes[j].data(), lexemes[j].size());
                range.end = lexemes[j].pos;
                ++j;
            }

            if (!has_main_part) {
                std::string ns_text(lexemes[i].data(), lexemes[i].size());
                SourceRange ns_range{lexemes[i].pos, lexemes[i].pos};
                result.push_back(TokenInfo::make(ParserToken::Kind::NAMESPACE, std::move(ns_text), ns_range));
                ++i;
                continue;
            }

            result.push_back(TokenInfo::make(ParserToken::Kind::Ident, std::move(text), range));
            i = j;
            continue;
        }

        // Regular tokens: just convert Lexeme to TokenInfo
        if (!is_unimplemented_token(lex.kind) && !is_concatenatable_token(lex.kind) && lex.kind != ParserToken::Kind::MANGLED && !is_id_start(lex.kind) &&
            !is_namespace(lex.kind)) {
            result.push_back(TokenInfo::make(lex));
        }

        ++i;
    }

    return result;
}

} // namespace trust