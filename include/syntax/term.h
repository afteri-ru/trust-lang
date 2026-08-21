#pragma once
#ifndef TRUST_SYNTAX_TERM_H_
#define TRUST_SYNTAX_TERM_H_

#include <cstring>
#include <optional>
#include <variant>

#include "syntax/term_types.h"

#include "location/location.hpp"

#include "syntax/warning_push.h"
#include "syntax/parser.h"
#include "syntax/warning_pop.h"

#include <list>
#include "trust/version.h"

namespace trust {

typedef std::pair<std::string, TermPtr> ArgsPair;
typedef std::list<ArgsPair> ArgsList;

class Term : public std::enable_shared_from_this<Term> {
  public:
    // --- Dict-compatible interface (via m_args) ---
    inline size_t size() const { return m_args ? m_args->size() : 0; }

    ArgsPair& push_back(const ArgsPair& p);
    ArgsPair& push_back(const TermPtr value, const std::string& name = "");

    // Копирует текст - ручное создание (безопасно для локальных std::string и литералов).
    // lex_type - последний с дефолтом, для ручных вызовов не нужен.
    static TermPtr Create(TermID id, std::string text, trust::MapperRange mapperRange = {}, parser::token_type lex_type = parser::token_type::END);

    // View из (text, len) = std::string_view(text, len) - НЕ копирует.
    // Данные должны пережить Term (лексер: исходный текст SourceMapper).
    // lex_type - второй аргумент (сразу после id), только для lexer.l / parser.y.
    static TermPtr Create(TermID id, parser::token_type lex_type, const char* text, size_t len, trust::MapperRange mapperRange = {});

    static TermPtr CreateSymbol(char sym);

    // Маппинг одиночного символа → лексический TermID (LPAREN/RPAREN/...).
    static TermID symbolToID(char sym);

    // Маппинг лексического TermID → bison-токен (token::LPAREN/...).
    static parser::token_type tokenFromID(TermID id);

    TermPtr Clone();

    Term(TermID id, std::string text, trust::MapperRange mapperRange = {}, parser::token_type lex_type = parser::token_type::END);

    Term(TermID id, std::string_view text, trust::MapperRange mapperRange, parser::token_type lex_type);

    virtual ~Term() = default;

    // --- Getters and inline helpers (unchanged) ---
    inline TermID getTermID() const { return m_id; }
    inline std::string& getText() {
        if (std::holds_alternative<std::string_view>(m_text)) {
            auto sv = std::get<std::string_view>(m_text);
            m_text.emplace<std::string>(sv);
        }
        return std::get<std::string>(m_text);
    }
    inline std::string_view getText() const {
        return std::visit([](const auto& v) -> std::string_view { return v; }, m_text);
    }
    inline bool isCall() const { return m_args.has_value(); }

    inline bool isCreate() const {
        switch (m_id) {
        case TermID::APPEND:
        case TermID::CREATE_TYPE:
        case TermID::CREATE_NAME:
        case TermID::ASSIGN:
        case TermID::SWAP:
            return true;
        default:
            break;
        }
        return false;
    }

    inline bool isMacro() const { return m_id == TermID::MACRO_DEL || (isCreate() && m_left && m_left->m_id == TermID::MACRO_SEQ); }

    // Used in toString() - kept
    inline bool isBlock() const {
        switch (m_id) {
        case TermID::SEQUENCE:
        case TermID::BLOCK:
        case TermID::BLOCK_TRY:
        case TermID::BLOCK_PLUS:
        case TermID::BLOCK_MINUS:
            return true;
        default:
            break;
        }
        return false;
    }

  private:
    // --- Helper methods for toString() ---

    void appendParenItems_(std::string& str) const;
    void appendBracketItems_(std::string& str) const;
    void appendSemicolon_(std::string& str, bool nested) const;
    void appendBlockItems_(std::string& str, bool nested);
    void dump_items_(std::string& str) const;

  public:
    std::string toString(bool nested = false, bool suppressType = false);

    void AppendLeft(TermPtr item);
    void AppendRight(TermPtr item);
    void AppendText(const std::string& s);

    void RightToBlock(SequenceType& vect, bool remove = true);

    TermPtr AppendBlock(const TermPtr& item, TermID id, bool force = false);

    TermPtr Last();

    inline TermPtr GetType() { return m_type; }

    void FinalizeAndTest(TermID id);

    TermID m_id;

    TermPtr m_left;
    TermPtr m_right;

    // На TYPE-узлах m_type хранит ARGS-терм размерностей [...], на узлах-значениях - тип.
    // Для терма `\module(func)` loader кладёт сюда тело загруженного модуля (m_ast),
    // чтобы конвертация в AstNode была loader-free (рекурсивная конвертация m_sequence).
    // Единое поле последовательности: и верхнеуровневая `sequence` (SEQUENCE), и
    // вложенные `{ ... }`/тела конструкций (BLOCK/body) лежат здесь (см. MEMORY.md).
    SequenceType m_sequence;
    TermPtr m_type;

    /// Документирующие комментарии (`///`, `##`, `/**`, т.ч. хвостовой `///<`/`##<`),
    /// привязанные грамматикой к терму-идентификатору (объявлению). Для не-объявлений
    /// док остаётся отдельным sibling-узлом (makeDocBundle/appendDocs), этот слот пуст.
    /// Источник для AstNodeBase::documentation (см. TermToAstConverter::convert).
    std::vector<TermPtr> m_docs;

    std::optional<ArgsList> m_args;

    SequenceType m_attr;

  public:
    parser::token_type m_lexer_type;
    trust::MapperRange m_mapperRange;

    ArgsPair& at(const int64_t index);

  private:
    mutable std::variant<std::string, std::string_view> m_text;
};

} // namespace trust
#endif // TRUST_SYNTAX_TERM_H_