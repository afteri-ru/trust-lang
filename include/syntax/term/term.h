#pragma once
#ifndef TRUST_SYNTAX_TERM_H_
#define TRUST_SYNTAX_TERM_H_

#include <cstring>
#include <iosfwd>
#include <optional>
#include <variant>

#include "syntax/term/term_types.h"
#include "utils/partial_switch.hpp"

#include "location/location.hpp"
#include "location/module_index.hpp"

#include "syntax/util/warning_push.h"
#include "syntax/parser.h"
#include "syntax/util/warning_pop.h"

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

    // --- Getters and inline helpers ---
    inline TermID getTermID() const { return m_id; }

    /// Текст терма только для чтения. Для source-backed термов это view в буфер
    /// SourceMapWriter; для синтезированных - owned строка, пока Term живёт в
    /// синтаксической фазе. Интернирование в пул строк компиляции выполняет конвертация
    /// (узлы-носители `WithText`/`HasText`, см. ast/token_base.hpp).
    /// ВНИМАНИЕ: `getText()` и `m_mapperRange` - НЕЗАВИСИМЫЕ факты. При раскрытии макроса
    /// spelling остаётся из определения макроса, а range переприсваивается на call-site
    /// (или становится невалидным). Выводить текст из range нельзя.
    inline std::string_view getText() const {
        return std::visit([](const auto& v) -> std::string_view { return v; }, m_text);
    }

    /// Явная установка собственной строки (только синтаксическая фаза).
    inline void setTextOwned(std::string text) { m_text.emplace<std::string>(std::move(text)); }

    /// Мутируемая строка для macro assembly (единственный путь записи; текст становится owned).
    inline std::string& mutableText() {
        if (std::holds_alternative<std::string_view>(m_text)) {
            auto sv = std::get<std::string_view>(m_text);
            m_text.emplace<std::string>(sv);
        }
        return std::get<std::string>(m_text);
    }
    inline bool isCall() const { return m_args.has_value(); }

    inline bool isMacro() const { return m_id == TermID::MACRO_DEL || (m_id == TermID::MACRO_SEQ && m_right != nullptr); }

    // Выбор формы вывода в toString(): блочные термы печатаются как блоки, не как список элементов.
    inline bool isBlock() const {
        // Частичный предикат множества (осознанно): прочие TermID - не блочные термы.
        TRUST_PARTIAL_ENUM_SWITCH_BEGIN
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
        TRUST_PARTIAL_ENUM_SWITCH_END
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

    void RightToBlock(SequenceType& vect, bool remove = true);

    TermPtr AppendBlock(const TermPtr& item, TermID id, bool force = false);

    TermPtr Last();

    inline TermPtr GetType() { return m_type; }

    void FinalizeAndTest(TermID id);

    TermID m_id;

    TermPtr m_left;
    TermPtr m_right;

    // На TYPE-узлах m_type хранит ARGS-терм размерностей [...], на узлах-значениях - тип.
    // Единое поле последовательности: и верхнеуровневая `sequence` (SEQUENCE), и
    // вложенные `{ ... }`/тела конструкций (BLOCK/body) лежат здесь (см. MEMORY.md).
    // Сайт импорта несёт список индексов модулей (`m_moduleIndices`): обычный импорт — один
    // элемент, групповой (`\\module()`) — несколько; тела конвертируются загрузчиком.
    SequenceType m_sequence;
    TermPtr m_type;

    /// Документирующие комментарии (`///`, `##`, `/**`, т.ч. хвостовой `///<`/`##<`),
    /// привязанные грамматикой к терму-идентификатору (объявлению). Для не-объявлений
    /// док остаётся отдельным sibling-узлом (makeDocBundle/appendDocs), этот слот пуст.
    /// Источник для AstNodeBase::documentation (см. TermToAstConverter::convert).
    std::vector<TermPtr> m_docs;

    /// Нативный шаблон-тип. Наличие (has_value) - отличительный признак шаблона, значение -
    /// список типов-параметров (объявление `<T> %... := ...;`) либо типов-аргументов
    /// (использование `vector<Int32>`). Различает template-аргументы от кортежных параметров
    /// `:Type(...)` в visit_TYPE и помечает тип-конструктор в visit_CREATE_NAME.
    std::optional<ArgsList> m_template;

    std::optional<ArgsList> m_args;

    SequenceType m_attr;

  public:
    parser::token_type m_lexer_type;
    trust::MapperRange m_mapperRange;

    /// Синтаксический маркер типового пакета: `<T...>` (template_param), `xs: T...`
    /// (параметр функции) или `:T...` (аргумент шаблона). Семантика превращает его в
    /// PackExpansion(T) / FunctionTypeData::paramPackType.
    bool m_isPack = false;

    /// Индексы загруженных модулей для MODULE-терма `\module(...)` (ставит загрузчик при
    /// импорте). Пусто — терм не является сайтом загруженного модуля. Для обычного импорта
    /// один элемент, для группового — несколько; поиск индексов по Term-телу не выполняется.
    std::vector<ModuleIndex> m_moduleIndices;

    ArgsPair& at(const int64_t index);

  private:
    std::variant<std::string, std::string_view> m_text;
};

/// Рекурсивно печатает термы дерева в формате "текст\t<терм>" (режим --emit-lexemes).
void dumpTermTree(const TermPtr& term, std::ostream& out);

} // namespace trust
#endif // TRUST_SYNTAX_TERM_H_