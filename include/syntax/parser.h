#ifndef TRUST_SYNTAX_PARSER_H_
#define TRUST_SYNTAX_PARSER_H_

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <type_traits>
#include <vector>

namespace trust {
class AstNodeBase;
using AstNodePtr = std::shared_ptr<AstNodeBase>;
} // namespace trust

#include "syntax/parser/parser_error.hpp"
#include "syntax/term/term_types.h"
#include "syntax/parser_services.hpp"

#include "syntax/predef_macro.hpp"
#include "syntax/macro/pragma_evaluator.hpp"

#include "syntax/util/warning_push.h"
#include "parser.yy.h"
#include "syntax/util/warning_pop.h"

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

    /** Диапазон синтаксической ошибки (реализация - в src/syntax/parser/parser.cpp). */
    trust::MapperRange syntaxErrorRange(const trust::ParserServices& ctx) const;
};

/** The Driver class brings together all components. It creates an instance of
 * the Parser and Scanner classes and connects them. Then the input stream is
 * fed into the scanner object and the parser gets it's token
 * sequence. Furthermore the driver object is available in the grammar rules as
 * a parameter. Therefore the driver class contains a reference to the
 * structure into which the parsed data is saved. */
class Parser {
  public:
    Parser(trust::ParserServices ctx, PostLexerType* postlex = nullptr, bool pragma_enable = true, bool macro_expand = true);

    template <class Ctx>
        requires(!std::is_same_v<std::remove_cvref_t<Ctx>, trust::ParserServices>)
    Parser(Ctx& ctx, PostLexerType* postlex = nullptr, bool pragma_enable = true, bool macro_expand = true)
    : Parser(trust::makeParserServices(ctx), postlex, pragma_enable, macro_expand) {}

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
    const trust::ParserServices& context() const { return m_ctx; }

    /// Коллбек: сырой терм, прочитанный из лексера (до раскрытия макроса), в порядке обработки.
    /// Вызывается в GetNextToken для КАЖДОГО raw-токена (включая токены макроопределений,
    /// буферные и т.п.) — парсер продолжает работать как обычно, событие лишь передаётся
    /// внешнему потребителю (напр. форматтеру) для раскладки.
    std::function<void(const Term&)> on_token;

    TermPtr GetAst();

    /// Диапазон последней синтаксической ошибки. Используется правилами восстановления
    /// грамматики, чтобы представить пропущенный фрагмент пустым SEQUENCE-термом с корректным
    /// диапазоном (диагностика уже выдана в error()).
    [[nodiscard]] MapperRange lastSyntaxErrorRange() const noexcept { return m_lastSyntaxErrorRange.value_or(MapperRange{}); }

    /// Достигнут ли настроенный лимит ошибок DiagnosticEngine (правила восстановления
    /// делают YYABORT). Лимит и итоговое сообщение принадлежат DiagnosticEngine.
    [[nodiscard]] bool syntaxErrorLimitReached() const;

    SequenceType m_macro_analisys_buff; ///< Последовательность лексем для анализа на наличие макросов

    /// Последние токены, реально потреблённые bison - единственный источник позиции
    /// синтаксической ошибки (flex-курсор при упреждающей буферизации GetNextToken
    /// «убегает» вперёд и для позиции ошибки непригоден).
    RecentTokens m_recent;

    bool m_no_macro;
    bool m_enable_pragma;
    bool m_expand_module = false; ///< Разрешает загрузку модулей при парсинге

    /// Признак нахождения внутри скобок атрибута `@[ ... @]` (между ATTRIBUTE и ATTR_COMPLETE).
    /// Внутри атрибута макро-имена НЕ раскрываются: содержимое атрибута - имя атрибута и
    /// параметры-литералы (не код). Это нужно, чтобы имя встроенного атрибута, совпадающее
    /// с именем keyword-макроса (напр. func_const), не раскрывалось (иначе бесконечная рекурсия).
    /// Явный `@`-макрос внутри атрибута - ошибка (см. parser.cpp GetNextToken).
    bool m_in_attr = false;

    /// Глубина раскрытия макросов (защита от бесконечной рекурсии)
    size_t m_macro_depth = 0;

    /// Блок интерфейса файла (`\path { … };`): раскрытие макросов модуля запрещено — скопы макросов
    /// наследуются от импортёра, и публичный API файла зависел бы от того, кто его импортирует.
    /// Допустимы макросы базового скоупа (он неизменяем из файлов, `Macro::isDslScopeGroup`) и
    /// предопределённые. Флаг ставит загрузчик (начало блока), снимает грамматика по завершении
    /// первого `block`; диагностика запрещённого макроса отложена до успешного разбора блока.
    bool m_inFileInterface = false;
    /// Смещение `{` интерфейса в шкале диапазонов термов (1-based, как `Location::offset()`).
    size_t m_fileInterfaceBegin = 0;

    /// true - макровызов в интерфейсе запрещён: раскрывать нельзя. Диагностика ОТЛОЖЕНА до
    /// успешного разбора блока (см. `m_pendingInterfaceMacro`).
    bool forbidFileInterfaceMacro(const TermPtr& callTerm);

    /// Первый запрещённый макрос интерфейса: диагностика выдаётся только после успешного разбора
    /// блока (незакрытый блок — интерфейса нет, ложного запрета для макросов тела не будет).
    struct PendingInterfaceMacro {
        MapperRange range;
        std::string name;
    };
    std::optional<PendingInterfaceMacro> m_pendingInterfaceMacro;
    /// Выдать отложенную диагностику запрещённого макроса (один раз на файл).
    void reportPendingInterfaceMacro();

