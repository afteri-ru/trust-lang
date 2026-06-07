#pragma once

#include <cstdint>
#include <expected>
#include <format>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "diag/diag.hpp"
#include "diag/location.hpp"
#include "diag/mapper.hpp"
#include "diag/options.hpp"
#include "trust/version.h"
#include "ast/attr_pool.hpp"
#include "types/registry.hpp"
#include "pipeline/module_loader.hpp"

namespace trust {

class Macro;

// Context — фасад, объединяющий SourceMapWriter, DiagnosticEngine, Options,
// AttrPool, TypeRegistry, ModuleLoader.
// Владеет всеми объектами (unique_ptr) и предоставляет доступ к ним через геттеры.
class Context {
  public:
    Context();
    explicit Context(std::string_view basePath, std::string_view tempPath = "");

    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;

    // ── Доступ к компонентам ──
    SourceMapWriter& source();
    const SourceMapWriter& source() const;
    DiagnosticEngine& diag();
    Options& opts();
    AttrPool& attrs();
    TypeRegistry& types();
    const DiagnosticEngine& diag() const;
    const Options& opts() const;
    const AttrPool& attrs() const;

    ModuleLoader& loader();
    const ModuleLoader& loader() const;

    // ── Текущий (активный) модуль ──
    /// Индекс текущего модуля (верх стека ModuleLoader). nullopt = модуль не задан.
    [[nodiscard]] std::optional<std::size_t> currentModule() const noexcept { return m_currentModule; }
    /// Устанавливает активный модуль.
    void setCurrentModule(std::size_t idx) { m_currentModule = idx; }
    /// Сбрасывает активный модуль.
    void resetCurrentModule() { m_currentModule.reset(); }

    // ── Макросы ──
    /// Возвращает макрос, загруженный в этот контекст (может быть nullptr).
    std::shared_ptr<Macro> macro() const;
    /// Устанавливает макрос для этого контекста.
    void setMacro(std::shared_ptr<Macro> macro);

    // ── Макро-счётчики ──
    /// Возвращает текущее значение счётчика макросов и инкрементирует его.
    int nextMacroCounter() { return m_macroCounter++; }
    /// Возвращает текущее значение гигиенического счётчика и инкрементирует его.
    int nextHygienicCounter() { return m_hygienicCounter++; }
    /// Сброс счётчика макросов (для тестов).
    void resetMacroCounter(int val = 1) { m_macroCounter = val; }
    /// Сброс гигиенического счётчика (для тестов).
    void resetHygienicCounter(int val = 1) { m_hygienicCounter = val; }

    // ── Счётчик анонимных блоков ──
    /// Возвращает текущее значение счётчика блоков и инкрементирует его.
    int nextBlockCounter() { return m_blockCounter++; }
    /// Сброс счётчика блоков (для тестов).
    void resetBlockCounter(int val = 1) { m_blockCounter = val; }

    // report — convenience-метод: берёт severity из Options, вызывает DiagnosticEngine::report.
    template <typename... Args>
    void report(MapperRange range, OptKind kind, std::format_string<Args...> fmt, Args&&... args) {
        auto sev = opts().severity(kind);
        if (!sev.has_value())
            return;
        diag().report(*sev, range, std::move(fmt), std::forward<Args>(args)...);
    }

  private:
    std::unique_ptr<SourceMapWriter> m_sourceMap;
    std::unique_ptr<DiagnosticEngine> m_diag;
    std::optional<Options> m_opts;
    mutable std::unique_ptr<AttrPool> m_attr_pool;

    std::unique_ptr<TypeRegistry> m_type_registry;
    std::unique_ptr<ModuleLoader> m_moduleLoader;
    std::shared_ptr<Macro> m_macro;

    std::optional<std::size_t> m_currentModule; ///< Индекс текущего (активного) модуля

    // ── Макро-/блок-счётчики ──
    int m_macroCounter{1};
    int m_hygienicCounter{1};
    int m_blockCounter{1};
};

} // namespace trust