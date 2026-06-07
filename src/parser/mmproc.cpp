#include "diag/diag.hpp"
#include "parser/mmproc.hpp"
#include "ast/token_info.hpp"
#include "ast/attr_builtin.hpp"
#include "parser/lexer.hpp"
#include <cstddef>
#include <cstdlib>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <format>
#include <set>
#include <stdexcept>
#include <string>
#include <sstream>
#include <iomanip>
#include <unordered_map>

// Определение статических счётчиков (начинаются с 1)
int trust::MMProcessor::s_counter = 1;
int trust::MMProcessor::s_hygienicCounter = 1;

namespace trust {

std::string_view getDefaultDslSrc() noexcept {
    static constexpr char kData[] = {
#embed "dsl.src"
    };
    return std::string_view{kData, sizeof(kData)};
}

// ============================================================
// Вспомогательные функции для handlePredefinedMacro
// ============================================================

namespace {

static std::string formatHash(uint64_t hash) {
    std::array<char, 17> buf{};
    int n = std::snprintf(buf.data(), buf.size(), "%016llx", static_cast<unsigned long long>(hash));
    return std::string(buf.data(), static_cast<std::size_t>(n));
}

static std::string formatDateAsCpp() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_r(&t, &tm);
    std::array<char, 12> buf{};
    std::strftime(buf.data(), buf.size(), "%b %d %Y", &tm);
    return std::string(buf.data());
}

static std::string formatTimeAsCpp() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_r(&t, &tm);
    std::array<char, 9> buf{};
    std::strftime(buf.data(), buf.size(), "%H:%M:%S", &tm);
    return std::string(buf.data());
}

static std::string formatTimestampAsAsctime() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_r(&t, &tm);
    std::array<char, 26> buf{};
    std::strftime(buf.data(), buf.size(), "%a %d %b %H:%M:%S %Y", &tm);
    return std::string(buf.data());
}

static std::string formatTimestampISO() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    gmtime_r(&t, &tm);
    std::array<char, 21> buf{};
    std::strftime(buf.data(), buf.size(), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return std::string(buf.data());
}

} // anonymous namespace

// ============================================================
// handlePredefinedMacro
// ============================================================

TokenSequence MMProcessor::handlePredefinedMacro(Context& ctx, std::string_view macroName, MapperFile fileIdx, MapperLocation loc) {
    using Kind = ParserToken::Kind;
    auto makeStringLit = [&](std::string text) -> TokenSequence { return {TokenInfo::make(Kind::StringLiteral, std::move(text), MapperRange{loc, loc})}; };
    auto makeIntLit = [&](int value) -> TokenSequence { return {TokenInfo::make(Kind::IntLiteral, std::to_string(value), MapperRange{loc, loc})}; };

    if (macroName == "__TRUST_VERSION_MAJOR__")
        return makeIntLit(TRUST_VERSION_MAJOR);
    if (macroName == "__TRUST_VERSION_MINOR__")
        return makeIntLit(TRUST_VERSION_MINOR);
    if (macroName == "__TRUST_VERSION_PATCH__")
        return makeIntLit(TRUST_VERSION_PATCH);
    if (macroName == "__TRUST_VERSION__")
        return makeStringLit(TRUST_VERSION);
    if (macroName == "__TRUST_GIT_HASH__")
        return makeStringLit(TRUST_GIT_HASH);
    if (macroName == "__TRUST_VERSION_FULL__")
        return makeStringLit(TRUST_VERSION_FULL);
    if (macroName == "__TRUST_DATE_BUILD__")
        return makeStringLit(TRUST_DATE_BUILD);

    if (macroName == "__FILE__")
        return makeStringLit(std::string{ctx.filename(fileIdx)});
    if (macroName == "__FILE_NAME__") {
        std::filesystem::path p(std::string{ctx.filename(fileIdx)});
        return makeStringLit(p.filename().string());
    }
    if (macroName == "__LINE__" || macroName == "__FILE_LINE__") {
        uint32_t line = ctx.line(loc);
        return makeIntLit(static_cast<int>(line));
    }
    if (macroName == "__FILE_MD5__")
        return makeStringLit(formatHash(ctx.getFileHash(fileIdx)));
    if (macroName == "__FILE_TIMESTAMP__")
        return makeStringLit(formatTimestampISO());
    if (macroName == "__DATE__")
        return makeStringLit(formatDateAsCpp());
    if (macroName == "__TIME__")
        return makeStringLit(formatTimeAsCpp());
    if (macroName == "__TIMESTAMP__")
        return makeStringLit(formatTimestampAsAsctime());
    if (macroName == "__TIMESTAMP_ISO__")
        return makeStringLit(formatTimestampISO());

    if (macroName == "__COUNTER__") {
        return makeIntLit(s_counter++);
    }

    if (macroName.size() >= 4 && macroName.starts_with("__") && macroName.ends_with("__")) {
        ctx.diag().report(loc, Severity::Error, "unknown predefined macro '@{}'", macroName);
    }
    return {};
}

// ============================================================
// currentMacros
// ============================================================

const std::vector<MacroDef>& MMProcessor::currentMacros(const MacroTable& macros) noexcept {
    static const std::vector<MacroDef> empty_vec;
    if (macros.empty())
        return empty_vec;
    const auto& last_module = macros.back();
    if (last_module.empty())
        return empty_vec;
    return last_module.begin()->second;
}

// ============================================================
// escape / unescape
// ============================================================

