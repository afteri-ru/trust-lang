#pragma once

#include "diag/context.hpp"
#include "ast/token_info.hpp"
#include "ast/token.hpp"
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string_view>
#include <vector>

namespace trust {

// Forward enum declaration
enum class BinOp : int;

// Accessor for embedded default DSL source
std::string_view getDefaultDslSrc() noexcept;

/// Тип тела макроса
enum class MacroBodyType : uint8_t {
    kExpression,    ///< тело — выражение до ';'
    kTokenSequence, ///< тело в @@ ... @@
    kStringLiteral, ///< тело в @@@ ... @@@
};

/// Токен, ожидаемый после раскрытия макроса (для @__PRAGMA_EXPECTED__)
struct ExpectedToken {
    bool isKind{true};                              ///< true — проверка по kind, false — проверка по text
    ParserToken::Kind kind{ParserToken::Kind::END}; ///< kind для проверки (если isKind)
    std::string text;                               ///< текст для проверки (если !isKind)
};

/// Одна группа скобок в определении макроса.
/// Например, в `@@ macro (a, ...)<b>[c] @@ := body ;` три группы:
/// (a, ...), <b>, [c]
struct MacroArgGroup {
    bool m_hasVariadic{false};              ///< true — группа содержит `...`
    bool m_isTemplate{false};               ///< true — template-style ($name вне скобок)
    std::vector<std::string_view> m_params; ///< имена $param в этой группе (string_view в Context)
};

/// Определение макроса
struct MacroDef {
    std::vector<Lexeme> m_nameLexemes;      ///< лексемы имени макроса (первая — ключ)
    std::vector<MacroArgGroup> m_argGroups; ///< группы аргументов (0, 1 или более)
    int m_variadicCount{0};                 ///< количество групп, содержащих `...`
    MacroBodyType m_bodyType{MacroBodyType::kExpression};
    LexemeSequence m_body;                      ///< тело как последовательность лексем (буфер живёт в Context)
    MapperRange m_bodyRange;                    ///< диапазон тела макроса в исходном коде
    std::vector<ExpectedToken> m_expectedAfter; ///< ожидаемые токены после раскрытия (@__PRAGMA_EXPECTED__)
};

/// Динамический массив таблиц макросов.
/// Каждый элемент соответствует одному модулю.
/// Поиск — с конца к началу.
/// Ключ — первая лексема имени (string_view в Context), значение — вектор макросов с разными полными именами.
using MacroTable = std::vector<std::map<std::string_view, std::vector<MacroDef>, std::less<>>>;

/** MMProc (Macros & Module Processor) — этап обработки между Lexer и Parser.
 *
 *  Реализация:
 *  - Конвертирует LexemeSequence → TokenSequence
 *  - Конкатенирует последовательные строковые литералы одного типа
 *  - Применяет unescape к обычным строковым литералам (кроме RAW)
 *  - Собирает определения макросов и регистрирует их
 *  - Раскрывает вызовы макросов с подстановкой аргументов
 *  - Формирует ошибки через diag() для неподдерживаемых токенов (MODULE)
 */
struct MMProcessor {
    static constexpr int kMaxRecursionDepth = 100;

    /** Обработать последовательность лексем.
     *  @param ctx контекст компиляции
     *  @param lexemes последовательность лексем от Flex
     *  @param macros опциональная таблица макросов. Если nullptr — создаётся внутри метода.
     *         Каждый вызов добавляет новый элемент в массив; после завершения он удаляется.
     *  @return TokenSequence для передачи в Bison-парсер
     */
    static TokenSequence process(Context& ctx, const LexemeSequence& lexemes, std::shared_ptr<MacroTable> macros = nullptr);

    /** Доступ к таблице макросов текущего модуля (последний элемент массива).
     *  @param macros таблица макросов
     *  @return ссылка на таблицу текущего модуля (непустая) */
    [[nodiscard]] static const std::vector<MacroDef>& currentMacros(const MacroTable& macros) noexcept;

    /** Скомпилировать макросы из текстового источника (dsl) в существующую таблицу.
     *  @param ctx контекст компиляции
     *  @param macros таблица макросов (будет пополнена)
     *  @param source исходный код с определениями макросов */
    static void compileFromSource(Context& ctx, MacroTable& macros, std::string_view source);

