#pragma once

#include "ast/ast_nodes.hpp"
#include "diag/location.hpp"
#include <string>
#include <utility>
#include <vector>

namespace trust {

class Context;

/// CppTranspiler — генератор C++ кода из AST в выходной файл.
/// Использует Context для доступа к SymbolTable/TypeRegistry и для маппинга позиций.
/// Вся генерация только через generateToFile() — строковая генерация не используется.
class CppTranspiler {
  public:
    explicit CppTranspiler(Context& ctx);

    /// Генерация C++ кода непосредственно в выходной файл с построением source map.
    /// Для каждого узла AST создаётся маппинг trust-range → cpp-range через mapStart/mapStop.
    /// @param ast_nodes Выход парсера (вектор AstNodePtr).
    /// @param output_idx Индекс выходного C++ файла (должен быть создан через ctx.add_output()).
    void generateToFile(const std::vector<AstNodePtr>& ast_nodes, MapperFile output_idx);

    /// Экспортированный символ: original trust-имя и сгенерированное C++ имя.
    struct ExportEntry {
        std::string trustName; ///< Имя в языке Trust
        std::string cppName;   ///< Имя в сгенерированном C++ коде
    };

    /// Список всех экспортированных символов (собран в процессе generateToFile).
    const std::vector<ExportEntry>& exports() const noexcept { return m_exports; }

  private:
    /// Генерация для одного узла (в файл).
    void generateNodeToFile(const AstNodeBase& node, MapperFile output_idx);

    /// Выводит перевод строки между последовательными блоками при генерации C++ кода,
    /// если их строки в исходнике различаются. Если строка конца prev совпадает со строкой
    /// начала node (блоки на одной строке исходника) — перевод строки не выводится, вместо
    /// этого для читаемости ставится пробел (emitSameLineSpace).
    /// prev == nullptr означает первый блок (перевод строки не нужен).
    void emitBlockSeparator(const AstNodeBase* prev, const AstNodeBase& node, MapperFile output_idx);

    /// Для блоков на одной строке исходника: вставляет пробел между ними, если на границе
    /// ещё нет пробельного символа (не дублирует пробелы из EMBED-содержимого и т.п.).
    /// nextText — первый фрагмент следующего блока (его text() или "{" / "}").
    void emitSameLineSpace(std::string_view nextText, MapperFile output_idx);

    /// Генерация для объявления переменной (VarDecl).
    void generateVarDeclToFile(const VarDecl& var_node, MapperFile output_idx);

    /// Генерация для объявления типа (BinaryOp ::=).
    void generateTypeDeclToFile(const Binary& binary_node, MapperFile output_idx);

    /// Генерация для объявления функции (FuncDecl).
    void generateFuncDeclToFile(const FuncDecl& func_node, MapperFile output_idx);

    /// Генерация условного оператора (IfStmt): if/else-if/else с маппингом диапазона.
    void generateIfToFile(const IfStmt& node, MapperFile output_idx);

    /// Генерация цикла while (WhileStmt), включая опциональный else.
    void generateWhileToFile(const WhileStmt& node, MapperFile output_idx);

    /// Генерация цикла do-while (DoWhileStmt).
    void generateDoWhileToFile(const DoWhileStmt& node, MapperFile output_idx);

    /// Генерация оператора match (MatchStmt): временная переменная + if/else-if/else.
    void generateMatchToFile(const MatchStmt& node, MapperFile output_idx);

    /// Генерация тела блока { ... } с зеркалированием строк '{' и '}' по исходнику.
    /// body — операторы тела, blockRange — диапазон блока (скобок) из исходника
    /// (невалидный, если блок/скобки недоступны — тогда между { и } перевод строки).
    /// mapBlock=false — тело не оборачивается собственным mapStart/mapStop (используется для
    /// do-while, где range statement'а и тела начинаются с '{' и их begin совпадают, что
    /// приводило бы к коллизии ключа в mapStop).
    /// beforeCloseLabel — если не пуст, перед '}' вставляется метка '<beforeCloseLabel>:;'
    /// (используется для continue-метки do-while).
    void emitBlockBodyToFile(const std::vector<AstNodePtr>& body, MapperRange blockRange, MapperFile output_idx, bool mapBlock = true,
                             const std::string& beforeCloseLabel = "", bool inFunction = false, const std::string& functionName = "",
                             const std::string& afterOpen = "");

    /// Генерация тела { ... } для одного узла-тела (ScopeBlock/Sequence или одиночный statement):
    /// собирает операторы и диапазон блока, затем вызывает emitBlockBodyToFile.
    /// beforeCloseLabel — continue-метка, вставляемая перед '}' (для do-while).
    /// afterOpen — текст, вставляемый сразу после '{' (например, установка флага while-else).
    void emitBodyNode(const AstNodePtr& body, MapperFile output_idx, bool mapBlock = true, const std::string& beforeCloseLabel = "",
                      const std::string& afterOpen = "");

    /// Генерация выражения (правая часть :=).
    std::string generateExpr(const AstNodeBase* node);

    /// Генерация statement-level выражения (присваивание, составные операторы, ++, --).
    void generateExprStmtToFile(const Binary& binary_node, MapperFile output_idx);

    /// Генерация break/continue как goto по метке начала/выхода соответствующего цикла.
    void generateBreakContinueToFile(const JumpStmt& node, MapperFile output_idx);

    /// Получить C++ имя типа из Trust IdentType.
    std::string resolveTypeName(const AstNodeBase* type_node) const;

    Context& m_ctx;

    /// Истина, если генерируем внутри тела функции (C++-метки именованных блоков допустимы только в функциях).
    bool m_inFunction = false;

    /// Текущий уровень отступа (для форматирования тел блоков/функций/циклов/match).
    int m_indent = 0;

    /// Префикс отступа для текущего уровня (4 пробела на уровень).
    [[nodiscard]] std::string indentPrefix() const { return std::string(static_cast<size_t>(m_indent) * 4, ' '); }

    /// Имя текущей функции (очищенное от '%'): функция — top-level именованный блок.
    /// Именованный break на это имя трактуется как return (void).
    std::string m_currentFuncName;

    /// continue-метка именованного блока, которую потребляет первый цикл в его теле
    /// (ставится перед циклом — после инициализации блока). Очищается при генерации цикла.
    std::string m_pendingContinueLabel;

    /// Счётчик временных переменных для операторов match.
    uint32_t m_matchCounter = 0;

    /// Счётчик флагов для эмуляции while-else (в C++ нет 'while...else').
    uint32_t m_whileElseCounter = 0;

    /// Экспортированные символы (пополняется в generateVarDeclToFile).
    std::vector<ExportEntry> m_exports;
};

} // namespace trust