std::string MMProcessor::escape(const std::string& s) {
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

std::string MMProcessor::unescape(const std::string& s) {
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

// ============================================================
// Kind classification helpers
// ============================================================

static bool is_string_token(ParserToken::Kind k) noexcept {
    return k == ParserToken::Kind::STRWIDE || k == ParserToken::Kind::STRCHAR || k == ParserToken::Kind::STRWIDE_RAW || k == ParserToken::Kind::STRCHAR_RAW;
}
static bool is_embed_token(ParserToken::Kind k) noexcept {
    return k == ParserToken::Kind::EMBED;
}
static bool is_concatenatable_token(ParserToken::Kind k) noexcept {
    return is_string_token(k) || is_embed_token(k);
}
static bool is_raw_token(ParserToken::Kind k) noexcept {
    return k == ParserToken::Kind::STRWIDE_RAW || k == ParserToken::Kind::STRCHAR_RAW;
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
static bool is_creation_operator(ParserToken::Kind k) noexcept {
    return k == ParserToken::Kind::CREATE_NEW || k == ParserToken::Kind::CREATE_USE || k == ParserToken::Kind::ASSIGN;
}
static bool is_macro_name_lexeme(ParserToken::Kind k) noexcept {
    return k == ParserToken::Kind::NAME || k == ParserToken::Kind::LOCAL || k == ParserToken::Kind::NATIVE;
}

// ============================================================
// formatArgsText
// ============================================================

static std::string formatArgsText(const std::vector<LexemeSequence>& args, bool withParens) {
    std::string out;
    if (withParens)
        out = "(";
    for (std::size_t ai = 0; ai < args.size(); ++ai) {
        if (ai > 0)
            out += ", ";
        bool first = true;
        for (const auto& tok : args[ai]) {
            if (!first)
                out += " ";
            out.append(tok.data(), tok.size());
            first = false;
        }
    }
    if (withParens) {
        out += ",)";
    }
    return out;
}

// ============================================================
// makeStringLiteral
// ============================================================

TokenPtr MMProcessor::makeStringLiteral(const Lexeme& lex) {
    std::string text{lex};
    if (!is_raw_token(lex.kind))
        text = unescape(text);
    return TokenInfo::make(ParserToken::Kind::StringLiteral, std::move(text), MapperRange{lex.pos, lex.pos});
}

// ============================================================
// concatStringTokens
// ============================================================

TokenPtr MMProcessor::concatStringTokens(const LexemeSequence& lexemes, std::size_t& pos) {
    const Lexeme& first = lexemes[pos];
    ParserToken::Kind kind = first.kind;
    std::string text{first};
    MapperRange range{first.pos, first.pos};
    std::size_t j = pos + 1;
    while (j < lexemes.size() && lexemes[j].kind == kind) {
        text.append(lexemes[j].data(), lexemes[j].size());
        range.end = lexemes[j].pos;
        ++j;
    }
    if (!is_raw_token(kind))
        text = unescape(text);
    pos = j;
    return TokenInfo::make(kind, std::move(text), range);
}

// ============================================================
// buildIdentToken
// ============================================================

TokenPtr MMProcessor::buildIdentToken(const LexemeSequence& lexemes, std::size_t& pos) {
    std::string text;
    MapperRange range{lexemes[pos].pos, lexemes[pos].pos};
    bool has_main_part = false;
    std::size_t j = pos;
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
            j = ns_pos;
            break;
        }
    }
    if (j < lexemes.size() && is_id_terminator(lexemes[j].kind)) {
        text.append(lexemes[j].data(), lexemes[j].size());
        range.end = lexemes[j].pos;
        ++j;
    }
    if (!has_main_part) {
        pos = j;
        return TokenInfo::make(ParserToken::Kind::NAMESPACE, std::move(text), range);
    }
    pos = j;
    return TokenInfo::make(ParserToken::Kind::Ident, std::move(text), range);
}

// ============================================================
// splitArgsByComma
// ============================================================

std::vector<LexemeSequence> MMProcessor::splitArgsByComma(const LexemeSequence& tokens) {
    std::vector<LexemeSequence> result;
    if (tokens.empty())
        return result;
    LexemeSequence current;
    int dp = 0, db = 0, dbr = 0;
    for (const auto& tok : tokens) {
        if (tok.kind == ParserToken::Kind::LPAREN) {
            dp++;
            current.push_back(tok);
        } else if (tok.kind == ParserToken::Kind::RPAREN) {
            dp--;
            current.push_back(tok);
        } else if (tok.kind == ParserToken::Kind::LBRACKET) {
            db++;
            current.push_back(tok);
        } else if (tok.kind == ParserToken::Kind::RBRACKET) {
            db--;
            current.push_back(tok);
        } else if (tok.kind == ParserToken::Kind::LBRACE) {
            dbr++;
            current.push_back(tok);
        } else if (tok.kind == ParserToken::Kind::RBRACE) {
            dbr--;
            current.push_back(tok);
        } else if (tok.kind == ParserToken::Kind::COMMA && dp == 0 && db == 0 && dbr == 0) {
            result.push_back(std::move(current));
            current.clear();
        } else {
            current.push_back(tok);
        }
    }
    result.push_back(std::move(current));
    return result;
}

// ============================================================
// resolveArgByNameOrNumber
// ============================================================

static int resolveArgByNameOrNumber(std::string_view ref, const std::vector<std::string_view>& paramNames) noexcept {
    if (ref.empty())
        return -1;
    bool is_numeric = true;
    for (char c : ref) {
        if (!std::isdigit(static_cast<unsigned char>(c))) {
            is_numeric = false;
            break;
        }
    }
    if (is_numeric)
        return std::stoi(std::string{ref}) - 1;
    for (std::size_t pi = 0; pi < paramNames.size(); ++pi)
        if (paramNames[pi] == ref)
            return static_cast<int>(pi);
    return -1;
}

// ============================================================
// substituteTokenText
// ============================================================

std::string MMProcessor::substituteTokenText(std::string_view text, const std::vector<std::vector<LexemeSequence>>& callArgsByGroup,
                                             const std::vector<MacroArgGroup>& argGroups) {
    if (text.empty())
        return std::string{text};
    std::string result;
    result.reserve(text.size());
    int totalArgCount = 0;
    for (const auto& group : callArgsByGroup)
        totalArgCount += static_cast<int>(group.size());
    std::vector<int> variadicGroupIndices;
    for (int gi = 0; gi < static_cast<int>(argGroups.size()); ++gi)
        if (argGroups[gi].m_hasVariadic)
            variadicGroupIndices.push_back(gi);
    auto formatVariadicGroup = [&](int groupIdx, bool withParens) -> std::string {
        if (groupIdx < 0 || groupIdx >= static_cast<int>(callArgsByGroup.size()))
            return {};
        return formatArgsText(callArgsByGroup[groupIdx], withParens);
    };
    auto formatAllVariadic = [&](bool withParens) -> std::string {
        std::vector<LexemeSequence> all;
        for (int gi : variadicGroupIndices)
            if (gi < static_cast<int>(callArgsByGroup.size()))
                for (const auto& seq : callArgsByGroup[gi])
                    all.push_back(seq);
        return formatArgsText(all, withParens);
    };
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '@' && i + 1 < text.size() && text[i + 1] == '$') {
            std::size_t start = i + 2;
            std::string ref;
            std::size_t j = start;
            bool is_star = false, is_hash = false, is_ellipsis = false;
            int ellipsis_number = 0;
            if (j < text.size() && text[j] == '*') {
                is_star = true;
                ref = "*";
                j++;
            } else if (j < text.size() && text[j] == '#') {
                is_hash = true;
                ref = "#";
                j++;
            } else if (j + 2 < text.size() && text[j] == '.' && text[j + 1] == '.' && text[j + 2] == '.') {
                is_ellipsis = true;
                ref = "...";
                j += 3;
                if (j < text.size() && text[j] >= '1' && text[j] <= '9') {
                    ellipsis_number = text[j] - '0';
                    j++;
                }
            } else {
                while (j < text.size() && (std::isalnum(static_cast<unsigned char>(text[j])) || text[j] == '_')) {
                    ref += text[j];
                    j++;
                }
            }
            if (ref.empty()) {
                result += text[i];
                continue;
            }
            if (is_star) {
                if (ellipsis_number > 0)
                    result += formatVariadicGroup(variadicGroupIndices[ellipsis_number - 1], true);
                else
                    result += formatAllVariadic(true);
                i = j - 1;
                continue;
            }
            if (is_ellipsis) {
                if (ellipsis_number > 0)
                    result += formatVariadicGroup(variadicGroupIndices[ellipsis_number - 1], false);
                else
                    result += formatAllVariadic(false);
                i = j - 1;
                continue;
            }
            if (is_hash) {
                result += std::to_string(totalArgCount);
                i = j - 1;
                continue;
            }
            for (int gi = 0; gi < static_cast<int>(argGroups.size()); ++gi) {
                int arg_index = resolveArgByNameOrNumber(ref, argGroups[gi].m_params);
                if (arg_index >= 0 && arg_index < static_cast<int>(callArgsByGroup[gi].size())) {
                    bool first = true;
                    for (const auto& tok : callArgsByGroup[gi][arg_index]) {
                        if (!first)
                            result += " ";
                        result.append(tok.data(), tok.size());
                        first = false;
                    }
                    break;
                }
                bool is_numeric = true;
                for (char c : ref) {
                    if (!std::isdigit(static_cast<unsigned char>(c))) {
                        is_numeric = false;
                        break;
                    }
                }
                if (is_numeric) {
                    int flat_idx = std::stoi(std::string{ref}) - 1;
                    int cur = 0;
                    for (int g = 0; g < static_cast<int>(callArgsByGroup.size()); ++g) {
                        if (flat_idx < cur + static_cast<int>(callArgsByGroup[g].size())) {
                            int local_idx = flat_idx - cur;
                            if (local_idx >= 0 && local_idx < static_cast<int>(callArgsByGroup[g].size())) {
                                bool first = true;
                                for (const auto& tok : callArgsByGroup[g][local_idx]) {
                                    if (!first)
                                        result += " ";
                                    result.append(tok.data(), tok.size());
                                    first = false;
                                }
                            }
                            break;
                        }
                        cur += static_cast<int>(callArgsByGroup[g].size());
                    }
                    break;
                }
            }
            i = j - 1;
        } else {
            result += text[i];
        }
    }
    return result;
}

