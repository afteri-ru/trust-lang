#ifndef TRUST_SYNTAX_PARSER_H_
#define TRUST_SYNTAX_PARSER_H_

#include <memory>
#include <vector>

namespace trust {
class AstNodeBase;
using AstNodePtr = std::shared_ptr<AstNodeBase>;
class Context;
} // namespace trust

#include "syntax/types.h"

#include "syntax/warning_push.h"
#include "parser.yy.h"
#include "syntax/warning_pop.h"

namespace trust {
TermPtr ProcessMacro(Parser& parser, TermPtr& term);

enum class ExpandMacroResult : uint8_t {
    Continue,
    Break,
    Goto,
};
ExpandMacroResult ExpandTermMacro(Parser& parser);

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

    parser::token_type ExpandPredefMacro(TermPtr& term);

    static int m_counter;
    // Единый реестр предопределённых макросов (@__...__ и др.) - static, чтобы
    // LSP-автодополнение могло перечислять его без инстанса парсера.
    static std::map<std::string, std::string> m_predef_macro;
    static bool RegisterPredefMacro(const char* name, const char* desc);
    static void InitPredefMacro();
    /// Имена предопределённых макросов (ключи m_predef_macro) - для автодополнения.
    static std::vector<std::string> PredefMacroNames();
    bool CheckPredefMacro(const TermPtr& term);

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

    TermPtr GetAst();

    SequenceType m_macro_analisys_buff; ///< Последовательность лексем для анализа на наличие макросов

    // TODO(cleanup): unused - commented out, see task 1785696533477
    // TermPtr m_expected;
    // TermPtr m_unexpected;
    // TODO(cleanup): unused - commented out, see task 1785675437901
    // TermPtr m_finalize;
    // int m_finalize_counter;
    // TODO(cleanup): unused - only for m_annotation pragmas, commented out, see task 1785678668891
    // TermPtr m_annotation;
    bool m_no_macro;
    bool m_enable_pragma;
    bool m_expand_module = false; ///< Разрешает загрузку модулей при парсинге

    /// Глубина раскрытия макросов (защита от бесконечной рекурсии)
    size_t m_macro_depth = 0;

    /// Гигиенические имена: ident -> сгенерированное имя в пределах одного раскрытия макроса
    std::map<std::string, std::string> m_hygienic_names;
    /// Глобальный счётчик гигиенических имён (начинается с 1)
    int m_hygienic_counter = 0;

    parser::token_type GetNextToken(TermPtr* yylval);
    //        TermPtr MacroEval(const TermPtr &term);

    // Проверяет термин на наличие команды препроцессора (прагмы)
    bool PragmaCheck(const TermPtr& term);
    // Выполняет команду препроцессора (прагму)
    bool PragmaEval(const TermPtr& term, SequenceType& buffer, SequenceType& seq);

    /// Специальная обработка @__OPTION_TRUE__/@__OPTION_FALSE__ прямо в GetNextToken:
    /// содержимое после имени флага берётся «сырым» (токены до закрывающей скобки), без
    /// разбивки по запятым и без ParseTerm-пре-парсинга. При срабатывании флага токены
    /// вставляются как есть (преdef-макросы раскрываются на сайте вызова, локация не
    /// «запекается»). Возвращает true, если прагма распознана и обработана.
    bool EvalOptionTrueFalseRaw();

    /// Парсит текстовый фрагмент (не файл/модуль) под указанным «фиктивным» именем
    /// источника. Имена фиктивных источников помечаются префиксом '@' (in-memory,
    /// файла на диске нет). По умолчанию "@input"; для встроенного DSL - "@dsl".
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

    // TODO(cleanup): unused - commented out, see task 1785675437901
    // inline static bool IsBracket(const std::string_view str) { return str.size() > 0 && (str[0] == '(' || str[0] == '[' || str[0] == '<'); }

    static std::string GetCurrentDate(time_t ts = std::time(NULL));
    static std::string GetCurrentTime(time_t ts = std::time(NULL));
    static std::string GetCurrentTimeStamp(time_t ts = std::time(NULL));
    static std::string GetCurrentTimeStampISO(time_t ts = std::time(NULL));

    TermPtr CheckModuleTerm(const TermPtr& term);

    static size_t SkipBrackets(const SequenceType& buffer, size_t offset);

    time_t m_timestamp;

    trust::Context& m_ctx;

    MacroPtr m_macro;
    bool m_is_lexer_complete;

  private:
    TermPtr m_ast;
    // TODO(cleanup): unused - commented out, see task 1785675437901
    // bool m_is_runing;
    PostLexerType* m_postlex;
};

} // namespace trust

#endif // TRUST_SYNTAX_PARSER_H_