    /** Экранировать спецсимволы в строке для вывода C++ */
    [[nodiscard]] static std::string escape(const std::string& s);

    /** Преобразовать escape-последовательности в реальные символы */
    [[nodiscard]] static std::string unescape(const std::string& s);

    /** Глобальный счётчик для @__COUNTER__ */
    static int s_counter;

    /** Обработать предопределённый макрос вида @__NAME__.
     *  @return TokenSequence с одним токеном, или пустой если макрос не распознан
     *  (в этом случае уже выдана ошибка). */
    static TokenSequence handlePredefinedMacro(Context& ctx, std::string_view macroName, MapperFile fileIdx, MapperLocation loc);

  private:
    /** Внутренняя обработка (рекурсивное раскрытие).
     *  @param expandedMacros если не nullptr — в него записываются имена раскрытых макросов */
    static TokenSequence processInternal(Context& ctx, MacroTable& macros, int& recursionDepth, const LexemeSequence& lexemes, std::size_t& pos,
                                         std::set<std::string>* expandedMacros = nullptr);

    /** Конкатенация последовательных строковых/embed-лексем одного типа.
     *  Начиная с pos, собирает все соседние лексемы типа kind, конкатенирует их текст,
     *  применяет unescape (если не raw) и создаёт один TokenInfo.
     *  Сдвигает pos за последнюю сконкатенированную лексему. */
    static TokenPtr concatStringTokens(const LexemeSequence& lexemes, std::size_t& pos);

    /** Собрать идентификатор из последовательности NAME/LOCAL/NATIVE/NAMESPACE.
     *  Начиная с pos, пытается собрать полный идентификатор (с ::, $local, %native).
     *  Сдвигает pos за последнюю лексему идентификатора.
     *  Если идентификатор не собран — сдвигает pos на один токен и возвращает NAMESPACE токен.
     *  @return TokenInfo с Kind::Ident или Kind::NAMESPACE (если только :: без имени) */
    static TokenPtr buildIdentToken(const LexemeSequence& lexemes, std::size_t& pos);

    /** Попытаться считать определение макроса, начиная с pos.
     *  Если определение найдено — регистрирует его и сдвигает pos.
     *  Возвращает true, если определение считано. */
    static bool collectMacroDef(Context& ctx, MacroTable& macros, const LexemeSequence& lexemes, std::size_t& pos);

    /** Раскрыть вызов макроса с подстановкой аргументов (работает с LexemeSequence).
     *  @param name первая лексема имени макроса (ключ поиска)
     *  @param expandedMacros если не nullptr — в него записываются имена раскрытых макросов */
    static TokenSequence expandMacroLexeme(Context& ctx, MacroTable& macros, int& recursionDepth, std::string_view name, const LexemeSequence& lexemes,
                                           std::size_t& pos, std::set<std::string>* expandedMacros = nullptr);

    /** Заменить @$param, @$N, @$*, @$# в теле макроса на аргументы.
     *  callArgsByGroup[groupIdx] = vector of comma-separated LexemeSequences for that group. */
    static LexemeSequence substituteArgs(const LexemeSequence& body, const std::vector<std::vector<LexemeSequence>>& callArgsByGroup,
                                         const std::vector<MacroArgGroup>& argGroups);

    /** Заменить @$param, @$N, @$*, @$# в одиночном токене на аргументы (строковая подстановка). */
    static std::string substituteTokenText(std::string_view text, const std::vector<std::vector<LexemeSequence>>& callArgsByGroup,
                                           const std::vector<MacroArgGroup>& argGroups);

    /** Создать StringLiteral из MACRO_STR (@@@...@@@) или STRWIDE/STRCHAR. */
    static TokenPtr makeStringLiteral(const Lexeme& lex);

    /** Разделить последовательность лексем по запятым на верхнем уровне с учётом вложенных скобок. */
    static std::vector<LexemeSequence> splitArgsByComma(const LexemeSequence& tokens);

    /** Обработать атрибут @[...]@ с раскрытием макросов внутри. */
    static TokenSequence processAttrGroup(Context& ctx, MacroTable& macros, int& recursionDepth, const Lexeme& startLex, const LexemeSequence& lexemes,
                                          std::size_t& pos, std::set<std::string>* expandedMacros);
};

} // namespace trust