// ============================================================
// substituteArgs
// ============================================================

LexemeSequence MMProcessor::substituteArgs(const LexemeSequence& body, const std::vector<std::vector<LexemeSequence>>& callArgsByGroup,
                                           const std::vector<MacroArgGroup>& argGroups) {
    LexemeSequence result;
    result.reserve(body.size());
    std::vector<std::string> owned_strings;
    struct ParamLoc {
        int group;
        int index;
    };
    std::unordered_map<std::string_view, ParamLoc> paramMap;
    for (int gi = 0; gi < static_cast<int>(argGroups.size()); ++gi)
        for (int pi = 0; pi < static_cast<int>(argGroups[gi].m_params.size()); ++pi)
            paramMap[argGroups[gi].m_params[pi]] = {gi, pi};
    std::vector<int> variadicGroupIndices;
    for (int gi = 0; gi < static_cast<int>(argGroups.size()); ++gi)
        if (argGroups[gi].m_hasVariadic)
            variadicGroupIndices.push_back(gi);
    auto insertGroupArgs = [&](int groupIdx, bool withParens, MapperLocation tokenPos) {
        if (groupIdx < 0 || groupIdx >= static_cast<int>(callArgsByGroup.size()))
            return;
        if (withParens)
            result.push_back(Lexeme(ParserToken::Kind::LPAREN, "(", tokenPos));
        const auto& group = callArgsByGroup[groupIdx];
        for (std::size_t ai = 0; ai < group.size(); ++ai) {
            if (ai > 0)
                result.push_back(Lexeme(ParserToken::Kind::COMMA, ",", tokenPos));
            for (const auto& arg_tok : group[ai])
                result.push_back(arg_tok);
        }
        if (withParens) {
            result.push_back(Lexeme(ParserToken::Kind::COMMA, ",", tokenPos));
            result.push_back(Lexeme(ParserToken::Kind::RPAREN, ")", tokenPos));
        }
    };
    auto insertAllVariadic = [&](bool withParens, MapperLocation tokenPos) {
        if (!variadicGroupIndices.empty()) {
            if (withParens)
                result.push_back(Lexeme(ParserToken::Kind::LPAREN, "(", tokenPos));
            bool first = true;
            for (int gi : variadicGroupIndices) {
                if (gi >= static_cast<int>(callArgsByGroup.size()))
                    continue;
                for (std::size_t ai = 0; ai < callArgsByGroup[gi].size(); ++ai) {
                    if (!first)
                        result.push_back(Lexeme(ParserToken::Kind::COMMA, ",", tokenPos));
                    first = false;
                    for (const auto& arg_tok : callArgsByGroup[gi][ai])
                        result.push_back(arg_tok);
                }
            }
            if (withParens) {
                result.push_back(Lexeme(ParserToken::Kind::COMMA, ",", tokenPos));
                result.push_back(Lexeme(ParserToken::Kind::RPAREN, ")", tokenPos));
            }
        } else {
            if (withParens)
                result.push_back(Lexeme(ParserToken::Kind::LPAREN, "(", tokenPos));
            bool first = true;
            for (std::size_t gi = 0; gi < callArgsByGroup.size(); ++gi) {
                for (std::size_t ai = 0; ai < callArgsByGroup[gi].size(); ++ai) {
                    if (!first)
                        result.push_back(Lexeme(ParserToken::Kind::COMMA, ",", tokenPos));
                    first = false;
                    for (const auto& arg_tok : callArgsByGroup[gi][ai])
                        result.push_back(arg_tok);
                }
            }
            if (withParens) {
                result.push_back(Lexeme(ParserToken::Kind::COMMA, ",", tokenPos));
                result.push_back(Lexeme(ParserToken::Kind::RPAREN, ")", tokenPos));
            }
        }
    };
    auto totalArgCount = [&]() -> int {
        int c = 0;
        for (const auto& g : callArgsByGroup)
            c += static_cast<int>(g.size());
        return c;
    };
    for (const auto& token : body) {
        if (token.kind == ParserToken::Kind::MACRO_ARGNAME || token.kind == ParserToken::Kind::MACRO_ARGPOS) {
            std::string_view sv{token};
            if (sv.size() >= 2 && sv[0] == '@' && sv[1] == '$')
                sv = sv.substr(2);
            if (token.kind == ParserToken::Kind::MACRO_ARGNAME) {
                auto it = paramMap.find(sv);
                if (it != paramMap.end()) {
                    int gi = it->second.group, pi = it->second.index;
                    if (gi < static_cast<int>(callArgsByGroup.size()) && pi < static_cast<int>(callArgsByGroup[gi].size()))
                        for (const auto& arg_tok : callArgsByGroup[gi][pi])
                            result.push_back(arg_tok);
                }
                continue;
            }
            auto dot_pos = sv.find('.');
            if (dot_pos != std::string_view::npos) {
                int a = std::stoi(std::string{sv.substr(0, dot_pos)}) - 1;
                int g = std::stoi(std::string{sv.substr(dot_pos + 1)}) - 1;
                if (g >= 0 && g < static_cast<int>(callArgsByGroup.size()) && a >= 0 && a < static_cast<int>(callArgsByGroup[g].size()))
                    for (const auto& arg_tok : callArgsByGroup[g][a])
                        result.push_back(arg_tok);
                continue;
            }
            if (argGroups.size() == 1) {
                int a = std::stoi(std::string{sv}) - 1;
                if (a >= 0 && a < static_cast<int>(callArgsByGroup[0].size()))
                    for (const auto& arg_tok : callArgsByGroup[0][a])
                        result.push_back(arg_tok);
            }
            continue;
        }
        if (token.kind == ParserToken::Kind::MACRO_ARGUMENT) {
            std::string_view sv{token};
            if (sv == "@$...") {
                insertAllVariadic(false, token.pos);
                continue;
            }
            if (sv.starts_with("@$...") && sv.size() > 5 && sv[5] >= '1' && sv[5] <= '9') {
                int variadicIdx = (sv[5] - '0') - 1;
                if (variadicIdx < static_cast<int>(variadicGroupIndices.size()))
                    insertGroupArgs(variadicGroupIndices[variadicIdx], false, token.pos);
                continue;
            }
            if (sv == "@$*") {
                insertAllVariadic(true, token.pos);
                continue;
            }
            if (sv.starts_with("@$*.") && sv.size() > 4 && sv[3] == '.' && sv[4] >= '1' && sv[4] <= '9') {
                int groupIdx = (sv[4] - '0') - 1;
                if (groupIdx >= 0 && groupIdx < static_cast<int>(callArgsByGroup.size()))
                    insertGroupArgs(groupIdx, true, token.pos);
                continue;
            }
            owned_strings.push_back(substituteTokenText(std::string{token}, callArgsByGroup, argGroups));
            result.push_back(Lexeme(ParserToken::Kind::Ident, owned_strings.back(), token.pos));
            continue;
        }
        if (token.kind == ParserToken::Kind::MACRO_ARGCOUNT) {
            std::string_view sv{token};
            if (sv == "@$#") {
                owned_strings.push_back(std::to_string(totalArgCount()));
                result.push_back(Lexeme(ParserToken::Kind::IntLiteral, owned_strings.back(), token.pos));
                continue;
            }
            if (sv.starts_with("@$#.") && sv.size() > 4 && sv[3] == '.' && sv[4] >= '1' && sv[4] <= '9') {
                int groupIdx = (sv[4] - '0') - 1;
                if (groupIdx >= 0 && groupIdx < static_cast<int>(callArgsByGroup.size())) {
                    owned_strings.push_back(std::to_string(callArgsByGroup[groupIdx].size()));
                    result.push_back(Lexeme(ParserToken::Kind::IntLiteral, owned_strings.back(), token.pos));
                }
                continue;
            }
            owned_strings.push_back(std::to_string(totalArgCount()));
            result.push_back(Lexeme(ParserToken::Kind::IntLiteral, owned_strings.back(), token.pos));
            continue;
        }
        std::string new_text = substituteTokenText(token, callArgsByGroup, argGroups);
        if (new_text.size() != token.size() || memcmp(new_text.data(), token.data(), token.size()) != 0) {
            owned_strings.push_back(std::move(new_text));
            result.push_back(Lexeme(token.kind, owned_strings.back(), token.pos));
        } else {
            result.push_back(token);
        }
    }
    return result;
}

