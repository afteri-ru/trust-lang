#pragma once

#include <cstdint>
#include <expected>
#include <format>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "diag/diag.hpp"
#include "diag/report.hpp"
#include "diag/source_text_provider.hpp"
#include "location/location.hpp"
#include "sourcemap/mapper.hpp"
#include "module_api/module_resolver.hpp"
#include "session/run_services.hpp"
#include "types/type_read.hpp"
#include "utils/error.hpp"
#include "diag/options.hpp"
#include "trust/version.h"
#include "attrs/attr_pool.hpp"
#include "target/decl_cpp_facts.hpp"
#include "utils/string_interner.hpp"

namespace trust {

class Macro;
class TypeSystem;
class TargetContract;

// Запись о макроопределении, записанная в Context во время парсинга. Хранится отдельно
// от таблицы макросов (Macro), т.к. макросы модуля удаляются из неё при PopScope - для
// LSP-автодополнения и навигации нужны все определения независимо от текущего модуля.
struct MacroDef {
    std::string name;  ///< Имя макроса (первый терм группы, может содержать '@')
    MapperRange range; ///< Диапазон определения макроса в исходнике
    /// Документирующий комментарий (`///`, `/** */`, `##`) перед макроопределением (для LSP-док).
    /// Владелец текста - пул строк прогона (`context.strings()`): view валиден, пока жив
    /// Context/результат прогона (LSP держит Context в CachedSource, результат - свой пул).
    std::string_view documentation;
    /// Модуль разбора, в котором определен макрос. `nullopt` - вне модуля (predef/stdlib DSL):
    /// такой макрос виден в любом файле. Макросы НЕ экспортируются: определение в модуле
    /// видно только этому модулю (макросы импортируемого модуля не наследуются).
    std::optional<ModuleIndex> module;
};

// Context - фасад, объединяющий SourceMapWriter, DiagnosticEngine, Options,
// AttrPool, TypeSystem, ModuleResolver.
// Владеет всеми объектами (unique_ptr) и предоставляет доступ к ним через геттеры.
class Context : public SourceTextProvider {
  public:
    Context();
    explicit Context(std::string_view basePath, std::string_view tempPath = "");

    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;

    SourceMapWriter& source();
    const SourceMapWriter& source() const;

    [[nodiscard]] std::string_view fileName(MapperLocation loc) const override;
    [[nodiscard]] std::string_view fileContents(MapperLocation loc) const override;
    [[nodiscard]] SourceTextProvider::LineColumn lineColumn(MapperLocation loc) const override;
    [[nodiscard]] std::vector<ExpansionFrame> expansionChain(MapperRange range) const override;
    DiagnosticEngine& diag();
    Options& opts();
    AttrPool& attrs();
    /// Интернер НЕ-исходного текста КОМПИЛЯЦИИ (см. utils/string_interner.hpp): один пул на
    /// Context, append-only (выданные view не инвалидируются, пока жив пул). Вне компиляции
    /// ручные/тестовые узлы интернируются в процессный пул по явному `ManualInternerScope`.
    StringInterner& strings() const { return *m_strings; }
    /// Пул строк компиляции (shared ownership): AST/HIR/SymbolIndex держат view в него;
    /// результат анализа хранит shared_ptr, чтобы тексты жили вместе с результатом.
    std::shared_ptr<StringInterner> stringsPool() const { return m_strings; }
    /// READ-ONLY доступ к реестру (R12/E1): мутации — только у зоны записи через capability
    /// (`AnalysisContext::registry`/инжект из `Pipeline`); фасад мутабельный реестр НЕ отдаёт.
    const TypeSystem& types() const;
    /// READ-ONLY проекция реестра для потребителей без права записи (кодоген/solver/LSP/verify).
    /// Мутации возможны только через `TypeSystem&` (владелец — Pipeline).
    [[nodiscard]] TypeReadModel readTypes() const;
    /// Порт прогона (R12/E4): снимок невладеющих ссылок на владельцев фактов для потребителей
    /// без права записи (кодоген, solver, стадии бэкенда). Мутабельного реестра порт не даёт.
    [[nodiscard]] RunServices runServices();
    /// Устанавливает внешний (не владеющий) реестр типов. Context не владеет
    /// TypeSystem: он создаётся и владеется вышележащим слоем (Pipeline) и
    /// внедряется сюда, чтобы избежать цикла diag ↔ types.
    void setTypes(TypeSystem* reg) { m_type_registry = reg; }

    /// Порт целевого контракта прогона (целевые C++-факты, вне языковой модели; R1/R4/R37).
    /// Владелец — `Pipeline` (per-run `TargetContract`); `Context` хранит невладеющий указатель.
    [[nodiscard]] TargetContract& target();
    [[nodiscard]] const TargetContract& target() const;
    void setTarget(TargetContract* target) noexcept { m_target = target; }
    /// Удобный аксессор целевых decl-фактов (`target().declCppFacts()`), без include `target.hpp`.
    [[nodiscard]] DeclCppFacts& declCppFacts();
    [[nodiscard]] const DeclCppFacts& declCppFacts() const;

    const DiagnosticEngine& diag() const;
    const Options& opts() const;
    const AttrPool& attrs() const;

    /// Узкий резолвер загрузки модулей для парсера (низкий слой; реализация - ModuleLoader).
    /// Владелец загрузчика — Pipeline: `Context` хранит только невладеющий резолвер, поэтому
    /// syntax/ast не зависят от module_loader (ни include, ни символы линковки).
    [[nodiscard]] ModuleResolver* moduleResolver() const noexcept { return m_moduleResolver; }
    void setModuleResolver(ModuleResolver* resolver) noexcept { m_moduleResolver = resolver; }

