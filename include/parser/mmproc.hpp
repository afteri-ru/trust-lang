#pragma once

#include "diag/context.hpp"
#include "ast/token_info.hpp"
#include "ast/token.hpp"
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace trust {

// Forward enum declaration
enum class BinOp : int;

/// Тип тела макроса
enum class MacroBodyType : uint8_t {
    kExpression,    ///< тело — выражение до ';'
    kTokenSequence, ///< тело в @@ ... @@
    kStringLiteral, ///< тело в @@@ ... @@@
};

/// Определение макроса
struct MacroDef {
    std::string m_name;                ///< нормализованное имя макроса (без @@)
    std::vector<std::string> m_params; ///< имена образцов ($name)
    MacroBodyType m_bodyType{MacroBodyType::kExpression};
    TokenSequence m_body;    ///< тело как последовательность токенов
    MapperRange m_bodyRange; ///< диапазон тела макроса в исходном коде
};

/// Динамический массив таблиц макросов.
/// Каждый элемент соответствует одному модулю.
/// Поиск — с конца к началу.
using MacroTable = std::vector<std::unordered_map<std::string, MacroDef>>;

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
     *  @return ссылка на таблицу текущего модуля (непустая)
     */
    [[nodiscard]] static const std::unordered_map<std::string, MacroDef>& currentMacros(const MacroTable& macros) noexcept;

    /** Экранировать спецсимволы в строке для вывода C++ */
    [[nodiscard]] static std::string escape(const std::string& s);

    /** Преобразовать escape-последовательности в реальные символы */
    [[nodiscard]] static std::string unescape(const std::string& s);

  private:
    /** Внутренняя обработка (рекурсивное раскрытие). */
    static TokenSequence processInternal(Context& ctx, MacroTable& macros, int& recursionDepth, const LexemeSequence& lexemes, std::size_t& pos);

    /** Попытаться считать определение макроса, начиная с pos.
     *  Если определение найдено — регистрирует его и сдвигает pos.
     *  Возвращает true, если определение считано. */
    static bool collectMacroDef(Context& ctx, MacroTable& macros, const LexemeSequence& lexemes, std::size_t& pos);

    /** Раскрыть вызов макроса с подстановкой аргументов. */
    static TokenSequence expandMacro(Context& ctx, MacroTable& macros, int& recursionDepth, const std::string& name, const LexemeSequence& lexemes,
                                     std::size_t& pos);

    /** Заменить @$param, @$N, @$*, @$# в теле макроса на аргументы. */
    static TokenSequence substituteArgs(const TokenSequence& body, const std::vector<TokenSequence>& args, const std::vector<std::string>& paramNames);

    /** Заменить @$param, @$N, @$*, @$# в одиночном токене на аргументы (строковая подстановка). */
    static std::string substituteTokenText(const std::string& text, const std::vector<TokenSequence>& args, const std::vector<std::string>& paramNames);

    /** Создать StringLiteral из MACRO_STR (@@@...@@@) или STRWIDE/STRCHAR. */
    static TokenPtr makeStringLiteral(const Lexeme& lex);

    /** Разделить последовательность токенов по запятым на верхнем уровне с учётом вложенных скобок. */
    static std::vector<TokenSequence> splitArgsByComma(const TokenSequence& tokens);
};

} // namespace trust