// ============================================================
// collectMacroDef
// ============================================================

static bool isMacroRedefined(MacroTable& macros, const MacroDef& def, Context& ctx, MapperLocation op_loc) {
    auto key = std::string_view{def.m_nameLexemes[0]};
    for (auto& module : macros) {
        auto it = module.find(key);
        if (it != module.end()) {
            for (const auto& existing_def : it->second) {
                if (existing_def.m_nameLexemes.size() != def.m_nameLexemes.size())
                    continue;
                bool same = true;
                for (std::size_t k = 0; k < def.m_nameLexemes.size(); ++k) {
                    if (existing_def.m_nameLexemes[k].kind != def.m_nameLexemes[k].kind ||
                        std::string_view{existing_def.m_nameLexemes[k]} != std::string_view{def.m_nameLexemes[k]}) {
                        same = false;
                        break;
                    }
                }
                if (same) {
                    ctx.diag().report(op_loc, Severity::Error, "macro '{}' redefined", std::string_view{def.m_nameLexemes[0]});
                    return true;
                }
            }
        }
    }
    return false;
}

static ParserToken::Kind closingFor(ParserToken::Kind open) noexcept {
    if (open == ParserToken::Kind::LPAREN)
        return ParserToken::Kind::RPAREN;
    if (open == ParserToken::Kind::LBRACKET)
        return ParserToken::Kind::RBRACKET;
    if (open == ParserToken::Kind::LANGLE)
        return ParserToken::Kind::RANGLE;
    return ParserToken::Kind::END;
}

static bool is_open_bracket(ParserToken::Kind k) noexcept {
    return k == ParserToken::Kind::LPAREN || k == ParserToken::Kind::LBRACKET || k == ParserToken::Kind::LANGLE;
}

