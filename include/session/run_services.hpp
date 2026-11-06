#pragma once

// Read-model порт прогона (R12/E4): снимок НЕвладеющих ссылок на владельцев фактов прогона,
// доступный потребителям без права записи (кодоген, solver, стадии бэкенда). Порт НЕ даёт
// мутабельного доступа к реестру `types` — только `readTypes()`; запись остаётся у зоны записи
// (семантика/`Pipeline`). Собирает владелец фасада (`Context::runServices`, см. R34/E4).
//
// ЕДИНЫЙ порт: и кодоген, и stage-окружение бэкенда используют его (R12 «read-model, а не
// мутабельный корень»); второго набора тех же аксессоров нет (AGENTS §4). Имя `CodegenServices`
// в зоне `transpiler` — псевдоним этого порта (документирует назначение у кодогена).

#include "attrs/attr_pool.hpp"
#include "diag/diag.hpp"
#include "diag/options.hpp"
#include "diag/report.hpp"
#include "location/location.hpp"
#include "module_api/module_resolver.hpp"
#include "sourcemap/mapper.hpp"
#include "target/decl_cpp_facts.hpp"
#include "target/target.hpp"
#include "types/type_read.hpp"

#include <format>
#include <type_traits>
#include <utility>

namespace trust {

/// Порт-набор сервисов прогона: владельцы фактов живут дольше порта; порт лишь связывает их
/// (R12). `TypeReadModel` хранится ПО ЗНАЧЕНИЮ — view, не владеющий хранилищем (R1).
struct RunServices {
    [[nodiscard]] const TypeReadModel& readTypes() const noexcept { return m_types; }
    [[nodiscard]] DiagnosticEngine& diag() const noexcept { return m_diag; }
    [[nodiscard]] SourceMapWriter& source() const noexcept { return m_map; }
    [[nodiscard]] const AttrPool& attrs() const noexcept { return m_attrs; }
    [[nodiscard]] const Options& opts() const noexcept { return m_opts; }
    [[nodiscard]] const DeclCppFacts& declCppFacts() const noexcept { return m_declCppFacts; }
    [[nodiscard]] const TargetContract& target() const noexcept { return m_target; }
    [[nodiscard]] const ModuleResolver* moduleResolver() const noexcept { return m_moduleResolver; }

    /// Как `Context::report`: severity берётся из `Options` владельца (единый путь диагностики).
    template <typename T, typename... Args>
        requires std::is_enum_v<T>
    void report(MapperRange range, T kind, std::format_string<Args...> fmt, Args&&... args) const {
        ::trust::diag::reportWithOptions(m_diag, m_opts, kind, range, std::move(fmt), std::forward<Args>(args)...);
    }

    const TypeReadModel m_types;
    DiagnosticEngine& m_diag;
    SourceMapWriter& m_map;
    const AttrPool& m_attrs;
    const Options& m_opts;
    const DeclCppFacts& m_declCppFacts;
    const TargetContract& m_target;
    const ModuleResolver* m_moduleResolver = nullptr;
};

} // namespace trust
