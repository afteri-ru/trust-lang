#include "utils/cache.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string_view>
#include <vector>

namespace trust {

void SparseCache::build(std::string_view source) {
    if (!m_entries.empty())
        return;

    size_t fileSize = source.size();
    if (fileSize == 0)
        return;

    std::vector<Entry> allLines;
    allLines.reserve(source.size() / 80 + 1);
    allLines.push_back({0, 1});
    uint32_t line = 1;
    for (size_t i = 0; i < fileSize; ++i) {
        if (source[i] == '\n') {
            ++line;
            allLines.push_back({static_cast<uint32_t>(i + 1), line});
        }
    }

    uint32_t lineCount = line;
    uint32_t cacheSize = std::max(2u, static_cast<uint32_t>(std::sqrt(static_cast<double>(lineCount))));
    uint32_t actualSize = std::min(cacheSize, lineCount);
    if (actualSize >= lineCount) {
        m_entries = std::move(allLines);
        return;
    }

    m_entries.reserve(actualSize);
    m_entries.push_back(allLines[0]);
    for (uint32_t i = 1; i + 1 < actualSize; ++i) {
        uint32_t idx = static_cast<uint32_t>(static_cast<uint64_t>(i) * (lineCount - 1) / (actualSize - 1));
        m_entries.push_back(allLines[idx]);
    }
    m_entries.push_back(allLines.back());
}

const SparseCache::Entry* SparseCache::find_by_offset(uint32_t target) const {
    if (m_entries.empty())
        return nullptr;

    auto it = std::upper_bound(m_entries.begin(), m_entries.end(), target, [](uint32_t off, const Entry& e) { return off < e.offset; });
    if (it == m_entries.begin())
        return &m_entries[0];
    --it;
    return &(*it);
}

const SparseCache::Entry* SparseCache::find_by_line(uint32_t target) const {
    if (m_entries.empty())
        return nullptr;

    auto it = std::upper_bound(m_entries.begin(), m_entries.end(), target, [](uint32_t l, const Entry& e) { return l < e.line; });
    if (it == m_entries.begin())
        return &m_entries[0];
    --it;
    return &(*it);
}

} // namespace trust