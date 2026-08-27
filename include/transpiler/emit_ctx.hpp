#pragma once

// include/transpiler/emit_ctx.hpp
// Общий контекст кодогенерации (CppEmitContext): всё mutable-состояние транспилятора и
// низкоуровневые операции вывода/маппинга/отступа. Вынесено из монолитного CppTranspiler
// в отдельный, независимо тестируемый контейнер. Эмиттеры (TypeEmitter/DeclEmitter/...) и
// драйвер CppTranspiler разделяют этот контекст. CppEmitContext НЕ зависит от KindVisitor.

#include "ast/ast_nodes.hpp"
#include "location/location.hpp"
#include "types/runtime_symbols.hpp"
#include "types/type_id.hpp"

#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace trust {

class Context;
class SymbolTable;
class CppEmitContext;

/// Экспортированный символ: original trust-имя, сгенерированное C++ имя и trust-source
/// предварительного объявления (например `x:Int32 := ...;`) - семантическая конструкция языка,
/// пригодная для парсинга (используется в поле `__trust_export_decls` при сборке .trust).
struct ExportEntry {
    std::string trustName; ///< Имя в языке Trust
    std::string cppName;   ///< Имя в сгенерированном C++ коде
    std::string fwdDecl{}; ///< Предварительное объявление в Trust-синтаксисе (:= ...;)
};

/// Контекст вложенности генерации: уровень отступа. Единый стек вместо вложенных

/// CppTranspiler-объектов и ручного проброса indent аргументами. Индентация
/// определяется по вершине стека (см. indentLevel()).
struct ScopeContext {
    int indent = 0; ///< уровень отступа (4 пробела на уровень)
};

/// RAII-обёртка временной подстановки «имя функции → C++-значение возврата» при эмиссии
/// пост-условий перед `return`: устанавливает m_resultName/m_resultCpp, восстанавливает
/// предыдущие значения в деструкторе (безопасно при раннем выходе).
class ResultGuard {
  public:
    ResultGuard(CppEmitContext& ectx, std::string name, std::string cpp);
    ~ResultGuard();
    ResultGuard(const ResultGuard&) = delete;
    ResultGuard& operator=(const ResultGuard&) = delete;

  private:
    CppEmitContext& m_ectx;
    std::string m_savedName;
    std::string m_savedCpp;
};

/// Все mutable-члены CppTranspiler, вынесенные в отдельный контейнер. Владеет Context&,
/// текущим выходным файлом, стеком отступов/областей имён, счётчиками и наборами собранных
/// инклудов/экспортов/нативных импортов. Множества, помеченные mutable, записываются и из
/// const-методов эмиттеров (например resolveCppTypeId → recordUsedType).
class CppEmitContext {
  public:
    explicit CppEmitContext(Context& ctx, const SymbolTable* resolvedTypes = nullptr);

    /// Текущий уровень отступа из вершины стека (0 = top-level).
    [[nodiscard]] int indentLevel() const noexcept { return m_scopeStack.empty() ? 0 : m_scopeStack.back().indent; }

    /// Префикс отступа для текущего уровня (4 пробела на уровень).
    [[nodiscard]] std::string indentPrefix() const { return std::string(static_cast<size_t>(indentLevel()) * 4, ' '); }

    /// Полное квалифицированное C++-имя (с учётом стека областей имён). Для верхнего уровня
    /// модуля / глобальной области `::` - само `name`.
    [[nodiscard]] std::string qualifiedCppName(std::string_view name) const;

    /// Имя C++ namespace из text() области имён: убирает ведущий и завершающий "::"
    /// (например "ns::" → "ns", "::ns::name" → "ns::name").
    [[nodiscard]] static std::string namespaceCppName(std::string_view text);

    Context& m_ctx;

    /// Текущий выходной C++ файл (устанавливается в generateNodeToFile/generateToFile).
    MapperFile m_out;

    std::vector<ScopeContext> m_scopeStack;

    /// Глубина вложенности выражения. 0 = statement-root (ребёнок SemicolonStmt: текст без
    /// скобок; ';' добавляет SemicolonStmt); >0 = вложенное выражение (только текст, без ';',
    /// бинарные - в скобках). Инкрементируется в emitExpr перед dispatchKind.
    int m_exprDepth = 0;

    /// True, если текущая генерация идёт ВНУТРИ C++ compound statement (тело функции или
    /// тело управляющей конструкции if/while/do-while/match).
    bool m_inCppBlock = false;

    /// Стек имён вложенных областей имён (для квалификации экспорта).
    std::vector<std::string> m_namespaceStack;

    /// Глубина вложенности скрытых (анонимных) областей имён `_`.
    int m_hiddenNamespaceDepth = 0;

    /// Счётчики временных переменных/флагов (match / while-else / деструктуризация).
    uint32_t m_matchCounter = 0;
    uint32_t m_whileElseCounter = 0;
    uint32_t m_destructureCounter = 0;

    /// В режиме forward-decl-only кодогенерация объявлений (var/func) подавляет определение.
    bool m_forwardDeclOnly = false;

    /// Временное связывание имени функции → C++-значение возврата (temp) при эмиссии пост-условий.
    std::string m_resultName;
    std::string m_resultCpp;
    int m_resultCounter = 0;

    /// Экспортированные символы (собранные в generate*ToFile).
    std::vector<ExportEntry> m_exports;

    /// Заголовки рантайма (bare-имена, маркер '@'), реально использованные кодом.
    mutable std::set<std::string> m_runtimeHeaders;

    /// Собранные за время эмиссии полные директивы #include (дедуп). Препендятся в конце.
    mutable std::set<std::string> m_requiredIncludes;

    /// Канонические TypeId типов, реально использованных при эмиссии (инклуды ПОСЛЕ обхода).
    mutable std::set<TypeId> m_usedTypes;

    /// Флаги линковки нативных библиотек (`-l<имя>`) из `@[link("имя")]`.
    std::set<std::string> m_linkLibs;

    /// Импорты нативных функций: trust-имя → нативное C++-имя.
    std::unordered_map<std::string, std::string> m_nativeImports;

    /// Разрешённая семантикой таблица символов (необязательно).
    const SymbolTable* m_resolvedTypes = nullptr;
};

} // namespace trust