    /// Снимает режим интерфейса, если разобранный блок начался с его `{` (вызов из грамматики).
    /// `blockBegin` - 1-based смещение `{` из диапазона терма. Блок РАЗОБРАН — выдаём отложенную
    /// диагностику запрещённого макроса (для незакрытого блока её не будет: интерфейса нет).
    void leaveFileInterfaceIfAt(size_t blockBegin) {
        if (m_inFileInterface && blockBegin == m_fileInterfaceBegin) {
            m_inFileInterface = false;
            reportPendingInterfaceMacro();
        }
    }

    /// Суммарное число раскрытий макросов в текущем операторе (защита от самовоспроизведения
    /// через границы чтений). Сбрасывается на границе оператора (буфер анализа пуст) в GetNextToken.
    size_t m_macro_expansion_total = 0;

    /// Один раз на оператор уже выдан отчёт о рекурсивном макросе (guard в macro.cpp ExpandTermMacro),
    /// чтобы не спамить ошибкой на каждом раскрытии. Сбрасывается на границе оператора.
    bool m_macro_recursion_reported = false;

    parser::token_type GetNextToken(TermPtr* yylval);
    //        TermPtr MacroEval(const TermPtr &term);

    /// Кадр ожидаемого закрывающего try-блока (из @__PRAGMA_TRY_CLOSE__): endToken - в какой
    /// токен превращается финальная '}' (TRY_ALL_END/TRY_MINUS_END/TRY_PLUS_END), depth - сколько
    /// вложенных '{'/'{+'/'{*'/'{-' открыто внутри тела (не считая сам try-begin этого кадра).
    struct PendingTryClose {
        parser::token_type endToken = parser::token_type::END;
        int depth = 0;
    };
    /// Стек ожидаемых try-закрытий (LIFO - для вложенных try через макросы). Пуст - нет ожидания.
    /// Финальная '}' (depth==0) верхнего кадра конвертируется в его endToken.
    std::vector<PendingTryClose> m_pendingTryClose;

    /// Парсит текстовый фрагмент (не файл/модуль) под указанным «фиктивным» именем
    /// источника. Имена фиктивных источников помечаются префиксом '@' (in-memory,
    /// файла на диске нет). По умолчанию "@input"; для DSL-макросов стандартной библиотеки -
    /// "@stdlib/dsl".
    TermPtr ParseText(std::string_view text, std::string_view sourceName = "@input", bool expand_module = false);

    /// Парсит из уже зарегистрированного source-файла (не создавая in-memory
    /// псевдо-источник с префиксом '@'). Используется модульным загрузчиком для
    /// главного файла/модуля, чтобы маппинги (mapStart/mapStop) привязывались к
    /// реальному файлу.
    /// `fileInterface` - разбор начинается с блока интерфейса файла: внутри блока раскрытие
    /// макросов запрещено (`m_inFileInterface`), режим снимает грамматика по завершении блока.
    TermPtr ParseWithSource(trust::MapperFile src, bool expand_module = false, std::size_t skipPrefix = 0, bool fileInterface = false);

    // Собирает термин из последовательности лексем и удаляет их из входного буфера.
    // ctx задаёт контекст, из которого наследуется Macro; macro_expand=false
    // отключает раскрытие макросов (специальные случаи: тесты, идентификаторы определений).
    static size_t ParseTerm(TermPtr& term, const SequenceType& buffer, trust::ParserServices ctx, const size_t skip = 0, bool pragma_enable = true,
                            bool macro_expand = true);
    static TermPtr ParseTerm(const char* proto, trust::ParserServices ctx, bool pragma_enable = true, bool macro_expand = true);

    template <class Ctx>
        requires(!std::is_same_v<std::remove_cvref_t<Ctx>, trust::ParserServices>)
    static size_t ParseTerm(TermPtr& term, const SequenceType& buffer, Ctx& ctx, const size_t skip = 0, bool pragma_enable = true,
                            bool macro_expand = true) {
        return ParseTerm(term, buffer, trust::makeParserServices(ctx), skip, pragma_enable, macro_expand);
    }
    template <class Ctx>
        requires(!std::is_same_v<std::remove_cvref_t<Ctx>, trust::ParserServices>)
    static TermPtr ParseTerm(const char* proto, Ctx& ctx, bool pragma_enable = true, bool macro_expand = true) {
        return ParseTerm(proto, trust::makeParserServices(ctx), pragma_enable, macro_expand);
    }

    TermPtr CheckModuleTerm(const TermPtr& term);

    static size_t SkipBrackets(const SequenceType& buffer, size_t offset);

    /// Активный пул строк прогона на время существования парсера: доки макросов/тексты разбора
    /// интернируются у владельца прогона (E9), даже если Parser создан вне `Pipeline::run`.
    trust::StringInternerScope m_stringsScope;
    trust::ParserServices m_ctx;

    syntax::PredefMacroResolver m_predef;
    syntax::PragmaEvaluator m_pragma;

    Macro* m_macro = nullptr;
    bool m_is_lexer_complete;

  private:
    TermPtr m_ast;
    PostLexerType* m_postlex;

    /// Состояние восстановления после синтаксических ошибок (сбрасывается на каждый разбор).
    /// Диапазон одновременно служит ключом дедупликации подряд идущих одинаковых ошибок.
    std::optional<MapperRange> m_lastSyntaxErrorRange;
};

} // namespace trust

#endif // TRUST_SYNTAX_PARSER_H_
