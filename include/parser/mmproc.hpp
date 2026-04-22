#pragma once

#include "diag/context.hpp"
#include "gencpp/ast.hpp"
#include "parser/token.hpp"
#include <string>

namespace trust {

// Forward enum declaration
enum class BinOp : int;

/** MMProc (Macros & Module Processor) — этап обработки между Lexer и Parser.
 *
 *  Текущая реализация:
 *  - Конвертирует LexemeSequence → AstNodeSequence, заполняя TokenInfo
 *  - Конкатенирует последовательные строковые литералы одного типа
 *  - Применяет unescape к обычным строковым литералам (кроме RAW)
 *  - Формирует ошибки через diag() для неподдерживаемых токенов (MACRO, MODULE)
 */
struct MMProcessor {
    /** Обработать последовательность лексем.
     *  @param ctx контекст диагностики
     *  @param lexemes последовательность лексем от Flex
     *  @return AstNodeSequence для передачи в Bison-парсер
     */
    static AstNodeSequence process(Context &ctx, const LexemeSequence &lexemes);

    /** Экранировать спецсимволы в строке для вывода C++ */
    [[nodiscard]] static std::string escape(const std::string &s);

    /** Преобразовать escape-последовательности в реальные символы */
    [[nodiscard]] static std::string unescape(const std::string &s);

    /** Преобразовать BinOp в строковое представление */
    [[nodiscard]] static std::string bin_op_to_string(BinOp op);

    /** Преобразовать строковое представление в BinOp */
    [[nodiscard]] static BinOp bin_op_from_string(const std::string &s);
};

} // namespace trust