    /// Возвращает макрос, загруженный в этот контекст (может быть nullptr).
    std::shared_ptr<Macro> macro() const;
    /// Устанавливает макрос для этого контекста.
    void setMacro(std::shared_ptr<Macro> macro);

    /// Модуль, тело которого парсится ПРЯМО СЕЙЧАС (ставит ModuleLoader на время разбора).
    /// `nullopt` - разбор вне модуля (predef/stdlib DSL): определения считаются глобальными.
    void setCurrentParseModule(std::optional<ModuleIndex> module) noexcept { m_currentParseModule = module; }
    [[nodiscard]] std::optional<ModuleIndex> currentParseModule() const noexcept { return m_currentParseModule; }

    /// Регистрирует макроопределение (имя + диапазон + модуль) - для LSP-навигации/автодополнения.
    void recordMacro(std::string name, MapperRange range);
    /// Все зарегистрированные макроопределения (не теряются после PopScope модуля).
    const std::vector<MacroDef>& macroDefs() const noexcept { return m_macroDefs; }

    /// Переопределяет док УЖЕ СУЩЕСТВУЮЩЕГО макроса (прагма `@__PRAGMA_DOC__`).
    /// Ключ нормализуется (ведущий '@' срезается - ключ группы = первый терм).
    /// Возвращает false, если такого макроса (ключа) в хранилище нет - ничего не пишет
    /// (прагма выдаёт error). Ограничение: один док на макрос; для группы с разными
    /// арностями (break, break $label, break $a $b) побеждает последняя запись.
    bool setMacroDoc(std::string_view name, std::string_view doc);
    /// Вставляет/обновляет док макроса (для записи `##`-доков через recordMacro и
    /// сидирования дефолтов предdef-макросов). Ключ нормализуется так же.
    void addMacroDoc(std::string_view name, std::string_view doc);
    /// Док макроса по ключу - читается и с ведущим '@', и без (нормализуется).
    /// nullptr, если дока нет. View живёт в пуле строк ЭТОГО прогона (`strings()`).
    const std::string_view* macroDoc(std::string_view name) const noexcept;
    /// Доки макросов ЭТОГО прогона (ключ = первый терм без '@'):
    /// predef/DSL сидинг + `##`-доки определений + прагма-переопределения.
    /// Прозрачный компаратор std::less<> - поиск по string_view без временной строки.
    const std::map<std::string, std::string_view, std::less<>>& macroDocs() const noexcept { return m_macroDocs; }

    /// Возвращает текущее значение счётчика макросов и инкрементирует его.
    int nextMacroCounter() { return m_macroCounter++; }
    /// Возвращает текущее значение гигиенического счётчика и инкрементирует его.
    int nextHygienicCounter() { return m_hygienicCounter++; }
    /// Сброс счётчика макросов (для тестов).
    void resetMacroCounter(int val = 1) { m_macroCounter = val; }
    /// Сброс гигиенического счётчика (для тестов).
    void resetHygienicCounter(int val = 1) { m_hygienicCounter = val; }

    /// Возвращает текущее значение счётчика блоков и инкрементирует его.
    int nextBlockCounter() { return m_blockCounter++; }
    /// Сброс счётчика блоков (для тестов).
    void resetBlockCounter(int val = 1) { m_blockCounter = val; }

    // report - convenience-метод: берёт severity из Options, вызывает DiagnosticEngine::report.
    // kind - пер-компонентный id (ADL: diagName(kind) из namespace компоненты).
    template <typename T, typename... Args>
        requires std::is_enum_v<T>
    void report(MapperRange range, T kind, std::format_string<Args...> fmt, Args&&... args) {
        diag::reportWithOptions(*m_diag, *m_opts, kind, range, std::move(fmt), std::forward<Args>(args)...);
    }

  private:
    std::unique_ptr<SourceMapWriter> m_sourceMap;
    /// Пул строк компиляции: владелец не-исходных текстов AST/HIR/символов.
    /// shared_ptr разделяется с PipelineResult (тексты переживают Context).
    std::shared_ptr<StringInterner> m_strings = std::make_shared<StringInterner>();
    std::unique_ptr<DiagnosticEngine> m_diag;
    std::optional<Options> m_opts;
    mutable std::unique_ptr<AttrPool> m_attr_pool;

    TypeSystem* m_type_registry = nullptr;      ///< Невладеющий: внедряется через setTypes()
    TargetContract* m_target = nullptr;         ///< Невладеющий: внедряется через setTarget()
    ModuleResolver* m_moduleResolver = nullptr; ///< Невладеющий: внедряется через setModuleResolver()
    std::shared_ptr<Macro> m_macro;
    std::vector<MacroDef> m_macroDefs;               ///< Реестр макроопределений для LSP
    std::optional<ModuleIndex> m_currentParseModule; ///< модуль текущего разбора (см. setCurrentParseModule)

    // Доки макросов ЭТОГО прогона (ключ = первый терм без '@'): predef/DSL-сидинг,
    // `##`-доки определений и прагма-переопределения. Значения - view в пул строк прогона
    // (`m_strings`), поэтому живут, пока жив Context/результат его прогона.
    // Прозрачный компаратор std::less<> - heterogeneous lookup (поиск по string_view без аллокации).
    std::map<std::string, std::string_view, std::less<>> m_macroDocs;

    int m_macroCounter{1};
    int m_hygienicCounter{1};
    int m_blockCounter{1};
};

} // namespace trust