// src/sourcemap/serialize/reader_lookup.cpp
// SourceMapReader/FileEntry: location/range/name lookup queries

#include "location/location.hpp"
#include "sourcemap/mapper.hpp"
#include "utils/error.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace trust {

std::optional<ReaderRange> SourceMapReader::findRange(const std::map<uint32_t, RangeMap>& ranges, ReaderLocation loc) {
    if (ranges.empty() || loc.isInvalid()) {
        return std::nullopt;
    }

    auto it = ranges.upper_bound(loc.packed);

    if (it == ranges.begin()) {
        return std::nullopt;
    }
    --it;

    if (it->second.from.begin.fileIdx() != loc.fileIdx()) {
        return std::nullopt;
    }

    if (it->second.from.end.isInvalid()) {
        return std::nullopt;
    }
    if (loc.packed > it->second.from.end.packed) {
        return std::nullopt;
    }

    uint32_t delta = loc.packed - it->second.from.begin.packed;
    return ReaderRange{ReaderLocation::fromPacked(it->second.to.begin.packed + delta), ReaderLocation::fromPacked(it->second.to.end.packed + delta)};
}

std::optional<SourceMapReader::Range> SourceMapReader::getMapTrustToCpp(Location trustLoc) const {
    if (trustLoc.isInvalid() || trustLoc.isOutput()) {
        return std::nullopt;
    }

    return findRange(m_forward, trustLoc);
}

std::optional<SourceMapReader::Range> SourceMapReader::getMapCppToTrust(Location cppLoc) const {
    if (cppLoc.isInvalid() || !cppLoc.isOutput()) {
        return std::nullopt;
    }

    return findRange(m_backward, cppLoc);
}

std::optional<SourceMapReader::NameMap> SourceMapReader::getCppName(Location trustLoc, std::string_view trustName) const {
    // Возвращает полный NameMap (цель hover-ссылки - весь диапазон имени на
    // противоположной стороне, без сдвига по позиции курсора внутри имени).
    auto result = findNameInMappings(m_nameMappings, trustLoc.packed, [trustName](const NameMap& v) { return v.fromName == trustName; }, &RangeMap::from);
    if (result.has_value()) {
        return result;
    }

    // Если не найден - проверяем макросы (input → input)
    auto macroRange = findRange(m_macroForward, trustLoc);
    if (macroRange.has_value()) {
        NameMap result;
        result.rangeMap.from = Range{trustLoc, trustLoc};
        result.rangeMap.to = Range{trustLoc, trustLoc};
        result.fromName = std::string(trustName);
        result.toName = std::string(trustName);
        result.macroDefRange = *macroRange;
        return result;
    }

    return std::nullopt;
}

std::vector<SourceMapReader::RangeMap> SourceMapReader::getTrustFileMappings(ReaderFile trustFileIdx) const {
    std::vector<RangeMap> result;
    for (const auto& [key, entry] : m_forward) {
        (void)key;
        if (entry.from.begin.fileIdx() != trustFileIdx) {
            continue;
        }
        if (entry.from.end.isInvalid() || entry.to.end.isInvalid()) {
            continue;
        }
        result.push_back(entry);
    }
    return result;
}

std::optional<SourceMapReader::NameMap> SourceMapReader::getTrustName(Location cppLoc, std::string_view cppName) const {
    // Возвращает полный NameMap (цель hover-ссылки - весь диапазон имени на
    // противоположной стороне, без сдвига по позиции курсора внутри имени).
    return findNameInMappings(m_nameMappings, cppLoc.packed, [cppName](const NameMap& v) { return v.toName == cppName; }, &RangeMap::to);
}

