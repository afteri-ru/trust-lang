#pragma once

// Единственный поиск записи раскрытия по позиции (writer и reader): запись ключуется началом
// точки использования и покрывает её диапазон.

namespace trust {

/// Запись раскрытия, содержащая позицию `loc`; nullptr, если раскрытия нет.
template <typename ExpansionMap, typename Location>
[[nodiscard]] const typename ExpansionMap::mapped_type* findEnclosingExpansion(const ExpansionMap& expansions, Location loc) {
    if (expansions.empty() || loc.isInvalid()) {
        return nullptr;
    }
    auto it = expansions.upper_bound(loc.asPacked());
    if (it == expansions.begin()) {
        return nullptr;
    }
    --it;
    const auto& entry = it->second;
    if (entry.from.begin.fileIdx() != loc.fileIdx()) {
        return nullptr;
    }
    if (entry.from.end.isInvalid() || entry.to.end.isInvalid()) {
        return nullptr;
    }
    if (loc.asPacked() > entry.from.end.asPacked()) {
        return nullptr;
    }
    return &entry;
}

} // namespace trust