bool MMProcessor::collectMacroDef(Context& ctx, MacroTable& macros, const LexemeSequence& lexemes, std::size_t& pos) {
    EXPECT(pos < lexemes.size() && lexemes[pos].kind == ParserToken::Kind::MACRO_SEQ);
    MapperLocation macro_start_loc = lexemes[pos].pos;
    pos++;
    std::vector<Lexeme> name_lexemes;
    std::vector<MacroArgGroup> arg_groups;
    MacroArgGroup* current_group = nullptr;
    ParserToken::Kind current_open_kind{ParserToken::Kind::END};
    MapperLocation paren_open_loc{};
    MapperLocation last_name_loc = macro_start_loc;
    while (pos < lexemes.size() && lexemes[pos].kind != ParserToken::Kind::MACRO_SEQ) {
        if (current_group) {
            ParserToken::Kind close_kind = closingFor(current_open_kind);
            if (lexemes[pos].kind == close_kind) {
                current_group = nullptr;
                current_open_kind = ParserToken::Kind::END;
                pos++;
                continue;
            }
            if (lexemes[pos].kind == ParserToken::Kind::ELLIPSIS) {
                current_group->m_hasVariadic = true;
                std::size_t next = pos + 1;
                if (next < lexemes.size() && lexemes[next].kind == ParserToken::Kind::COMMA)
                    next++;
                if (next < lexemes.size() && lexemes[next].kind != close_kind)
                    ctx.diag().report(lexemes[pos].pos, Severity::Error, "'...' must be the last argument in a macro argument group");
                pos++;
                continue;
            }
            if (lexemes[pos].kind == ParserToken::Kind::COMMA) {
                pos++;
                continue;
            }
            if (lexemes[pos].kind == ParserToken::Kind::LOCAL) {
                current_group->m_params.push_back(std::string_view{lexemes[pos]}.substr(1));
                pos++;
                continue;
            }
            ctx.diag().report(lexemes[pos].pos, Severity::Error, "unexpected token '{}' in macro argument group", ParserToken::name(lexemes[pos].kind));
            pos++;
            continue;
        }
        if (is_open_bracket(lexemes[pos].kind)) {
            current_open_kind = lexemes[pos].kind;
            paren_open_loc = lexemes[pos].pos;
            arg_groups.emplace_back();
            current_group = &arg_groups.back();
            pos++;
            continue;
        }
        if (lexemes[pos].kind == ParserToken::Kind::SEMICOLON) {
            ctx.diag().report(lexemes[pos].pos, Severity::Error, "unexpected ';' in macro name");
            pos++;
            return true;
        }
        if (is_macro_name_lexeme(lexemes[pos].kind)) {
            last_name_loc = lexemes[pos].pos;
            if (lexemes[pos].kind == ParserToken::Kind::LOCAL) {
                if (arg_groups.empty() || !arg_groups.back().m_isTemplate) {
                    MacroArgGroup tg;
                    tg.m_isTemplate = true;
                    tg.m_params.push_back(std::string_view{lexemes[pos]}.substr(1));
                    arg_groups.push_back(std::move(tg));
                } else
                    arg_groups.back().m_params.push_back(std::string_view{lexemes[pos]}.substr(1));
            } else
                name_lexemes.push_back(lexemes[pos]);
            pos++;
            continue;
        }
        ctx.diag().report(lexemes[pos].pos, Severity::Error, "unexpected token '{}' in macro name", ParserToken::name(lexemes[pos].kind));
        pos++;
        continue;
    }
    if (current_group) {
        ctx.diag().report(paren_open_loc.isValid() ? paren_open_loc : macro_start_loc, Severity::Error, "unterminated bracket in macro argument group");
        if (pos < lexemes.size() && lexemes[pos].kind == ParserToken::Kind::MACRO_SEQ)
            pos++;
        return true;
    }
    if (pos >= lexemes.size() || lexemes[pos].kind != ParserToken::Kind::MACRO_SEQ) {
        ctx.diag().report(last_name_loc.isValid() ? last_name_loc : macro_start_loc, Severity::Error, "unterminated macro name (expected '@@')");
        return true;
    }
    MapperLocation close_name_loc = lexemes[pos].pos;
    pos++;
    if (pos >= lexemes.size()) {
        ctx.diag().report(close_name_loc, Severity::Error, "expected creation operator after macro name, got end of input");
        return true;
    }
    if (!is_creation_operator(lexemes[pos].kind)) {
        ctx.diag().report(lexemes[pos].pos, Severity::Error, "expected creation operator after macro name");
        return true;
    }
    if (name_lexemes.empty()) {
        ctx.diag().report(macro_start_loc, Severity::Error, "macro name cannot be empty");
        pos++;
        return true;
    }
    MapperLocation op_loc = lexemes[pos].pos;
    pos++;
    MacroDef def;
    def.m_nameLexemes = std::move(name_lexemes);
    def.m_argGroups = std::move(arg_groups);
    def.m_bodyType = MacroBodyType::kExpression;
    int vc = 0;
    for (const auto& g : def.m_argGroups)
        if (g.m_hasVariadic)
            vc++;
    def.m_variadicCount = vc;
    MapperLocation first_body_loc = (pos < lexemes.size()) ? lexemes[pos].pos : op_loc;
    MapperLocation last_consumed_loc = op_loc;
    if (pos < lexemes.size()) {
        if (lexemes[pos].kind == ParserToken::Kind::MACRO_SEQ) {
            def.m_bodyType = MacroBodyType::kTokenSequence;
            pos++;
            while (pos < lexemes.size() && lexemes[pos].kind != ParserToken::Kind::MACRO_SEQ) {
                def.m_body.push_back(lexemes[pos]);
                last_consumed_loc = lexemes[pos].pos;
                pos++;
            }
            if (pos >= lexemes.size() || lexemes[pos].kind != ParserToken::Kind::MACRO_SEQ) {
                ctx.diag().report(last_consumed_loc, Severity::Error, "unterminated macro body (expected '@@')");
                return true;
            }
            last_consumed_loc = lexemes[pos].pos;
            pos++;
        } else if (lexemes[pos].kind == ParserToken::Kind::MACRO_STR) {
            def.m_bodyType = MacroBodyType::kStringLiteral;
            def.m_body.push_back(lexemes[pos]);
            last_consumed_loc = lexemes[pos].pos;
            pos++;
        } else {
            while (pos < lexemes.size() && lexemes[pos].kind != ParserToken::Kind::SEMICOLON) {
                def.m_body.push_back(lexemes[pos]);
                last_consumed_loc = lexemes[pos].pos;
                pos++;
            }
        }
    }
    def.m_bodyRange = MapperRange{first_body_loc, last_consumed_loc};
    if (pos < lexemes.size() && lexemes[pos].kind == ParserToken::Kind::SEMICOLON) {
        def.m_bodyRange.end = lexemes[pos].pos;
        pos++;
    } else
        ctx.diag().report(last_consumed_loc, Severity::Error, "expected ';' after macro definition");
    if (isMacroRedefined(macros, def, ctx, op_loc))
        return true;
    macros.back()[std::string_view{def.m_nameLexemes[0]}].push_back(std::move(def));
    return true;
}

// ============================================================
// makeHygienicName
// ============================================================

std::string MMProcessor::makeHygienicName(std::string_view ident, HygienicMap& hygienicMap) {
    auto it = hygienicMap.find(std::string(ident));
    if (it != hygienicMap.end()) {
        std::size_t last_ns = ident.rfind("::");
        std::string_view last_part = (last_ns == std::string_view::npos) ? ident : ident.substr(last_ns + 2);
        std::string result;
        result.reserve(ident.size() + 8);
        if (last_ns != std::string_view::npos)
            result.append(ident.data(), last_ns + 2);
        result.append(last_part);
        result += "__G";
        result += std::to_string(it->second);
        result += "_";
        return result;
    }
    int num = s_hygienicCounter++;
    hygienicMap[std::string(ident)] = num;
    std::size_t last_ns = ident.rfind("::");
    std::string_view last_part = (last_ns == std::string_view::npos) ? ident : ident.substr(last_ns + 2);
    std::string result;
    result.reserve(ident.size() + 8);
    if (last_ns != std::string_view::npos)
        result.append(ident.data(), last_ns + 2);
    result.append(last_part);
    result += "__G";
    result += std::to_string(num);
    result += "_";
    return result;
}

