#pragma once

// Общий контекст кодогенерации (CppEmitContext): всё mutable-состояние транспилятора и
// низкоуровневые операции вывода/маппинга/отступа. Вынесено из монолитного CppTranspiler
// в отдельный, независимо тестируемый контейнер. Эмиттеры (TypeEmitter/DeclEmitter/...) и
// драйвер CppTranspiler разделяют этот контекст. CppEmitContext НЕ зависит от диспетчера.

#include "ast/ast_nodes.hpp"
#include "analysis/modes.hpp"
#include "attrs/attr_pool.hpp"
#include "diag/diag.hpp"
#include "diag/options.hpp"
#include "diag/report.hpp"
#include "location/decl_id.hpp"
#include "location/location.hpp"
#include "location/module_index.hpp"
#include "session/run_services.hpp"
#include "transpiler/emit/emit_common.hpp"
#include "target/runtime_symbols.hpp"
#include "target/target.hpp"
#include "types/type_id.hpp"

#include <cstddef>
#include <format>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace trust {

class CppEmitContext;
class ModuleResolver;

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
    DeclId decl{};
    bool ambiguous = false;

    void add(DeclId identity) noexcept {
        if (!decl.isValid()) {
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
    /// Директива `#include "<own>.hpp"` собственного заголовка модуля: при генерации
    /// заголовка/`.cppt` include типа из ТОГО ЖЕ модуля не эмитится (self-include бессмыслен).
    std::string ownHeaderInclude;
    /// Выражение приёмника для системного имени `$0` (текущий объект): `(*this)` внутри
    /// метода-члена; вне метода пусто (`$0` недопустим).
    std::string selfExpr;

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

/// Факты объявлений: identity объявления (`DeclId`, R23) → значение/индекс по тексту.
/// Адресная идентичность узлов не используется: `DeclId` переживает клонирование AST→HIR.
struct DeclFacts {
    /// Опубликовано заголовком текущей единицы трансляции — идентичности объявлений (`DeclId`).
    /// `TypeDecl`-обёртка alias-объявления несёт собственный id (`assignDeclarationIds`), для типа
    /// с телом id принадлежит `ClassDecl`/`RecordDecl` (одна идентичность на объявление).
    std::set<DeclId> publishedByHeader;
    std::set<DeclId> headerPublishedDecls;
    std::map<DeclId, StackCheckGuard> stackCheck;
    std::map<DeclId, std::string> functionNames;
    /// Поиск по spelling, введённому пользователем (`--stack-check-functions`), — не identity
    /// объявления: второй кандидат с тем же текстом делает запись неоднозначной.
    std::unordered_map<std::string, TextDeclarationIndex> userNamesByText;
    std::set<std::string> forwardFunctionDefaults;

    void registerStackCheck(const AstNodeBase& decl, StackCheckGuard guard);
    void registerFunctionName(const AstNodeBase& decl, std::string cppName, std::string_view trustText);
    [[nodiscard]] const StackCheckGuard* findStackCheck(DeclId decl) const noexcept;
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

/// Кодоген получает ЕДИНЫЙ read-model порт прогона (`RunServices`, R12/E4) — тот же, что стадии
/// бэкенда; мутабельного `Context` кодоген НЕ видит. Псевдоним документирует назначение порта
/// у кодогена; определение — `session/run_services.hpp` (единственный источник, AGENTS §4).
using CodegenServices = RunServices;

/// Общий контекст кодогенерации: зависимости и именованные зоны mutable-состояния.
class CppEmitContext {
  public:
    explicit CppEmitContext(CodegenServices services, analysis::BehavioralModes behavioral = {});

    /// Резолв имён функций в C++-адреса (`reinterpret_cast<void*>(&c_name)`) для ограничения
    /// `m_stack_limit`. Неизвестное или неоднозначное имя — диагностика; возвращаются адреса
    /// только разрешённых функций (порядок сохраняется).
    std::vector<std::string> resolveStackCheckAddresses(const std::vector<std::string>& names);

    /// C++-имя сущности (единая точка для значений/функций). Модуль в имя не входит.
    [[nodiscard]] std::string cppName(std::string_view trustName, CppEntityKind kind) const;

    /// C++-имя свободного оператора: `<ns>::operator<sym>` (модуль в имя не входит).
    [[nodiscard]] std::string operatorCppName(std::string_view symbol, std::string_view trustNamespacePath) const;

    /// Порт целевых C++-фактов типа: карта фактов (`target`) + языковые предикаты адресации `types`.
    [[nodiscard]] TypeCppFactsView cppFactsView() const;

    /// C/C++-имя нативной привязки объявления — ЧТЕНИЕ у владельца (`target::DeclCppFacts`, R1/R12),
    /// без локальной копии. Пусто — привязки нет (объявление не нативное/не экспортное).
    [[nodiscard]] std::string_view nativeCppNameOf(const AstNodeBase* decl) const noexcept;

    CodegenServices m_services;

    /// Поведенческие режимы кодогена — ДАННЫЕ, разрешённые владельцем флагов (`transpiler`,
    /// `codegenModes`) один раз за прогон. Кодоген НЕ читает `diag::Options`/`FlagKind` (R39).
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

} // namespace trust
