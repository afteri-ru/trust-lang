#include "diag/diag.hpp"
#include "parser/mmproc.hpp"
#include "ast/token_info.hpp"
#include "parser/lexer.hpp"
#include <cstdlib>
#include <format>
#include <stdexcept>
#include <string>

namespace trust {

// ============================================================
// currentMacros
// ============================================================

const std::unordered_map<std::string, MacroDef>& MMProcessor::currentMacros(const MacroTable& macros) noexcept {
    // Всегда есть хотя бы один элемент (создаётся в process)
    return macros.back();
}

// ============================================================
// escape / unescape (unchanged from original)
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

/// Проверить, является ли лексема допустимой в имени макроса (между @@ ... @@)
static bool is_macro_name_lexeme(ParserToken::Kind k) noexcept {
    return k == ParserToken::Kind::NAME || k == ParserToken::Kind::LOCAL || k == ParserToken::Kind::NATIVE;
}

// ============================================================
// makeStringLiteral
// ============================================================

TokenPtr MMProcessor::makeStringLiteral(const Lexeme& lex) {
    std::string text(lex.data(), lex.size());
    bool is_raw = (lex.kind == ParserToken::Kind::STRWIDE_RAW || lex.kind == ParserToken::Kind::STRCHAR_RAW);
    if (!is_raw) {
        text = unescape(text);
    }
    MapperRange range{lex.pos, lex.pos};
    return TokenInfo::make(ParserToken::Kind::StringLiteral, std::move(text), range);
}

// ============================================================
// splitArgsByComma
// ============================================================

std::vector<TokenSequence> MMProcessor::splitArgsByComma(const TokenSequence& tokens) {
    std::vector<TokenSequence> result;
    if (tokens.empty())
        return result;

    TokenSequence current;
    int depth_paren = 0;
    int depth_bracket = 0;
    int depth_brace = 0;

    for (const auto& tok : tokens) {
        if (tok->kind == ParserToken::Kind::LPAREN) {
            depth_paren++;
            current.push_back(tok);
        } else if (tok->kind == ParserToken::Kind::RPAREN) {
            depth_paren--;
            current.push_back(tok);
        } else if (tok->kind == ParserToken::Kind::LBRACKET) {
            depth_bracket++;
            current.push_back(tok);
        } else if (tok->kind == ParserToken::Kind::RBRACKET) {
            depth_bracket--;
            current.push_back(tok);
        } else if (tok->kind == ParserToken::Kind::LBRACE) {
            depth_brace++;
            current.push_back(tok);
        } else if (tok->kind == ParserToken::Kind::RBRACE) {
            depth_brace--;
            current.push_back(tok);
        } else if (tok->kind == ParserToken::Kind::COMMA && depth_paren == 0 && depth_bracket == 0 && depth_brace == 0) {
            result.push_back(std::move(current));
            current.clear();
        } else {
            current.push_back(tok);
        }
    }

    // Добавляем последний аргумент (даже если пустой)
    result.push_back(std::move(current));
    return result;
}

// ============================================================
// substituteTokenText
// ============================================================

std::string MMProcessor::substituteTokenText(const std::string& text, const std::vector<TokenSequence>& args, const std::vector<std::string>& paramNames) {
    if (text.empty())
        return text;

    std::string result;
    result.reserve(text.size());

    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '@' && i + 1 < text.size() && text[i + 1] == '$') {
            std::size_t start = i + 2;
            std::string ref;
            std::size_t j = start;

            bool is_star = false;
            bool is_hash = false;
            bool is_ellipsis = false;

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

            if (is_star || is_ellipsis) {
                std::string all_args;
                for (std::size_t ai = 0; ai < args.size(); ++ai) {
                    if (ai > 0)
                        all_args += ", ";
                    bool first = true;
                    for (const auto& tok : args[ai]) {
                        if (!first)
                            all_args += " ";
                        all_args += tok->text;
                        first = false;
                    }
                }
                result += all_args;
                i = j - 1;
                continue;
            }

            if (is_hash) {
                result += std::to_string(args.size());
                i = j - 1;
                continue;
            }

            int arg_index = -1;

            bool is_numeric = true;
            for (char c : ref) {
                if (!std::isdigit(static_cast<unsigned char>(c))) {
                    is_numeric = false;
                    break;
                }
            }

            if (is_numeric && !ref.empty()) {
                arg_index = std::stoi(ref) - 1;
            } else {
                for (std::size_t pi = 0; pi < paramNames.size(); ++pi) {
                    if (paramNames[pi] == ref) {
                        arg_index = static_cast<int>(pi);
                        break;
                    }
                }
                if (arg_index < 0) {
                    result += text.substr(i, j - i);
                    i = j - 1;
                    continue;
                }
            }

            if (arg_index >= 0 && arg_index < static_cast<int>(args.size())) {
                bool first = true;
                for (const auto& tok : args[arg_index]) {
                    if (!first)
                        result += " ";
                    result += tok->text;
                    first = false;
                }
            } else if (arg_index >= 0 && arg_index < static_cast<int>(paramNames.size())) {
                result += paramNames[arg_index];
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

TokenSequence MMProcessor::substituteArgs(const TokenSequence& body, const std::vector<TokenSequence>& args, const std::vector<std::string>& paramNames) {
    TokenSequence result;
    result.reserve(body.size());

    for (const auto& token : body) {
        if (token->kind == ParserToken::Kind::MACRO_ARGNAME) {
            std::string arg_name = token->text;
            if (arg_name.size() >= 2 && arg_name[0] == '@' && arg_name[1] == '$') {
                arg_name = arg_name.substr(2);
            }
            int arg_index = -1;
            for (std::size_t pi = 0; pi < paramNames.size(); ++pi) {
                if (paramNames[pi] == arg_name) {
                    arg_index = static_cast<int>(pi);
                    break;
                }
            }
            if (arg_index >= 0 && arg_index < static_cast<int>(args.size())) {
                // Вставляем все токены аргумента с сохранением их исходных типов
                for (const auto& arg_tok : args[arg_index]) {
                    auto t = TokenInfo::make(arg_tok->kind, arg_tok->text, token->range);
                    result.push_back(std::move(t));
                }
            }
            continue;
        }

        if (token->kind == ParserToken::Kind::MACRO_ARGPOS) {
            std::string num_str = token->text;
            if (num_str.size() >= 2 && num_str[0] == '@' && num_str[1] == '$') {
                num_str = num_str.substr(2);
            }
            int arg_index = -1;
            try {
                arg_index = std::stoi(num_str) - 1;
            } catch (...) {
            }
            if (arg_index >= 0 && arg_index < static_cast<int>(args.size())) {
                for (const auto& arg_tok : args[arg_index]) {
                    auto t = TokenInfo::make(arg_tok->kind, arg_tok->text, token->range);
                    result.push_back(std::move(t));
                }
            }
            continue;
        }

        if (token->kind == ParserToken::Kind::MACRO_ARGUMENT) {
            std::string all_args;
            for (std::size_t ai = 0; ai < args.size(); ++ai) {
                if (ai > 0)
                    all_args += ", ";
                bool first = true;
                for (const auto& tok : args[ai]) {
                    if (!first)
                        all_args += " ";
                    all_args += tok->text;
                    first = false;
                }
            }
            auto arg_token = TokenInfo::make(ParserToken::Kind::Ident, std::move(all_args), token->range);
            result.push_back(std::move(arg_token));
            continue;
        }

        if (token->kind == ParserToken::Kind::MACRO_ARGCOUNT) {
            auto arg_token = TokenInfo::make(ParserToken::Kind::IntLiteral, std::to_string(args.size()), token->range);
            result.push_back(std::move(arg_token));
            continue;
        }

        auto new_token = TokenInfo::make(token->kind, token->text, token->range);
        new_token->text = substituteTokenText(token->text, args, paramNames);

        if (!token->m_sequence.empty()) {
            new_token->m_sequence = substituteArgs(token->m_sequence, args, paramNames);
        }

        result.push_back(std::move(new_token));
    }

    return result;
}

// ============================================================
// collectMacroDef
// ============================================================

bool MMProcessor::collectMacroDef(Context& ctx, MacroTable& macros, const LexemeSequence& lexemes, std::size_t& pos) {
    if (pos >= lexemes.size())
        return false;
    if (lexemes[pos].kind != ParserToken::Kind::MACRO_SEQ)
        return false;

    // Фиксируем позицию открывающего @@ для сообщений об ошибках
    MapperLocation macro_start_loc = lexemes[pos].pos;
    std::size_t start_pos = pos;

    // Пропускаем открывающий MACRO_SEQ (@@)
    std::size_t i = pos + 1;

    // Первая проверка: смотрим, похоже ли это на определение макроса
    while (i < lexemes.size() && lexemes[i].kind != ParserToken::Kind::MACRO_SEQ) {
        if (is_macro_name_lexeme(lexemes[i].kind)) {
            i++;
        } else if (lexemes[i].kind == ParserToken::Kind::LPAREN || lexemes[i].kind == ParserToken::Kind::RPAREN ||
                   lexemes[i].kind == ParserToken::Kind::COMMA) {
            i++;
        } else {
            return false;
        }
    }

    if (i >= lexemes.size() || lexemes[i].kind != ParserToken::Kind::MACRO_SEQ) {
        return false;
    }

    i++;

    if (i >= lexemes.size() || !is_creation_operator(lexemes[i].kind)) {
        return false;
    }

    // Это действительно определение макроса — фиксируем и повторно парсим
    pos = start_pos;

    pos++; // пропускаем открывающий @@

    std::string macro_name;
    std::vector<std::string> macro_params;
    bool in_parens = false;
    MapperLocation paren_open_loc{}; // позиция открывающей скобки для диагностики
    MapperLocation last_name_loc = macro_start_loc;

    while (pos < lexemes.size() && lexemes[pos].kind != ParserToken::Kind::MACRO_SEQ) {
        if (in_parens) {
            if (lexemes[pos].kind == ParserToken::Kind::RPAREN) {
                in_parens = false;
                pos++;
                continue;
            }
            if (lexemes[pos].kind == ParserToken::Kind::COMMA) {
                // Пропускаем запятую между аргументами внутри скобок
                pos++;
                continue;
            }
            if (lexemes[pos].kind == ParserToken::Kind::LOCAL) {
                std::string text(lexemes[pos].data(), lexemes[pos].size());
                std::string param_name = (text.size() > 1) ? text.substr(1) : text;
                macro_params.push_back(param_name);
            }
            pos++;
            continue;
        }

        if (lexemes[pos].kind == ParserToken::Kind::LPAREN) {
            in_parens = true;
            paren_open_loc = lexemes[pos].pos;
            pos++;
            continue;
        }

        if (is_macro_name_lexeme(lexemes[pos].kind)) {
            std::string text(lexemes[pos].data(), lexemes[pos].size());
            last_name_loc = lexemes[pos].pos;
            if (lexemes[pos].kind == ParserToken::Kind::LOCAL) {
                std::string param_name = (text.size() > 1) ? text.substr(1) : text;
                macro_params.push_back(param_name);
            } else {
                if (!macro_name.empty())
                    macro_name += ' ';
                macro_name += text;
            }
            pos++;
        } else {
            ctx.diag().report(lexemes[pos].pos, Severity::Error, "unexpected token '{}' in macro name", ParserToken::name(lexemes[pos].kind));
            // Пропускаем до закрывающего @@ или конца
            while (pos < lexemes.size() && lexemes[pos].kind != ParserToken::Kind::MACRO_SEQ) {
                pos++;
            }
            if (pos < lexemes.size() && lexemes[pos].kind == ParserToken::Kind::MACRO_SEQ) {
                pos++;
            }
            return true;
        }
    }

    if (in_parens) {
        ctx.diag().report(paren_open_loc.isValid() ? paren_open_loc : last_name_loc, Severity::Error, "unterminated '(' in macro arguments");
        // Пытаемся восстановиться: ищем закрывающий @@
        while (pos < lexemes.size() && lexemes[pos].kind != ParserToken::Kind::MACRO_SEQ) {
            pos++;
        }
        if (pos < lexemes.size())
            pos++;
        return true;
    }

    if (pos >= lexemes.size() || lexemes[pos].kind != ParserToken::Kind::MACRO_SEQ) {
        ctx.diag().report(last_name_loc.isValid() ? last_name_loc : macro_start_loc, Severity::Error, "unterminated macro name (expected '@@')");
        return true;
    }

    MapperLocation close_name_loc = lexemes[pos].pos;
    pos++; // пропускаем закрывающий @@

    if (pos >= lexemes.size()) {
        ctx.diag().report(close_name_loc, Severity::Error, "expected creation operator after macro name, got end of input");
        return true;
    }

    if (!is_creation_operator(lexemes[pos].kind)) {
        ctx.diag().report(lexemes[pos].pos, Severity::Error, "expected creation operator after macro name");
        return true;
    }

    MapperLocation op_loc = lexemes[pos].pos;
    pos++;

    MacroDef def;
    def.m_name = macro_name;
    def.m_params = macro_params;
    def.m_bodyType = MacroBodyType::kExpression;

    MapperLocation first_body_loc = (pos < lexemes.size()) ? lexemes[pos].pos : op_loc;
    MapperLocation last_consumed_loc = op_loc;

    if (pos < lexemes.size()) {
        if (lexemes[pos].kind == ParserToken::Kind::MACRO_SEQ) {
            def.m_bodyType = MacroBodyType::kTokenSequence;
            pos++;
            while (pos < lexemes.size() && lexemes[pos].kind != ParserToken::Kind::MACRO_SEQ) {
                def.m_body.push_back(TokenInfo::make(lexemes[pos]));
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
            def.m_body.push_back(makeStringLiteral(lexemes[pos]));
            last_consumed_loc = lexemes[pos].pos;
            pos++;
        } else {
            // Expression body: собираем токены до ';'
            while (pos < lexemes.size() && lexemes[pos].kind != ParserToken::Kind::SEMICOLON) {
                def.m_body.push_back(TokenInfo::make(lexemes[pos]));
                last_consumed_loc = lexemes[pos].pos;
                pos++;
            }
        }
    }

    // Сохраняем range тела макроса для SourceMapper
    def.m_bodyRange = MapperRange{first_body_loc, last_consumed_loc};

    if (pos < lexemes.size() && lexemes[pos].kind == ParserToken::Kind::SEMICOLON) {
        def.m_bodyRange.end = lexemes[pos].pos;
        pos++;
    } else {
        ctx.diag().report(last_consumed_loc, Severity::Error, "expected ';' after macro definition");
        // Не возвращаем true — макрос всё равно регистрируем
    }

    if (macro_name.empty()) {
        ctx.diag().report(first_body_loc, Severity::Error, "macro name cannot be empty");
        return true;
    }

    std::string key = macro_name.substr(0, macro_name.find(' '));

    // Проверяем уникальность по всему массиву таблиц
    for (const auto& module : macros) {
        if (module.count(key)) {
            ctx.diag().report(op_loc, Severity::Error, "macro '{}' redefined", macro_name);
            return true;
        }
    }

    // Регистрируем в текущем (последнем) элементе массива
    macros.back().emplace(key, std::move(def));

    return true;
}

// ============================================================
// expandMacro
// ============================================================

TokenSequence MMProcessor::expandMacro(Context& ctx, MacroTable& macros, int& recursionDepth, const std::string& name, const LexemeSequence& lexemes,
                                       std::size_t& pos) {
    TokenSequence result;

    if (recursionDepth >= kMaxRecursionDepth) {
        ctx.diag().report(lexemes[pos].pos, Severity::Error, "macro recursion depth exceeded (max {})", kMaxRecursionDepth);
        return result;
    }

    // Поиск макроса с конца массива к началу (последний модуль имеет приоритет)
    const MacroDef* def_ptr = nullptr;
    for (auto it = macros.rbegin(); it != macros.rend(); ++it) {
        auto found = it->find(name);
        if (found != it->end()) {
            def_ptr = &found->second;
            break;
        }
    }

    if (!def_ptr) {
        ctx.diag().report(lexemes[pos].pos, Severity::Error, "undefined macro '{}'", name);
        while (pos < lexemes.size() && lexemes[pos].kind != ParserToken::Kind::SEMICOLON) {
            pos++;
        }
        if (pos < lexemes.size())
            pos++;
        return result;
    }

    const MacroDef& def = *def_ptr;

    pos++; // пропускаем @name

    TokenSequence call_args_flat;
    bool has_paren_args = false;

    // Проверяем, есть ли скобки после имени макроса (аргументы в скобках)
    if (pos < lexemes.size() && lexemes[pos].kind == ParserToken::Kind::LPAREN) {
        has_paren_args = true;
        // Собираем токены внутри скобок
        pos++; // пропускаем (
        int depth = 1;
        while (pos < lexemes.size() && depth > 0) {
            if (lexemes[pos].kind == ParserToken::Kind::LPAREN) {
                depth++;
                call_args_flat.push_back(TokenInfo::make(lexemes[pos]));
            } else if (lexemes[pos].kind == ParserToken::Kind::RPAREN) {
                depth--;
                if (depth > 0) {
                    call_args_flat.push_back(TokenInfo::make(lexemes[pos]));
                }
            } else {
                call_args_flat.push_back(TokenInfo::make(lexemes[pos]));
            }
            pos++;
        }
        // Пропускаем ;
        if (pos < lexemes.size() && lexemes[pos].kind == ParserToken::Kind::SEMICOLON) {
            pos++;
        }
    } else if (!def.m_params.empty()) {
        // Шаблон с образцами — собираем токены до ;
        while (pos < lexemes.size() && lexemes[pos].kind != ParserToken::Kind::SEMICOLON) {
            call_args_flat.push_back(TokenInfo::make(lexemes[pos]));
            pos++;
        }
        if (pos < lexemes.size() && lexemes[pos].kind == ParserToken::Kind::SEMICOLON) {
            pos++;
        }
    } else {
        // Без аргументов — просто пропускаем до ;
        while (pos < lexemes.size() && lexemes[pos].kind != ParserToken::Kind::SEMICOLON) {
            pos++;
        }
        if (pos < lexemes.size() && lexemes[pos].kind == ParserToken::Kind::SEMICOLON) {
            pos++;
        }
    }

    // Разделяем аргументы по запятым, если были скобки
    std::vector<TokenSequence> call_args;
    if (has_paren_args && !call_args_flat.empty()) {
        call_args = splitArgsByComma(call_args_flat);
    } else if (!has_paren_args && !call_args_flat.empty()) {
        // Для шаблона без скобок — каждый токен отдельный аргумент
        for (const auto& tok : call_args_flat) {
            TokenSequence single;
            single.push_back(tok);
            call_args.push_back(std::move(single));
        }
    }

    TokenSequence expanded_body;

    if (def.m_bodyType == MacroBodyType::kStringLiteral) {
        // Строковое тело: сначала делаем текстовую подстановку аргументов,
        // затем лексируем результат и раскрываем рекурсивно
        std::string body_text;
        for (const auto& tok : def.m_body) {
            body_text += substituteTokenText(tok->text, call_args, def.m_params);
        }

        // Пере-лексируем результат подстановки
        MapperFile body_idx = ctx.add_source("<macro_body>", body_text);
        LexemeSequence body_lexemes;
        try {
            body_lexemes = Lexer::tokenize(ctx, body_idx);
        } catch (...) {
            // Если лексирование упало — вставляем как строку
            body_lexemes.clear();
        }

        // Раскрываем макросы внутри результата лексирования
        recursionDepth++;
        std::size_t recurse_pos = 0;
        expanded_body = processInternal(ctx, macros, recursionDepth, body_lexemes, recurse_pos);
        recursionDepth--;
    } else {
        // Expression или TokenSequence: обычная подстановка
        expanded_body = substituteArgs(def.m_body, call_args, def.m_params);

        recursionDepth++;
        LexemeSequence recurse_lexemes;
        for (const auto& tok : expanded_body) {
            recurse_lexemes.emplace_back(tok->kind, tok->text, tok->range.begin);
        }
        std::size_t recurse_pos = 0;
        expanded_body = processInternal(ctx, macros, recursionDepth, recurse_lexemes, recurse_pos);
        recursionDepth--;
    }

    // Регистрируем маппинг всего раскрытого макроса к его определению
    if (!expanded_body.empty()) {
        MapperLocation first_loc = expanded_body.front()->range.begin;
        MapperLocation last_loc = expanded_body.back()->range.end;
        MapperRange body_range{first_loc, last_loc};
        ctx.addMacroMapping(body_range, def.m_bodyRange);
    }

    return expanded_body;
}

// ============================================================
// processInternal
// ============================================================

TokenSequence MMProcessor::processInternal(Context& ctx, MacroTable& macros, int& recursionDepth, const LexemeSequence& lexemes, std::size_t& pos) {
    TokenSequence result;

    while (pos < lexemes.size()) {
        const Lexeme& lex = lexemes[pos];

        if (lex.kind == ParserToken::Kind::MACRO_SEQ) {
            if (collectMacroDef(ctx, macros, lexemes, pos)) {
                continue;
            }
            ctx.diag().report(lex.pos, Severity::Error, "unexpected '@@' — macro definition expected");
            pos++;
            continue;
        }

        if (is_concatenatable_token(lex.kind)) {
            std::string text(lex.data(), lex.size());
            MapperRange range{lex.pos, lex.pos};
            ParserToken::Kind kind = lex.kind;

            std::size_t j = pos + 1;
            while (j < lexemes.size() && lexemes[j].kind == kind) {
                text.append(lexemes[j].data(), lexemes[j].size());
                range.end = lexemes[j].pos;
                ++j;
            }

            bool is_raw = (kind == ParserToken::Kind::STRWIDE_RAW || kind == ParserToken::Kind::STRCHAR_RAW);
            if (!is_raw) {
                text = unescape(text);
            }
            result.push_back(TokenInfo::make(kind, std::move(text), range));
            pos = j;
            continue;
        }

        if (lex.kind == ParserToken::Kind::MANGLED) {
            std::string text(lex.data(), lex.size());
            MapperRange range{lex.pos, lex.pos};
            result.push_back(TokenInfo::make(ParserToken::Kind::Ident, std::move(text), range));
            ++pos;
            continue;
        }

        if (is_id_start(lex.kind) || is_namespace(lex.kind)) {
            std::string text;
            MapperRange range{lex.pos, lex.pos};
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
                    // :: без продолжения — откатываем j обратно на ::,
                    // чтобы :: был обработан как отдельный токен NAMESPACE
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
                // Только :: без имени → оставляем как NAMESPACE
                std::string ns_text(lexemes[pos].data(), lexemes[pos].size());
                MapperRange ns_range{lexemes[pos].pos, lexemes[pos].pos};
                result.push_back(TokenInfo::make(ParserToken::Kind::NAMESPACE, std::move(ns_text), ns_range));
                ++pos;
                continue;
            }

            result.push_back(TokenInfo::make(ParserToken::Kind::Ident, std::move(text), range));
            pos = j;
            continue;
        }

        // Вызов макроса (@name)
        if (lex.kind == ParserToken::Kind::MACRO) {
            std::string macro_name(lex.data(), lex.size());
            if (!macro_name.empty() && macro_name[0] == '@') {
                macro_name = macro_name.substr(1);
            }

            if (!macro_name.empty() && macro_name.back() == '^') {
                macro_name.pop_back();
            }

            if (macro_name.empty()) {
                ctx.diag().report(lex.pos, Severity::Error, "empty macro name");
                pos++;
                continue;
            }

            TokenSequence expanded = expandMacro(ctx, macros, recursionDepth, macro_name, lexemes, pos);
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
            // Собираем токены между @[ и ]@ в один ATTR-токен
            ++pos;
            TokenSequence attr_buffer;
            bool found_end = false;

            while (pos < lexemes.size()) {
                if (lexemes[pos].kind == ParserToken::Kind::ATTR_COMPLETE) {
                    found_end = true;
                    ++pos;
                    break;
                }
                attr_buffer.push_back(TokenInfo::make(lexemes[pos]));
                ++pos;
            }

            if (!found_end) {
                ctx.diag().report(lex.pos, Severity::Error, "unterminated '@[' — expected ']@'");
                continue;
            }

            auto attr_token = TokenInfo::make(ParserToken::Kind::ATTR, "", MapperRange{});
            attr_token->m_sequence = std::move(attr_buffer);
            result.push_back(std::move(attr_token));
            continue;
        }

        // Пропускаем ATTR_COMPLETE без предшествующего ATTR
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
// process (public static)
// ============================================================

TokenSequence MMProcessor::process(Context& ctx, const LexemeSequence& lexemes, std::shared_ptr<MacroTable> macros) {
    bool owns_table = false;
    if (!macros) {
        macros = std::make_shared<MacroTable>();
        macros->emplace_back(); // первый пустой модуль
        owns_table = true;
    }

    // Добавляем новый модуль (пустая таблица)
    macros->emplace_back();

    int recursionDepth = 0;
    std::size_t pos = 0;
    TokenSequence result = processInternal(ctx, *macros, recursionDepth, lexemes, pos);

    // Если таблица была создана внутри — удаляем временный элемент
    if (owns_table) {
        macros->pop_back();
    }

    return result;
}

} // namespace trust