// ============================================================
// expandMacroLexeme
// ============================================================

TokenSequence MMProcessor::expandMacroLexeme(Context& ctx, MacroTable& macros, int& recursionDepth, std::string_view name, const LexemeSequence& lexemes,
                                             std::size_t& pos, std::set<std::string>* expandedMacros, HygienicMap* hygienicMap) {
    TokenSequence result;
    if (expandedMacros)
        expandedMacros->insert(std::string(name));
    if (recursionDepth >= kMaxRecursionDepth) {
        ctx.diag().report(lexemes[pos].pos, Severity::Error, "macro recursion depth exceeded (max {})", kMaxRecursionDepth);
        return result;
    }
    const MacroDef* def_ptr = nullptr;
    std::size_t name_match_len = 1;
    for (auto it = macros.rbegin(); it != macros.rend(); ++it) {
        auto found = it->find(name);
        if (found != it->end()) {
            for (const auto& candidate : found->second) {
                std::size_t check_pos = pos + 1;
                bool match = true;
                for (std::size_t ni = 1; ni < candidate.m_nameLexemes.size(); ++ni) {
                    if (check_pos >= lexemes.size()) {
                        match = false;
                        break;
                    }
                    if (lexemes[check_pos].kind != candidate.m_nameLexemes[ni].kind ||
                        std::string_view{lexemes[check_pos]} != std::string_view{candidate.m_nameLexemes[ni]}) {
                        match = false;
                        break;
                    }
                    check_pos++;
                }
                if (match) {
                    def_ptr = &candidate;
                    name_match_len = check_pos - pos;
                    break;
                }
            }
            if (def_ptr)
                break;
        }
    }
    if (!def_ptr) {
        ctx.diag().report(lexemes[pos].pos, Severity::Error, "undefined macro '{}'", name);
        return result;
    }
    const MacroDef& def = *def_ptr;
    pos += name_match_len;
    std::vector<std::vector<LexemeSequence>> callArgsByGroup;
    callArgsByGroup.reserve(def.m_argGroups.size());
    bool has_bracket_call = false;
    bool mismatch_error = false;
    for (std::size_t gi = 0; gi < def.m_argGroups.size(); ++gi) {
        const auto& group = def.m_argGroups[gi];
        if (group.m_isTemplate) {
            has_bracket_call = false;
            LexemeSequence group_flat;
            while (pos < lexemes.size() && lexemes[pos].kind != ParserToken::Kind::SEMICOLON) {
                group_flat.push_back(lexemes[pos]);
                pos++;
            }
            if (group_flat.empty())
                callArgsByGroup.emplace_back();
            else {
                std::vector<LexemeSequence> template_args;
                for (const auto& tok : group_flat) {
                    LexemeSequence single;
                    single.push_back(tok);
                    template_args.push_back(std::move(single));
                }
                callArgsByGroup.push_back(std::move(template_args));
            }
        } else {
            if (pos >= lexemes.size() || !is_open_bracket(lexemes[pos].kind)) {
                if (!mismatch_error) {
                    ctx.diag().report((pos < lexemes.size()) ? lexemes[pos].pos : lexemes[pos - 1].pos, Severity::Error,
                                      "macro '{}' expects bracket group {} but got end of input or unexpected token", std::string_view{def.m_nameLexemes[0]},
                                      gi + 1);
                    mismatch_error = true;
                }
                callArgsByGroup.emplace_back();
                continue;
            }
            has_bracket_call = true;
            ParserToken::Kind open_kind = lexemes[pos].kind, close_kind = closingFor(open_kind);
            pos++;
            LexemeSequence group_flat;
            int depth = 1;
            while (pos < lexemes.size() && depth > 0) {
                if (lexemes[pos].kind == open_kind) {
                    depth++;
                    if (depth > 1)
                        group_flat.push_back(lexemes[pos]);
                } else if (lexemes[pos].kind == close_kind) {
                    depth--;
                    if (depth > 0)
                        group_flat.push_back(lexemes[pos]);
                } else
                    group_flat.push_back(lexemes[pos]);
                pos++;
            }
            if (group_flat.empty())
                callArgsByGroup.emplace_back();
            else
                callArgsByGroup.push_back(splitArgsByComma(group_flat));
        }
    }
    if (def.m_argGroups.empty() && pos < lexemes.size() && is_open_bracket(lexemes[pos].kind)) {
        ctx.diag().report(lexemes[pos].pos, Severity::Error, "macro '{}' has no argument groups but called with brackets",
                          std::string_view{def.m_nameLexemes[0]});
        return result;
    }
    if (!def.m_argGroups.empty() && !has_bracket_call) {
        bool expect_brackets = false;
        for (const auto& g : def.m_argGroups) {
            if (!g.m_isTemplate) {
                expect_brackets = true;
                break;
            }
        }
        if (expect_brackets && !mismatch_error) {
            ctx.diag().report(lexemes[pos > 0 ? pos - 1 : pos].pos, Severity::Error, "macro '{}' defined with bracket arguments, but called without brackets",
                              std::string_view{def.m_nameLexemes[0]});
            return result;
        }
    }
    // Каждый вызов expandMacroLexeme создаёт свой hygienicMap (если hygienicMap не передан).
    // Вложенные раскрытия (рекурсивные) используют тот же map.
    HygienicMap localHygienicMap;
    HygienicMap* effectiveHygienicMap = hygienicMap ? hygienicMap : &localHygienicMap;
    auto recurseExpand = [&](LexemeSequence&& input) -> TokenSequence {
        recursionDepth++;
        std::size_t recurse_pos = 0;
        auto out = processInternal(ctx, macros, recursionDepth, input, recurse_pos, expandedMacros, effectiveHygienicMap);
        recursionDepth--;
        return out;
    };
    if (def.m_bodyType == MacroBodyType::kStringLiteral) {
        std::string body_text;
        for (const auto& tok : def.m_body)
            body_text += substituteTokenText(tok, callArgsByGroup, def.m_argGroups);
        MapperFile body_idx = ctx.add_source("<macro_body>", body_text);
        LexemeSequence body_lexemes;
        try {
            body_lexemes = Lexer::tokenize(ctx, body_idx);
        } catch (...) {
            body_lexemes.clear();
        }
        result = recurseExpand(std::move(body_lexemes));
    } else {
        LexemeSequence substituted = substituteArgs(def.m_body, callArgsByGroup, def.m_argGroups);
        result = recurseExpand(std::move(substituted));
    }
    if (!def.m_expectedAfter.empty()) {
        if (pos >= lexemes.size()) {
            MapperLocation errLoc = (pos > 0) ? lexemes[pos - 1].pos : def.m_bodyRange.end;
            ctx.diag().report(errLoc, Severity::Error, "macro '{}' expected one of: {} after expansion, but got end of input",
                              std::string_view{def.m_nameLexemes[0]}, [&]() -> std::string {
                                  std::string out;
                                  for (std::size_t i = 0; i < def.m_expectedAfter.size(); ++i) {
                                      if (i > 0)
                                          out += ", ";
                                      out += def.m_expectedAfter[i].isKind ? ParserToken::name(def.m_expectedAfter[i].kind)
                                                                           : "'" + def.m_expectedAfter[i].text + "'";
                                  }
                                  return out;
                              }());
        } else {
            const Lexeme& next = lexemes[pos];
            bool matched = false;
            for (const auto& et : def.m_expectedAfter) {
                if (et.isKind) {
                    if (next.kind == et.kind) {
                        matched = true;
                        break;
                    }
                } else {
                    if (next.size() == et.text.size() && memcmp(next.data(), et.text.data(), et.text.size()) == 0) {
                        matched = true;
                        break;
                    }
                }
            }
            if (!matched) {
                ctx.diag().report(
                    next.pos, Severity::Error, "macro '{}' expected one of: {} after expansion, but got '{}'", std::string_view{def.m_nameLexemes[0]},
                    [&]() -> std::string {
                        std::string out;
                        for (std::size_t i = 0; i < def.m_expectedAfter.size(); ++i) {
                            if (i > 0)
                                out += ", ";
                            out += def.m_expectedAfter[i].isKind ? ParserToken::name(def.m_expectedAfter[i].kind) : "'" + def.m_expectedAfter[i].text + "'";
                        }
                        return out;
                    }(),
                    std::string_view{next});
            }
        }
    }
    if (pos < lexemes.size() && lexemes[pos].kind == ParserToken::Kind::SEMICOLON)
        pos++;
    if (!result.empty()) {
        MapperLocation first_loc = result.front()->range.begin, last_loc = result.back()->range.end;
        ctx.addMacroMapping(MapperRange(first_loc, last_loc), def.m_bodyRange);
    }
    return result;
}

