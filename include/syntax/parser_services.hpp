#pragma once

// Порт парсинга (R12/E4, R34): снимок НЕвладеющих ссылок для Scanner/Macro/Parser/
// PredefMacroResolver/PragmaEvaluator без зависимости `syntax` от `session`.
// Владелец `Macro` и sink доков макросов — точка сборки (владелец `Context`); порт связывает их.
//
// Снимок `currentParseModule` берётся в момент сборки порта (перед созданием `Parser`), что
// совпадает с установкой модуля разбора в загрузчике (ParseScope::enter → setCurrentParseModule).

#include "diag/diag.hpp"
#include "diag/options.hpp"
#include "diag/report.hpp"
#include "location/location.hpp"
#include "location/module_index.hpp"
#include "module_api/module_resolver.hpp"
#include "sourcemap/mapper.hpp"
#include "utils/string_interner.hpp"

#include <format>
#include <functional>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>

namespace trust {

class Macro;

/// Сервисы парсинга: диагностика, источник, опции (мутабельные для прагм), пул строк прогона,
/// резолвер модулей, активный `Macro`, текущий модуль разбора и sink макроопределений (LSP).
struct ParserServices {
    [[nodiscard]] DiagnosticEngine& diag() const noexcept { return m_diag; }
    [[nodiscard]] SourceMapWriter& source() const noexcept { return m_map; }
    [[nodiscard]] Options& opts() const noexcept { return m_opts; }
    /// Пул строк прогона: `Parser` делает его активным на время своего существования, поэтому
    /// доки макросов/тексты разбора принадлежат владельцу прогона (E9).
    [[nodiscard]] StringInterner& strings() const noexcept { return m_strings; }
    [[nodiscard]] ModuleResolver* moduleResolver() const noexcept { return m_moduleResolver; }
    [[nodiscard]] Macro* macro() const noexcept { return m_macro; }
    [[nodiscard]] std::optional<ModuleIndex> currentParseModule() const noexcept { return m_currentParseModule; }

    /// Регистрирует макроопределение (имя + диапазон) для LSP-навигации/автодополнения.
    void recordMacro(std::string name, MapperRange range) { m_recordMacro(std::move(name), range); }

    /// Док макроса в хранилище прогона.
    void addMacroDoc(std::string_view name, std::string_view doc) const { m_addMacroDoc(name, doc); }

    /// Переопределяет док существующего макроса; false — макроса с таким ключом нет.
    [[nodiscard]] bool setMacroDoc(std::string_view name, std::string_view doc) const { return m_setMacroDoc(name, doc); }

    /// Как `Context::report`: severity берётся из `Options` (единый путь диагностики).
    template <typename T, typename... Args>
        requires std::is_enum_v<T>
    void report(MapperRange range, T kind, std::format_string<Args...> fmt, Args&&... args) const {
        ::trust::diag::reportWithOptions(m_diag, m_opts, kind, range, std::move(fmt), std::forward<Args>(args)...);
    }

    DiagnosticEngine& m_diag;
    SourceMapWriter& m_map;
    Options& m_opts;
    StringInterner& m_strings;
    ModuleResolver* m_moduleResolver = nullptr;
    Macro* m_macro = nullptr;
    std::optional<ModuleIndex> m_currentParseModule;
    std::function<void(std::string, MapperRange)> m_recordMacro;
    std::function<void(std::string_view, std::string_view)> m_addMacroDoc;
    std::function<bool(std::string_view, std::string_view)> m_setMacroDoc;
};

/// Помощник сборки порта на точке сборки (владелец, зависящий от `session`). Шаблон:
/// не создаёт ребра `syntax→session` (Context-интерфейс проверяется при инстанцировании).
/// `Macro`-связка берётся ПО ТЕКУЩЕМУ `ctx.macro()`; собирать порт после `setMacro`.
/// Sink'и адресуют владельца прогона: порт обязан жить не дольше `ctx`.
template <class Ctx>
[[nodiscard]] ParserServices makeParserServices(Ctx& ctx) {
    ParserServices services{ctx.diag(), ctx.source(), ctx.opts(), ctx.strings(), ctx.moduleResolver(), ctx.macro().get(),
                            ctx.currentParseModule()};
    services.m_recordMacro = [&ctx](std::string name, MapperRange range) { ctx.recordMacro(std::move(name), range); };
    services.m_addMacroDoc = [&ctx](std::string_view name, std::string_view doc) { ctx.addMacroDoc(name, doc); };
    services.m_setMacroDoc = [&ctx](std::string_view name, std::string_view doc) { return ctx.setMacroDoc(name, doc); };
    return services;
}

} // namespace trust
