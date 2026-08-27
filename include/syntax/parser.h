#ifndef TRUST_SYNTAX_PARSER_H_
#define TRUST_SYNTAX_PARSER_H_

#include <cstddef>
#include <functional>
#include <memory>
#include <vector>

namespace trust {
class AstNodeBase;
using AstNodePtr = std::shared_ptr<AstNodeBase>;
class Context;
} // namespace trust

#include "syntax/types.h"

#include "syntax/predef_macro.hpp"
#include "syntax/pragma_evaluator.hpp"

#include "syntax/warning_push.h"
#include "parser.yy.h"
#include "syntax/warning_pop.h"

namespace trust {
TermPtr ProcessMacro(Parser& parser, TermPtr& term);

// Лимиты раскрытия макросов (защита от бесконечной рекурсии):
// - kMacroNestingLimit   - максимальная ГЛУБИНА вложенности в одной цепочке раскрытий;
// - kMacroExpansionLimit - максимальное СУММАРНОЕ число раскрытий в пределах одного оператора
//   (ловит самовоспроизведение макроса через границы чтений парсера, когда глубина не копится).
inline constexpr std::size_t kMacroNestingLimit = 100;
inline constexpr std::size_t kMacroExpansionLimit = 1000;

enum class ExpandMacroResult : uint8_t {
    Continue,
    Break,
    Goto,
};
ExpandMacroResult ExpandTermMacro(Parser& parser);

/** Последние токены, реально потреблённые bison, для позиции синтаксической ошибки.
 *  Лексер читает токены упреждающе в m_macro_analisys_buff (до ';'), поэтому flex-курсор
 *  в момент error() «убегает» вперёд и непригоден; надёжная позиция - здесь, у токенов,
 *  реально отданных bison через GetNextToken. */
struct RecentTokens {
    trust::MapperRange cur{};  ///< последний отданный bison токен (текущий lookahead)
    trust::MapperRange prev{}; ///< предыдущий (последний корректно сдвинутый)

    void set(trust::MapperRange r) noexcept {
        prev = cur;
        cur = r;
    }
    void setEndOfLast() noexcept { set(trust::MapperRange(cur.end, cur.end)); }
    void reset() noexcept {
        cur = {};
        prev = {};
    }

    /** Диапазон синтаксической ошибки (реализация - в src/syntax/parser.cpp). */
    trust::MapperRange syntaxErrorRange(const trust::Context& ctx) const;
};

/** The Driver class brings together all components. It creates an instance of
 * the Parser and Scanner classes and connects them. Then the input stream is
 * fed into the scanner object and the parser gets it's token
 * sequence. Furthermore the driver object is available in the grammar rules as
 * a parameter. Therefore the driver class contains a reference to the
 * structure into which the parsed data is saved. */
class Parser {
  public:
    Parser(trust::Context& ctx, PostLexerType* postlex = nullptr, bool pragma_enable = true, bool macro_expand = true);

    virtual ~Parser() {}

    /// Реестр предопределённых макросов (@__...__) и их раскрытие.
    syntax::PredefMacroResolver& predef() { return m_predef; }
    const syntax::PredefMacroResolver& predef() const { return m_predef; }
    /// Обработчик прагм (@__PRAGMA_*, @__OPTION_*, @__HYGIENIC__).
    syntax::PragmaEvaluator& pragma() { return m_pragma; }
    const syntax::PragmaEvaluator& pragma() const { return m_pragma; }

    // To demonstrate pure handling of parse errors, instead of
    // simply dumping them on the standard error output, we will pass
    // them to the driver using the following two member functions.

    /** Error handling with associated line number. This can be modified to
     * output the error e.g. to a dialog box. */
    void error(const class location& l, const std::string& m);

    /** General error handling. This can be modified to output the error
     * e.g. to a dialog box. */
    void error(const std::string& m);

    /** Pointer to the current lexer instance, this is used to connect the
     * parser to the scanner. It is used in the yylex macro. */
    class Scanner* lexer;

    /** Reference to the calculator context filled during parsing of the
     * expressions. */

    void AstAddTerm(TermPtr val);

    /// Контекст, с которым работает парсер (макросы, source, опции).
    trust::Context& context() const { return m_ctx; }