// ============================================================
// processAttrGroup
// ============================================================

TokenSequence MMProcessor::processAttrGroup(Context& ctx, MacroTable& macros, int& recursionDepth, const Lexeme& startLex, const LexemeSequence& lexemes,
                                            std::size_t& pos, std::set<std::string>* expandedMacros, HygienicMap* hygienicMap) {
    ++pos;
    LexemeSequence attr_lexemes;
    bool found_end = false;
    while (pos < lexemes.size()) {
        if (lexemes[pos].kind == ParserToken::Kind::ATTR_COMPLETE) {
            found_end = true;
            ++pos;
            break;
        }
        attr_lexemes.push_back(lexemes[pos]);
        ++pos;
    }
    if (!found_end) {
        ctx.diag().report(startLex.pos, Severity::Error, "unterminated '@[' — expected ']@'");
        return {};
    }
    std::set<std::string> expanded_macros_in_attr;
    std::size_t attr_pos = 0;
    TokenSequence processed_attr = processInternal(ctx, macros, recursionDepth, attr_lexemes, attr_pos, &expanded_macros_in_attr, hygienicMap);
    TokenSequence result;
    auto attr_token = TokenInfo::make(ParserToken::Kind::ATTR, "", MapperRange{});
    attr_token->m_sequence = std::move(processed_attr);
    result.push_back(std::move(attr_token));
    if (!expanded_macros_in_attr.empty()) {
        if (expandedMacros)
            for (const auto& n : expanded_macros_in_attr)
                expandedMacros->insert(n);
        TokenSequence depend_seq;
        depend_seq.push_back(TokenInfo::make(ParserToken::Kind::Ident, "depend_macro", MapperRange{}));
        depend_seq.push_back(TokenInfo::make(ParserToken::Kind::LPAREN, "(", MapperRange{}));
        bool first = true;
        for (const auto& mn : expanded_macros_in_attr) {
            if (!first)
                depend_seq.push_back(TokenInfo::make(ParserToken::Kind::COMMA, ",", MapperRange{}));
            first = false;
            std::string quoted = "\"" + mn + "\"";
            depend_seq.push_back(TokenInfo::make(ParserToken::Kind::StringLiteral, std::move(quoted), MapperRange{}));
        }
        depend_seq.push_back(TokenInfo::make(ParserToken::Kind::RPAREN, ")", MapperRange{}));
        auto depend_attr_token = TokenInfo::make(ParserToken::Kind::ATTR, "", MapperRange{});
        depend_attr_token->m_sequence = std::move(depend_seq);
        result.push_back(std::move(depend_attr_token));
    }
    return result;
}

// ============================================================
// processInternal
// ============================================================

