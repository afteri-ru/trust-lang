#pragma once

// include/transpiler/emit/emit_ctx.hpp
// Общий контекст кодогенерации (CppEmitContext): всё mutable-состояние транспилятора и
// низкоуровневые операции вывода/маппинга/отступа. Вынесено из монолитного CppTranspiler
// в отдельный, независимо тестируемый контейнер. Эмиттеры (TypeEmitter/DeclEmitter/...) и
// драйвер CppTranspiler разделяют этот контекст. CppEmitContext НЕ зависит от диспетчера.

#include "ast/ast_nodes.hpp"
#include "analysis/modes.hpp"
#include "attrs/attr_pool.hpp"
#include "location/location.hpp"
#include "location/module_index.hpp"
#include "transpiler/emit/emit_common.hpp"
#include "types/runtime_symbols.hpp"
#include "types/type_id.hpp"

#include <cstddef>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace trust {

class Context;
class CppEmitContext;

/// Вид защиты стека вызова функции (из атрибута @[stack_check@]),
/// собираемый при эмиссии объявления/определения функции (см. DeclEmitter).
/// size == 0 → check_stack_limit() (free >= m_stack_limit + reserve); size > 0 → check_overflow(size).
struct StackCheckGuard {
    long size = 0; ///< @[stack_check(N)@] → check_overflow(N); 0 = limit (check_stack_limit)
};

/// Защита стека функции из атрибута @[stack_check]: size==0 → check_stack_limit(),
/// size>0 → check_overflow(N). Семантика уже проверила неотрицательность/целочисленность.
[[nodiscard]] std::optional<StackCheckGuard> stackCheckGuardOf(const FuncDecl& func, const AttrPool& attrs);

/// Индекс «текст trust-имени → объявление» для источников без узла-ссылки (атрибуты, CLI):
/// второй кандидат с тем же текстом делает запись неоднозначной (диагностика в точке использования).
struct TextDeclarationIndex {
    const AstNodeBase* decl = nullptr;
    bool ambiguous = false;

    void add(const AstNodeBase* identity) noexcept {
        if (decl == nullptr) {
            decl = identity;
            return;
        }
        if (decl != identity) {
            ambiguous = true;
        }
    }
};

/// Результат текстового поиска в таблицах кодогена (см. TextDeclarationIndex).
struct TextLookup {
    enum class Status {
        Found,
        NotFound,
        Ambiguous,
    };
    Status status = Status::NotFound;
    const std::string* value = nullptr; ///< валиден только при status == Found
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

/// RAII-обёртка активных имён типовых параметров шаблона: сохраняет/восстанавливает
/// `m_activeTemplateParams` (generic-функции не должны затирать активные параметры
/// объемлющего record-шаблона; ранние return не оставляют мусор).
class ActiveTemplateParamsGuard {
  public:
    ActiveTemplateParamsGuard(CppEmitContext& ectx, std::vector<std::string> names);
    ~ActiveTemplateParamsGuard();
    ActiveTemplateParamsGuard(const ActiveTemplateParamsGuard&) = delete;
    ActiveTemplateParamsGuard& operator=(const ActiveTemplateParamsGuard&) = delete;

  private:
    CppEmitContext& m_ectx;
    std::vector<std::string> m_saved;
};

/// Выходной поток и лексическая область текущей эмиссии.
struct OutputState {
    MapperFile out;
    std::vector<ScopeContext> scopeStack;
    std::vector<std::string> namespaceStack;
    int hiddenNamespaceDepth = 0;
    ModuleIndex currentModule = ModuleIndex::invalid();
    std::string selfHeaderInclude;

    [[nodiscard]] int indentLevel() const noexcept { return scopeStack.empty() ? 0 : scopeStack.back().indent; }
    [[nodiscard]] std::string indentPrefix() const { return std::string(static_cast<std::size_t>(indentLevel()) * 4, ' '); }
    [[nodiscard]] std::string qualifiedCppName(std::string_view name) const;
    [[nodiscard]] static std::string namespaceCppName(std::string_view text);
};

/// Переключатели текущей точки эмиссии.
struct EmitModeState {
    int exprDepth = 0;
    bool inDefaultArg = false;
    bool inCppBlock = false;
    bool forwardDeclOnly = false;
};

/// Факты объявлений: identity объявления → значение/индекс по тексту.
struct DeclFacts {
    std::set<const AstNodeBase*> publishedByHeader;
    std::set<const AstNodeBase*> headerPublishedDecls;
    std::unordered_map<const AstNodeBase*, std::string> nativeImports;
    std::unordered_map<std::string, TextDeclarationIndex> nativeImportsByText;
    std::unordered_map<const AstNodeBase*, StackCheckGuard> stackCheck;
    std::unordered_map<const AstNodeBase*, std::string> functionNames;
    std::unordered_map<std::string, TextDeclarationIndex> functionNamesByText;
    std::set<std::string> forwardFunctionDefaults;