    /// Коллбек: сырой терм, прочитанный из лексера (до раскрытия макроса), в порядке обработки.
    /// Вызывается в GetNextToken для КАЖДОГО raw-токена (включая токены макроопределений,
    /// буферные и т.п.) — парсер продолжает работать как обычно, событие лишь передаётся
    /// внешнему потребителю (напр. форматтеру) для раскладки.
    std::function<void(const Term&)> on_token;

    TermPtr GetAst();

    SequenceType m_macro_analisys_buff; ///< Последовательность лексем для анализа на наличие макросов

    /// Последние токены, реально потреблённые bison - единственный источник позиции
    /// синтаксической ошибки (flex-курсор при упреждающей буферизации GetNextToken
    /// «убегает» вперёд и для позиции ошибки непригоден).
    RecentTokens m_recent;

    bool m_no_macro;
    bool m_enable_pragma;
    bool m_expand_module = false; ///< Разрешает загрузку модулей при парсинге

    /// Признак нахождения внутри скобок атрибута `@[ ... ]@` (между ATTRIBUTE и ATTR_COMPLETE).
    /// Внутри атрибута макро-имена НЕ раскрываются: содержимое атрибута - имя атрибута и
    /// параметры-литералы (не код). Это нужно, чтобы имя встроенного атрибута, совпадающее
    /// с именем keyword-макроса (напр. func_const), не раскрывалось (иначе бесконечная рекурсия).
    /// Явный `@`-макрос внутри атрибута - ошибка (см. parser.cpp GetNextToken).
    bool m_in_attr = false;

    /// Глубина раскрытия макросов (защита от бесконечной рекурсии)
    size_t m_macro_depth = 0;

    /// Суммарное число раскрытий макросов в текущем операторе (защита от самовоспроизведения
    /// через границы чтений). Сбрасывается на границе оператора (буфер анализа пуст) в GetNextToken.
    size_t m_macro_expansion_total = 0;

    /// Один раз на оператор уже выдан отчёт о рекурсивном макросе (guard в macro.cpp ExpandTermMacro),
    /// чтобы не спамить ошибкой на каждом раскрытии. Сбрасывается на границе оператора.
    bool m_macro_recursion_reported = false;

    parser::token_type GetNextToken(TermPtr* yylval);
    //        TermPtr MacroEval(const TermPtr &term);

    /// Парсит текстовый фрагмент (не файл/модуль) под указанным «фиктивным» именем
    /// источника. Имена фиктивных источников помечаются префиксом '@' (in-memory,
    /// файла на диске нет). По умолчанию "@input"; для встроенного DSL - "@trust/dsl".
    TermPtr ParseText(std::string_view text, std::string_view sourceName = "@input", bool expand_module = false);

    /// Парсит из уже зарегистрированного source-файла (не создавая in-memory
    /// псевдо-источник с префиксом '@'). Используется модульным загрузчиком для
    /// главного файла/модуля, чтобы маппинги (mapStart/mapStop) привязывались к
    /// реальному файлу.
    TermPtr ParseWithSource(trust::MapperFile src, bool expand_module = false);

    // Собирает термин из последовательности лексем и удаляет их из входного буфера.
    // ctx задаёт контекст, из которого наследуется Macro; macro_expand=false
    // отключает раскрытие макросов (специальные случаи: тесты, идентификаторы определений).
    static size_t ParseTerm(TermPtr& term, const SequenceType& buffer, trust::Context& ctx, const size_t skip = 0, bool pragma_enable = true,
                            bool macro_expand = true);
    static TermPtr ParseTerm(const char* proto, trust::Context& ctx, bool pragma_enable = true, bool macro_expand = true);

    TermPtr CheckModuleTerm(const TermPtr& term);

    static size_t SkipBrackets(const SequenceType& buffer, size_t offset);

    trust::Context& m_ctx;

    syntax::PredefMacroResolver m_predef;
    syntax::PragmaEvaluator m_pragma;

    MacroPtr m_macro;
    bool m_is_lexer_complete;

  private:
    TermPtr m_ast;
    PostLexerType* m_postlex;
};

} // namespace trust

#endif // TRUST_SYNTAX_PARSER_H_
