#pragma once

// Нормализация локальных имён объявлений: сигил `$` и канонический текст цели.
// Свободные функции: зона используется Var/Destructure-анализаторами, состояние не нужно.

#include "location/location.hpp"
#include "utils/strings.hpp"

#include <string>
#include <string_view>

namespace trust {

class AnalysisContext;
class IdentName;

/// Нормализация bare-имени в локальном скоупе (`x` → `$x`, опция -Wsigil).
[[nodiscard]] std::string normalizeLocalSigil(AnalysisContext& actx, IdentName& node, MapperRange range, bool isLocal);
/// Каноническое имя цели деструктуризации (без мутации узла и диагностики).
[[nodiscard]] std::string canonicalTargetName(const AnalysisContext& actx, const IdentName& target);

/// Имя без сигила локальности '$': в SymbolTable локальные хранятся как `$x`, а в AST-узлах
/// текст на момент onNode ещё без '$'. Единая bare-форма ключей зависимых/источников.
/// ВНИМАНИЕ: не путать с utils::bareName (срезает '%'/'^' в ключе метода — другая семантика).
[[nodiscard]] inline std::string bareLocalName(std::string_view name) {
    if (!name.empty() && name.front() == '$') {
        return std::string(name.substr(1));
    }
    return std::string(name);
}

/// Простое имя (без сигила/квалификатора) — кандидат на нормализацию `x → $x` (-Wsigil).
inline bool isSimpleVarName(std::string_view name) noexcept {
    if (name.empty()) {
        return false;
    }
    const char c = name.front();
    if (c == '$' || c == '%' || c == '@' || c == '\\' || c == ':' || c == '.') {
        return false;
    }
    return !utils::isQualifiedName(name);
}

} // namespace trust