TokenSequence MMProcessor::processInternal(Context& ctx, MacroTable& macros, int& recursionDepth, const LexemeSequence& lexemes, std::size_t& pos,
                                           std::set<std::string>* expandedMacros, HygienicMap* hygienicMap) {
    TokenSequence result;
    while (pos < lexemes.size()) {
        const Lexeme& lex = lexemes[pos];
        if (lex.kind == ParserToken::Kind::MACRO_SEQ) {
            if (collectMacroDef(ctx, macros, lexemes, pos))
                continue;
            ctx.diag().report(lex.pos, Severity::Error, "unexpected '@@' — macro definition expected");
            pos++;
            continue;
        }
        if (is_concatenatable_token(lex.kind)) {
            result.push_back(concatStringTokens(lexemes, pos));
            continue;
        }
        if (lex.kind == ParserToken::Kind::MANGLED) {
            result.push_back(TokenInfo::make(ParserToken::Kind::Ident, std::string{lex}, MapperRange{lex.pos, lex.pos}));
            ++pos;
            continue;
        }
        if (is_id_start(lex.kind) || is_namespace(lex.kind)) {
            result.push_back(buildIdentToken(lexemes, pos));
            continue;
        }
        if (lex.kind == ParserToken::Kind::MACRO) {
            std::string_view macro_name{lex};
            if (!macro_name.empty() && macro_name[0] == '@')
                macro_name = macro_name.substr(1);
            if (!macro_name.empty() && macro_name.back() == '^')
                macro_name = macro_name.substr(0, macro_name.size() - 1);
            if (macro_name.empty()) {
                ctx.diag().report(lex.pos, Severity::Error, "empty macro name");
                pos++;
                continue;
            }

            if (macro_name == "__LEXEME_NEXT__") {
                pos++;
                if (pos < lexemes.size() && lexemes[pos].kind == ParserToken::Kind::LPAREN) {
                    pos++;
                    std::vector<ExpectedToken> expected;
                    while (pos < lexemes.size() && lexemes[pos].kind != ParserToken::Kind::RPAREN) {
                        if (lexemes[pos].kind == ParserToken::Kind::COMMA) {
                            pos++;
                            continue;
                        }
                        if (is_string_token(lexemes[pos].kind)) {
                            std::string text{lexemes[pos]};
                            text = unescape(text);
                            expected.push_back({false, ParserToken::Kind::END, std::move(text)});
                            pos++;
                            continue;
                        }
                        if (lexemes[pos].kind == ParserToken::Kind::NAME) {
                            std::string_view name{lexemes[pos]};
                            const auto* kindPtr = ParserToken::from_name(name);
                            if (kindPtr)
                                expected.push_back({true, *kindPtr, {}});
                            else
                                ctx.diag().report(lexemes[pos].pos, Severity::Error, "unknown token name '{}' in @__LEXEME_NEXT__", name);
                            pos++;
                            continue;
                        }
                        ctx.diag().report(lexemes[pos].pos, Severity::Error, "unexpected token in @__LEXEME_NEXT__ — expected token name or string literal");
                        pos++;
                    }
                    if (pos < lexemes.size() && lexemes[pos].kind == ParserToken::Kind::RPAREN)
                        pos++;
                    if (!expected.empty() && !macros.empty()) {
                        auto& last_module = macros.back();
                        for (auto& [key, defs] : last_module)
                            for (auto& def : defs)
                                if (def.m_expectedAfter.empty())
                                    def.m_expectedAfter = expected;
                    }
                    if (pos < lexemes.size() && lexemes[pos].kind != ParserToken::Kind::SEMICOLON)
                        ctx.diag().report(lexemes[pos].pos, Severity::Error, "@__LEXEME_NEXT__ must be the last element in the macro body");
                }
                if (pos < lexemes.size() && lexemes[pos].kind == ParserToken::Kind::SEMICOLON)
                    pos++;
                continue;
            }

            if (macro_name == "__HYGIENIC__") {
                pos++;
                if (pos < lexemes.size() && lexemes[pos].kind == ParserToken::Kind::LPAREN) {
                    pos++;
                    LexemeSequence arg_lexemes;
                    while (pos < lexemes.size() && lexemes[pos].kind != ParserToken::Kind::RPAREN) {
                        arg_lexemes.push_back(lexemes[pos]);
                        pos++;
                    }
                    if (pos < lexemes.size() && lexemes[pos].kind == ParserToken::Kind::RPAREN)
                        pos++;
                    std::string ident_text;
                    if (!arg_lexemes.empty() && (is_id_start(arg_lexemes[0].kind) || is_namespace(arg_lexemes[0].kind))) {
                        std::size_t apos = 0;
                        auto idTok = buildIdentToken(arg_lexemes, apos);
                        if (idTok->kind == ParserToken::Kind::Ident && apos == arg_lexemes.size())
                            ident_text = idTok->text;
                    }
                    if (ident_text.empty()) {
                        ctx.diag().report(lex.pos, Severity::Error, "@__HYGIENIC__ requires an identifier argument");
                    } else {
                        if (!hygienicMap) {
                            HygienicMap localMap;
                            ident_text = makeHygienicName(ident_text, localMap);
                        } else {
                            ident_text = makeHygienicName(ident_text, *hygienicMap);
                        }
                        result.push_back(TokenInfo::make(ParserToken::Kind::Ident, std::move(ident_text), MapperRange{lex.pos, lex.pos}));
                    }
                } else {
                    ctx.diag().report(lex.pos, Severity::Error, "@__HYGIENIC__ expects '(' after macro name");
                }
                if (pos < lexemes.size() && lexemes[pos].kind == ParserToken::Kind::SEMICOLON)
                    pos++;
                continue;
            }

            if (macro_name.size() >= 4 && macro_name.starts_with("__") && macro_name.ends_with("__")) {
                auto predefined = handlePredefinedMacro(ctx, macro_name, lex.pos.fileIdx(), lex.pos);
                if (!predefined.empty()) {
                    result.insert(result.end(), std::make_move_iterator(predefined.begin()), std::make_move_iterator(predefined.end()));
                    pos++;
                    continue;
                }
                pos++;
                continue;
            }

            // Каждый вызов макроса получает свой hygienicMap (процесс Internal передаёт nullptr).
            // Все вложенные раскрытия (рекурсивные) разделяют один map.
            std::size_t pos_before = pos;
            TokenSequence expanded = expandMacroLexeme(ctx, macros, recursionDepth, macro_name, lexemes, pos, expandedMacros, nullptr);
            if (pos == pos_before) {
                pos++;
                if (pos < lexemes.size() && lexemes[pos].kind == ParserToken::Kind::SEMICOLON)
                    pos++;
                continue;
            }
            result.insert(result.end(), std::make_move_iterator(expanded.begin()), std::make_move_iterator(expanded.end()));
            continue;
        }
        if (lex.kind == ParserToken::Kind::MACRO_STR) {
            result.push_back(makeStringLiteral(lex));
            ++pos;
            continue;
        }
        if (lex.kind == ParserToken::Kind::MODULE) {
            ctx.diag().report(lex.pos, Severity::Error, "unimplemented token '{}' — module processing is not implemented", ParserToken::name(lex.kind));
            ++pos;
            continue;
        }
        if (lex.kind == ParserToken::Kind::ATTR) {
            TokenSequence attr_result = processAttrGroup(ctx, macros, recursionDepth, lex, lexemes, pos, expandedMacros, hygienicMap);
            result.insert(result.end(), std::make_move_iterator(attr_result.begin()), std::make_move_iterator(attr_result.end()));
            continue;
        }
        if (lex.kind == ParserToken::Kind::ATTR_COMPLETE) {
            ctx.diag().report(lex.pos, Severity::Error, "unexpected ']@' without '@['");
            ++pos;
            continue;
        }
        result.push_back(TokenInfo::make(lex));
        ++pos;
    }
    return result;
}

// ============================================================
// compileFromSource (public static)
// ============================================================

void MMProcessor::compileFromSource(Context& ctx, MacroTable& macros, std::string_view source) {
    auto src_idx = ctx.add_source("<dsl_src>", std::string{source});
    auto lexemes = Lexer::tokenize(ctx, src_idx);
    if (macros.empty())
        macros.emplace_back();
    int recursionDepth = 0;
    std::size_t pos = 0;
    processInternal(ctx, macros, recursionDepth, lexemes, pos, nullptr, nullptr);
}

// ============================================================
// process (public static)
// ============================================================

TokenSequence MMProcessor::process(Context& ctx, const LexemeSequence& lexemes, std::shared_ptr<MacroTable> macros) {
    bool created = !macros;
    if (created)
        macros = std::make_shared<MacroTable>();
    macros->emplace_back();
    int recursionDepth = 0;
    std::size_t pos = 0;
    HygienicMap hygienicMap;
    TokenSequence result = processInternal(ctx, *macros, recursionDepth, lexemes, pos, nullptr, &hygienicMap);
    if (created)
        macros->pop_back();
    return result;
}

} // namespace trust