std::optional<std::string> SourceMapReader::getWordAt(Location loc) const {
    if (loc.isInvalid()) {
        return std::nullopt;
    }

    std::string_view src = source(loc.fileIdx());
    if (src.empty()) {
        return std::nullopt;
    }

    // offset - 1-based, переводим в 0-based
    uint32_t off = loc.offset();
    if (off < 1 || off > src.size()) {
        return std::nullopt;
    }
    size_t pos = off - 1;

    // Проверяем, что символ под курсором - буква, цифра, _ или '@'.
    // '@' включаем, чтобы ховер над макросом (@assert/@while/print) выделял
    // полное имя макроса с префиксом (иначе на позиции '@' слово пустое).
    auto is_word_char = [](char c) -> bool { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '@'; };

    if (!is_word_char(src[pos])) {
        return std::nullopt;
    }

    // Идём влево до начала слова
    size_t start = pos;
    while (start > 0 && is_word_char(src[start - 1])) {
        --start;
    }

    // Идём вправо до конца слова
    size_t end = pos;
    while (end < src.size() && is_word_char(src[end])) {
        ++end;
    }

    return std::string(src.substr(start, end - start));
}

SourceMapReader::Location SourceMapReader::lspToLocation(ReaderFile idx, int line, int character) const {
    if (line < 0) {
        FAULT("lspToLocation: negative line ({})", line);
    }
    if (character < 0) {
        FAULT("lspToLocation: negative character ({})", character);
    }

    Location loc = loc_from_line(idx, static_cast<size_t>(line) + 1);
    uint32_t off = loc.offset() + static_cast<uint32_t>(character);
    uint32_t maxOff = idx.isOutput() ? LocationPack::MAX_OFFSET_OUTPUT : LocationPack::MAX_OFFSET_INPUT;
    if (off > maxOff) {
        FAULT("lspToLocation: offset {} exceeds max {}", off, maxOff);
    }
    return makeLoc(loc.fileIdx(), off);
}

std::string SourceMapReader::rangeToFragmentString(Range range) const {
    auto start = line_column(range.begin);
    auto end = line_column(range.end);
    return std::format("L{},{}-{},{}", start.line, start.column, end.line, end.column);
}

bool SourceMapReader::isTrustFileExt(const std::string& path) noexcept {
    return path.ends_with(".src");
}

bool SourceMapReader::isCppFileExt(const std::string& path) noexcept {
    return path.size() >= 5 && (path.rfind(".cppt") == path.size() - 5 || path.rfind(".hppt") == path.size() - 5);
}

bool SourceMapReader::isInMemoryName(std::string_view name) noexcept {
    return name.starts_with('@');
}

ReaderFile SourceMapReader::findFile(const std::string& path) const {
    ReaderFile idx = findFileIdx(path);
    if (idx.isInvalid()) {
        std::string base = std::filesystem::path(path).filename().string();
        idx = findFileIdxByBasename(base);
    }
    return idx;
}

std::optional<SourceMapReader::Range> SourceMapReader::findTrustToCpp(const std::string& trustPath, int line) const {
    ReaderFile fidx = findFile(trustPath);
    if (fidx.isInvalid() || fidx.isOutput()) {
        return std::nullopt;
    }
    Location loc = loc_from_line(fidx, line);
    return getMapTrustToCpp(loc);
}

std::optional<SourceMapReader::Range> SourceMapReader::findCppToTrust(const std::string& cppPath, int line) const {
    ReaderFile fidx = findFile(cppPath);
    if (fidx.isInvalid() || !fidx.isOutput()) {
        return std::nullopt;
    }
    Location loc = loc_from_line(fidx, line);
    return getMapCppToTrust(loc);
}

std::optional<std::pair<std::string, int>> SourceMapReader::calcCppToTrustLine(const std::string& cppPath, int cppLine) const {
    auto mapping = findCppToTrust(cppPath, cppLine);
    if (!mapping.has_value()) {
        return std::nullopt;
    }
    ReaderFile tIdx = mapping->begin.fileIdx();
    return std::make_pair(std::string(filename(tIdx)), static_cast<int>(line(mapping->begin)));
}

