#pragma once

#include <optional>
#include <string_view>

namespace trust {

// X-macro: M(EnumName, "name", lspSeverity)
// Единый источник для enum Severity, канонических имён и LSP-кодов severity.
// lspSeverity - LSP DiagnosticSeverity (1=Error, 2=Warning, 3=Info, 4=Hint).
#ifndef SEVERITIES
#define SEVERITIES(M)        \
    M(Remark, "remark", 4)   \
    M(Note, "note", 3)       \
    M(Warning, "warning", 2) \
    M(Error, "error", 1)     \
    M(Fatal, "fatal", 1)
#endif

enum class Severity : int {
#define SEVERITY_ENUM(name, str, lsp) name,
    SEVERITIES(SEVERITY_ENUM)
#undef SEVERITY_ENUM
};

// Имена severity (порядок = порядок enum Severity).
inline constexpr std::string_view kSeverityNames[] = {
#define SEVERITY_NAME(name, str, lsp) str,
    SEVERITIES(SEVERITY_NAME)
#undef SEVERITY_NAME
};

/// LSP DiagnosticSeverity (1=Error, 2=Warning, 3=Info, 4=Hint) по индексу Severity.
inline constexpr int kSeverityToLsp[] = {
#define SEVERITY_LSP(name, str, lsp) lsp,
    SEVERITIES(SEVERITY_LSP)
#undef SEVERITY_LSP
};

#define SEVERITY_COUNT(name, str, lsp) +1
inline constexpr int kSeverityCount = 0 SEVERITIES(SEVERITY_COUNT);
#undef SEVERITY_COUNT

/// Каноническое имя severity; пусто при невалидном.
constexpr std::string_view severityName(Severity s) {
    const int i = static_cast<int>(s);
    return (i >= 0 && i < kSeverityCount) ? kSeverityNames[i] : std::string_view{};
}

/// Имя -> Severity (nullopt, если неизвестно).
constexpr std::optional<Severity> severityFromName(std::string_view name) {
    for (int i = 0; i < kSeverityCount; ++i) {
        if (kSeverityNames[i] == name) {
            return static_cast<Severity>(i);
        }
    }
    return std::nullopt;
}

} // namespace trust