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

    /// Генерация выражения (правая часть :=).
    std::string generateExpr(const AstNodeBase* node);

    /// Генерация statement-level выражения (присваивание, составные операторы, ++, --).
    void generateExprStmtToFile(const Binary& binary_node, MapperFile output_idx);

    /// Получить C++ имя типа из Trust IdentType.
    std::string resolveTypeName(const AstNodeBase* type_node) const;

    Context& m_ctx;

    /// Экспортированные символы (пополняется в generateVarDeclToFile).
    std::vector<ExportEntry> m_exports;
};

} // namespace trust