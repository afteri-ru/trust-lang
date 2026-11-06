#pragma once

// Порт конверсии Term→AST (R12/E4, R34): снимок НЕвладеющих ссылок на владельцев фактов,
// доступный конвертеру и конструкторам узлов без зависимости AST от `session`.
// Владельцы фактов живут дольше порта; порт лишь связывает их (как RunServices).

#include "attrs/attr_pool.hpp"
#include "diag/diag.hpp"
#include "diag/options.hpp"
#include "diag/report.hpp"
#include "location/location.hpp"
#include "utils/string_interner.hpp"

#include <format>
#include <type_traits>
#include <utility>

namespace trust {

/// Сервисы конверсии Term→AST: диагностика, пул атрибутов, опции, интернер строк.
/// `strings` — активный пул КОМПИЛЯЦИИ (узлы AST/HIR держат в него view; R1/R12).
struct ConvertServices {
    [[nodiscard]] DiagnosticEngine& diag() const noexcept { return m_diag; }
    [[nodiscard]] AttrPool& attrs() const noexcept { return m_attrs; }
    [[nodiscard]] const Options& opts() const noexcept { return m_opts; }
    [[nodiscard]] StringInterner& strings() const noexcept { return m_strings; }

    /// Как `Context::report`: severity берётся из `Options` (единый путь диагностики).
    template <typename T, typename... Args>
        requires std::is_enum_v<T>
    void report(MapperRange range, T kind, std::format_string<Args...> fmt, Args&&... args) const {
        ::trust::diag::reportWithOptions(m_diag, m_opts, kind, range, std::move(fmt), std::forward<Args>(args)...);
    }

    DiagnosticEngine& m_diag;
    AttrPool& m_attrs;
    const Options& m_opts;
    StringInterner& m_strings;
};

/// Помощник сборки порта на точке сборки (владелец, зависящий от `session`). Шаблон:
/// не создаёт ребра `ast→session` (Context-интерфейс проверяется при инстанцировании).
template <class Ctx>
[[nodiscard]] ConvertServices makeConvertServices(Ctx& ctx) {
    return ConvertServices{ctx.diag(), ctx.attrs(), ctx.opts(), ctx.strings()};
}

} // namespace trust