    [[nodiscard]] static const AstNodeBase* declIdentity(const AstNodeBase* node) noexcept {
        return (node != nullptr && node->origin != nullptr) ? node->origin : node;
    }

    void registerNativeImport(const AstNodeBase* decl, std::string nativeName, std::string_view trustText);
    void registerStackCheck(const AstNodeBase* decl, StackCheckGuard guard);
    void registerFunctionName(const AstNodeBase* decl, std::string cppName, std::string_view trustText);
    [[nodiscard]] const std::string* findNativeImport(const AstNodeBase* decl) const noexcept;
    [[nodiscard]] const StackCheckGuard* findStackCheck(const AstNodeBase* decl) const noexcept;
    [[nodiscard]] TextLookup lookupNativeImportByText(std::string_view text) const;
    [[nodiscard]] TextLookup lookupFunctionCppNameByText(std::string_view text) const;
    [[nodiscard]] bool isPublishedByHeader(const AstNodeBase* node) const;
};

/// Накопители требований сгенерированного кода (инклуды/типы/линковка/POD-проверки).
struct EmissionAccumulators {
    mutable std::set<std::string> packageHeaders;
    mutable std::set<std::string> requiredIncludes;
    mutable std::set<TypeId> usedTypes;
    mutable std::vector<std::string> structPodAsserts;
    std::set<std::string> linkLibs;
};

/// Состояние единицы трансляции: entry-факты и режим однофайловой генерации.
struct UnitState {
    bool singleFileMode = false;
    std::string singleFileEntryName;
    std::string entryParams;
    bool sawEntry = false;
};

/// Временное связывание имени функции → C++-значение возврата при эмиссии пост-условий.
struct ResultBinding {
    std::string name;
    std::string cpp;
};

/// Общий контекст кодогенерации: зависимости и именованные зоны mutable-состояния.
class CppEmitContext {
  public:
    explicit CppEmitContext(Context& ctx, analysis::BehavioralModes behavioral = {});

    /// Резолв имён функций в C++-адреса (`reinterpret_cast<void*>(&c_name)`) для ограничения
    /// `m_stack_limit`. Неизвестное или неоднозначное имя — диагностика; возвращаются адреса
    /// только разрешённых функций (порядок сохраняется).
    std::vector<std::string> resolveStackCheckAddresses(const std::vector<std::string>& names);

    /// C++-namespace модуля (`<enc>`) — единственный механизм изоляции; пусто для `kInvalid`
    /// и неимпортируемого юнита. FAULT, если провайдер не внедрён.
    [[nodiscard]] std::string moduleNamespace(ModuleIndex owner) const;

    /// C++-имя сущности с модульной изоляцией (единая точка для значений/функций).
    [[nodiscard]] std::string cppName(std::string_view trustName, CppEntityKind kind, ModuleIndex owner) const;
    [[nodiscard]] std::string cppFuncName(std::string_view trustName, bool isOperator, bool isEntry, ModuleIndex owner) const;

    /// C++-имя свободного оператора: `<enc>::<ns>::operator<sym>`.
    [[nodiscard]] std::string operatorCppName(std::string_view symbol, std::string_view trustNamespacePath, ModuleIndex owner) const;

    Context& m_ctx;

    /// Поведенческие режимы кодогена (значения, разрешённые владельцем флагов — semantic).
    /// Кодоген НЕ читает diag::Options/FlagKind напрямую (снята зависимость transpiler → semantic).
    analysis::BehavioralModes m_behavioral;

    OutputState m_output;
    EmitModeState m_mode;

    DeclFacts m_declFacts;
    UnitState m_unit;
    ResultBinding m_result;

    EmissionAccumulators m_accumulators;

    /// Активные имена типовых параметров шаблон-класса (стек времени эмиссии).
    std::vector<std::string> m_activeTemplateParams;
};

/// RAII-обёртка автоматического namespace модуля: открывает `namespace <enc> {` (если имя
/// непусто) и гарантированно закрывает при любом выходе. Используется для тела модульного юнита
/// и для объявлений заголовка модуля.
class ModuleNamespaceGuard {
  public:
    ModuleNamespaceGuard(CppEmitContext& ectx, MapperFile out, std::string_view ns) noexcept;
    ~ModuleNamespaceGuard();
    ModuleNamespaceGuard(const ModuleNamespaceGuard&) = delete;
    ModuleNamespaceGuard& operator=(const ModuleNamespaceGuard&) = delete;

  private:
    CppEmitContext& m_ectx;
    MapperFile m_out;
    bool m_open = false;
};

} // namespace trust