std::optional<SourceMapReader::RangeMap> SourceMapReader::findRangeMap(Location loc) const {
    if (loc.isInvalid()) {
        return std::nullopt;
    }

    // Если файл output - ищем по backward (cpp → trust), иначе по forward (trust → cpp)
    const auto& ranges = loc.fileIdx().isOutput() ? m_backward : m_forward;
    if (ranges.empty()) {
        return std::nullopt;
    }

    auto it = ranges.upper_bound(loc.packed);
    if (it == ranges.begin()) {
        return std::nullopt;
    }
    --it;

    if (it->second.from.begin.fileIdx() != loc.fileIdx()) {
        return std::nullopt;
    }
    if (it->second.from.end.isInvalid()) {
        return std::nullopt;
    }
    if (loc.packed > it->second.from.end.packed) {
        return std::nullopt;
    }

    return it->second;
}

std::vector<SourceMapReader::Range> SourceMapReader::findRangesByLine(ReaderFile idx, uint32_t line, std::optional<uint32_t> column) const {
    if (idx.isInvalid()) {
        FAULT("findRangesByLine: invalid ReaderFileIdx");
    }

    // Получаем Location начала строки
    Location lineStart = loc_from_line(idx, line);
    if (lineStart.isInvalid()) {
        return {};
    }

    // Если колонка задана - смещаем offset (1-based, по умолчанию 1 = начало строки)
    uint32_t col = column.value_or(1);
    // column 1-based → смещение 0-based: column - 1
    uint32_t offset = lineStart.offset() + (col - 1);
    uint32_t maxOff = idx.isOutput() ? LocationPack::MAX_OFFSET_OUTPUT : LocationPack::MAX_OFFSET_INPUT;
    if (offset > maxOff) {
        return {};
    }
    Location loc = makeLoc(lineStart.fileIdx(), offset);

    // Выбираем map: output → m_backward, input → m_forward
    const auto& ranges = idx.isOutput() ? m_backward : m_forward;

    // Собираем все подходящие диапазоны
    std::vector<Range> result;
    ReaderFile fileId = loc.fileIdx();

    for (const auto& [key, entry] : ranges) {
        (void)key;

        // Фильтр: файл должен совпадать со стороной "from"
        if (entry.from.begin.fileIdx() != fileId) {
            continue;
        }
        if (entry.from.end.isInvalid() || entry.to.end.isInvalid()) {
            continue;
        }

        // Проверка: loc внутри [from.begin, from.end]
        if (loc.packed < entry.from.begin.packed || loc.packed > entry.from.end.packed) {
            continue;
        }

        // Вычисляем сдвиг в "to" стороне
        uint32_t delta = loc.packed - entry.from.begin.packed;
        Range mapped;
        mapped.begin = Location::fromPacked(entry.to.begin.packed + delta);
        mapped.end = Location::fromPacked(entry.to.end.packed + delta);
        result.push_back(mapped);
    }

    // Сортировка от меньшего к большему (по размеру диапазона)
    std::sort(result.begin(), result.end(), [](const Range& a, const Range& b) {
        uint32_t sizeA = a.end.packed - a.begin.packed;
        uint32_t sizeB = b.end.packed - b.begin.packed;
        return sizeA < sizeB;
    });

    return result;
}

// Возвращает диапазон определения макроса по позиции в теле/вызове макроса.
// ВАЖНО: возвращаем ПОЛНЫЙ диапазон определения (m_macroForward[..].to), а не проекцию
// по позиции курсора. findRange сдвигает `to` на delta (смещение курсора внутри вызова),
// из-за чего для макросов в конце файла диапазон уходит за пределы source
// (→ getText «range out of bounds»).
std::optional<SourceMapReader::Range> SourceMapReader::getMacroDefRange(Location bodyLoc) const {
    if (m_macroForward.empty() || bodyLoc.isInvalid()) {
        return std::nullopt;
    }
    auto it = m_macroForward.upper_bound(bodyLoc.packed);
    if (it == m_macroForward.begin()) {
        return std::nullopt;
    }
    --it;
    if (it->second.from.begin.fileIdx() != bodyLoc.fileIdx()) {
        return std::nullopt;
    }
    if (it->second.from.end.isInvalid() || it->second.to.end.isInvalid()) {
        return std::nullopt;
    }
    if (bodyLoc.packed > it->second.from.end.packed) {
        return std::nullopt;
    }
    return it->second.to;
}

} // namespace trust
