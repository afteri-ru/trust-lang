#pragma once

#include <cstddef>
#include <deque>
#include <format>
#include <initializer_list>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "location/location.hpp"
#include "diag/diag_formatter.hpp"
#include "diag/severity.hpp"
#include "diag/options.hpp"
#include "diag/source_text_provider.hpp"

namespace trust {

class FatalError : public std::runtime_error {
  public:
    explicit FatalError(const std::string& msg)
    : std::runtime_error(msg) {}
};

struct FixitSuggestion {
    MapperRange range;       // что заменяем
    std::string replacement; // на что заменяем
};

struct DiagnosticEntry {
    MapperRange range;
    Severity severity;
    std::string_view optName; ///< cli-имя диагностики; пустое = без привязки к опции (всегда выводится)
    std::string message;
    std::vector<FixitSuggestion> fixits; // привязанные fixit-подсказки
};

class Options;

/// Лимит ошибок по умолчанию (как в clang: -ferror-limit=20). 0 = без лимита.
inline constexpr std::size_t kDefaultErrorLimit = 20;

class DiagnosticEngine {
  public:
    DiagnosticEngine();
    virtual ~DiagnosticEngine() = default;
    DiagnosticEngine(const DiagnosticEngine&) = delete;
    DiagnosticEngine& operator=(const DiagnosticEngine&) = delete;

    void setSourceTextProvider(const SourceTextProvider* provider) { m_source = provider; }

    /// Формат вывода диагностик (по умолчанию — text). Владение форматтером — у движка:
    /// переживает прогон и не зависит от времени жизни форматтера у вызывающего.
    void setFormatter(std::unique_ptr<DiagnosticFormatter> formatter) { m_formatter = std::move(formatter); }

    void setMinSeverity(Severity sev);
    Severity minSeverity() const;

    /// Лимит ошибок (0 = без лимита). После достижения выводится ОДНО итоговое
    /// Note-сообщение, дальнейшие не-Fatal диагностики подавляются.
    void setErrorLimit(std::size_t limit);
    std::size_t errorLimit() const;
    /// true, если лимит задан (ненулевой) и число ошибок достигло его.
    bool errorLimitReached() const;

    void setOptions(Options* opts) { m_opts = opts; }

    /// С пер-компонентным id диагностики. Метаданные берутся через ADL: diagName(id) из
    /// namespace компоненты (id - пер-компонентный enum, см. TRUST_DIAG_SET).
    /// Возвращает nullptr, если диагностика подавлена (severity = ignore).
    template <typename T, typename... Args>
        requires std::is_enum_v<T>
    DiagnosticEntry* report(Severity sev, MapperRange range, T opt, std::format_string<Args...> fmt, Args&&... args) {
        return output(sev, range, diagName(opt), std::format(fmt, std::forward<Args>(args)...));
    }

    /// Без id - всегда выводится (не проверяется по Options).
    template <typename... Args>
    DiagnosticEntry* report(Severity sev, MapperRange range, std::format_string<Args...> fmt, Args&&... args) {
        return output(sev, range, {}, std::format(fmt, std::forward<Args>(args)...));
    }

    /// С MapperLocation, без id.
    template <typename... Args>
    DiagnosticEntry* report(Severity sev, MapperLocation loc, std::format_string<Args...> fmt, Args&&... args) {
        return output(sev, MapperRange{loc, loc}, {}, std::format(fmt, std::forward<Args>(args)...));
    }

    /// Зафиксировать fixit для указанной диагностики. entry может быть nullptr (тогда ничего не делается).
    void fixit(DiagnosticEntry* entry, MapperRange range, std::string_view replacement);

    /// Кадр стека контекста диагностики (R30): диапазон точки контекста (инстанциация, раскрытие
    /// макроса) и её текст. Стек — состояние ВЛАДЕЛЬЦА диагностики (`diag`), не AST/узла: иначе
    /// диагностическое состояние осело бы в синтаксисе (R30).
    struct ContextFrame {
        MapperRange range;
        std::string label;
    };

    /// RAII-кадр стека контекста: добавлен на входе в контекст, снят при любом выходе (включая
    /// исключение EXPECT), чтобы состояние не «протекало» между вызовами (LSP-режим).
    class ContextScope {
      public:
        ContextScope(DiagnosticEngine& engine, MapperRange range, std::string label)
        : m_engine(&engine) {
            m_engine->pushContext(range, std::move(label));
        }
        ~ContextScope() {
            if (m_engine != nullptr) {
                m_engine->popContext();
            }
        }
        ContextScope(const ContextScope&) = delete;
        ContextScope& operator=(const ContextScope&) = delete;
        ContextScope(ContextScope&&) = delete;
        ContextScope& operator=(ContextScope&&) = delete;

      private:
        DiagnosticEngine* m_engine;
    };

    void pushContext(MapperRange range, std::string label);
    void popContext();
    [[nodiscard]] bool contextEmpty() const noexcept { return m_contextStack.empty(); }

    int errorCount() const;
    int warningCount() const;

    // Доступ к накопленным диагностикам. `deque` даёт стабильные адреса записей: `report()`
    // возвращает указатель, который переживает добавление Note-кадров контекста.
    const std::deque<DiagnosticEntry>& diagnostics() const { return m_diagnostics; }

    void clear();

  private:
    DiagnosticEntry* output(Severity sev, MapperRange range, std::string_view opt_name, std::string_view msg, bool internal = false);

    Severity m_minSeverity = Severity::Remark;
    Options* m_opts = nullptr;
    int m_errorCount = 0;
    int m_warningCount = 0;
    std::size_t m_errorLimit = kDefaultErrorLimit;
    bool m_errorLimitReported = false;
    const SourceTextProvider* m_source = nullptr;     ///< источник текста для печати (внедряет session)
    std::unique_ptr<DiagnosticFormatter> m_formatter; ///< рендеринг диагностик (default: text)
    std::deque<DiagnosticEntry> m_diagnostics;        // стабильные адреса: report() отдаёт указатель наружу
    std::vector<ContextFrame> m_contextStack;         ///< стек контекста диагностики (R30: инстанциация/раскрытие)
    bool m_emittingChain = false;                     ///< защита от рекурсии при выводе цепочки Notes контекста
};

} // namespace trust