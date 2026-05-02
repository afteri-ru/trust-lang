#pragma once

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "diag/location.hpp"

namespace trust {

// ── RangeMapEntry: маппинг диапазона trust → cpp ──
struct RangeMapEntry {
    SourceRange from;
    SourceRange to;
};

// ── NameRangeInfo: информация о маппинге имени (переменной/функции) с диапазоном ──
struct NameRangeInfo {
    RangeMapEntry range;
    std::string trustName;
    std::string cppName;
};

// ── SourceMapping: маппинг trust-файлов в cpp-файлы ──
//
// Хранит range-маппинги в двух индексах:
//   m_forward[trustFileIdx]  — отсортирован по trustRange.begin
//   m_backward[cppFileIdx]   — отсортирован по cppRange.begin
//
// Var-маппинги хранятся в m_varMappings.
// Для обратного поиска (cppVar → trustVar) используется m_cppToTrustVar.
class SourceMapping {
public:
    SourceMapping() = default;

    // ── Добавление маппингов ──
    // trustRange.begin должен быть монотонно возрастающим для одного trust-файла.
    // Возвращает false при нарушении монотонности.
    bool addRangeMapping(SourceRange trustRange, SourceRange cppRange);

    // addNameMapping: диапазон trust-кода → диапазон cpp-кода + переименование переменной.
    bool addNameMapping(SourceRange trustRange, SourceRange cppRange,
                        std::string_view trustName, std::string_view cppName);

    // ── Поиск диапазона по позиции ──
    std::optional<SourceRange> getMapTrustToCpp(SourceLoc trustLoc) const;
    std::optional<SourceRange> getMapCppToTrust(SourceLoc cppLoc) const;

    // ── Поиск имени по позиции ──
    std::optional<NameRangeInfo> getCppName(SourceLoc trustLoc, std::string_view trustName) const;
    std::optional<NameRangeInfo> getTrustName(SourceLoc cppLoc, std::string_view cppName) const;

    // ── Сериализация (msgpack) ──
    std::vector<unsigned char> pack() const;
    bool unpack(const unsigned char* data, size_t size);

private:
    // Индексы. Размер = maxFileIdx + 1. Пустые векторы для файлов без маппинга.
    std::vector<std::vector<RangeMapEntry>> m_forward;
    std::vector<std::vector<RangeMapEntry>> m_backward;

    // Name-маппинги (переменные, функции и другие именованные объекты)
    std::vector<NameRangeInfo> m_nameMappings;

    // Обратный индекс: cppName → trustName для быстрого поиска
    std::unordered_multimap<std::string, std::string> m_cppToTrustName;

    // Утилиты
    static void ensureSize(std::vector<std::vector<RangeMapEntry>>& vec, size_t idx);
    static bool isMonotonic(const std::vector<RangeMapEntry>& ranges, SourceLoc begin);

    // Бинарный поиск: последний entry с begin ≤ loc. Проверка loc ≤ end.
    static std::optional<SourceRange> findRange(
        const std::vector<RangeMapEntry>& ranges, SourceLoc loc);
};

} // namespace trust