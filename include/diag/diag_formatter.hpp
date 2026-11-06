#pragma once

// Формат вывода диагностик: сменный форматтер (text по умолчанию, json, sarif).
// Контракт:
//   * `DiagnosticEntry` (diag.hpp) — ЕДИНСТВЕННАЯ запись диагностики; text/json/sarif —
//     проекции одной и той же записи, второй модели данных нет (единый источник фактов);
//   * движок фильтрует/считает/накапливает записи и передаёт их форматтеру целиком
//     (включая fixit-подсказки); форматтер отвечает ТОЛЬКО за рендеринг;
//   * форматтер пишет в поток (по умолчанию `trust::errs()` — поток диагностик);
//   * JSON/SARIF пишут ВАЛИДНЫЙ JSON: массив/документ открывается конструктором и закрывается
//     деструктором — у движка нет сигнала «конец прогона», деструктор — единственная точка.

#include "diag/source_text_provider.hpp"

#include <memory>
#include <optional>
#include <string_view>

namespace trust {

struct DiagnosticEntry;
struct FixitSuggestion;

/// Машиночитаемый формат вывода диагностик (--diagnostics-format=<text|json|sarif>).
/// X-macro — единственный источник enum и имён (как SEVERITIES в severity.hpp; ручной
/// стрингификации нет).
#ifndef DIAGNOSTICS_FORMATS
#define DIAGNOSTICS_FORMATS(M) \
    M(Text, "text")            \
    M(Json, "json")            \
    M(Sarif, "sarif")
#endif

enum class DiagnosticsFormat : int {
#define DIAGNOSTICS_FORMAT_ENUM(name, str) name,
    DIAGNOSTICS_FORMATS(DIAGNOSTICS_FORMAT_ENUM)
#undef DIAGNOSTICS_FORMAT_ENUM
};

inline constexpr std::string_view kDiagnosticsFormatNames[] = {
#define DIAGNOSTICS_FORMAT_NAME(name, str) str,
    DIAGNOSTICS_FORMATS(DIAGNOSTICS_FORMAT_NAME)
#undef DIAGNOSTICS_FORMAT_NAME
};

#define DIAGNOSTICS_FORMAT_COUNT(name, str) +1
inline constexpr int kDiagnosticsFormatCount = 0 DIAGNOSTICS_FORMATS(DIAGNOSTICS_FORMAT_COUNT);
#undef DIAGNOSTICS_FORMAT_COUNT

/// Имя формата -> DiagnosticsFormat (nullopt, если неизвестно). Тихого fallback нет:
/// невалидное значение отклоняет parseCommandLine (ошибка, а не text по умолчанию).
constexpr std::optional<DiagnosticsFormat> diagnosticsFormatFromName(std::string_view name) {
    for (int i = 0; i < kDiagnosticsFormatCount; ++i) {
        if (kDiagnosticsFormatNames[i] == name) {
            return static_cast<DiagnosticsFormat>(i);
        }
    }
    return std::nullopt;
}

/// Информация об инструменте для SARIF-шапки (имя/версия компилятора).
struct ToolInfo {
    std::string_view name;
    std::string_view version;
};

/// Сменный форматтер диагностик: рендеринг одной записи (проекция `DiagnosticEntry`).
class DiagnosticFormatter {
  public:
    DiagnosticFormatter() = default;
    virtual ~DiagnosticFormatter() = default;
    DiagnosticFormatter(const DiagnosticFormatter&) = delete;
    DiagnosticFormatter& operator=(const DiagnosticFormatter&) = delete;

    /// Отрисовать/сериализовать запись. `source` — текст исходника (null, если провайдер
    /// не внедрён движком: локация недоступна).
    virtual void write(const DiagnosticEntry& entry, const SourceTextProvider* source) = 0;

    /// Текстовая пометка fix-it (консоль); для структурированных форматов — no-op
    /// (fixits уже входят в `entry` и сериализуются там).
    virtual void writeFixitNote(const FixitSuggestion& fixit, const SourceTextProvider* source) {
        (void)fixit;
        (void)source;
    }
};

/// Создать форматтер. Для sarif `tool` заполняет шапку `tool.driver`; для text/json игнорируется.
[[nodiscard]] std::unique_ptr<DiagnosticFormatter> makeDiagnosticFormatter(DiagnosticsFormat format, ToolInfo tool = {});

} // namespace trust
