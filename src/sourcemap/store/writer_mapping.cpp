#include "sourcemap/writer.hpp"

#include "sourcemap/serialize/reader.hpp"
#include "utils/error.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace trust {

// -- Создание и валидация Location / Range --

MapperRange SourceMapWriter::makeRange(MapperLocation begin, MapperLocation end) const {
    if (begin.isInvalid() || end.isInvalid()) {
        FAULT("Location is invalid (packed == 0)");
    }
    if (begin.fileIdx() != end.fileIdx()) {
        FAULT("begin and end belong to different files");
    }
    if (end.offset() < begin.offset()) {
        FAULT("end offset {} is less than begin offset {}", end.offset(), begin.offset());
    }
    return MapperRange{begin, end};
}

bool SourceMapWriter::isValid(MapperLocation loc) const {
    if (loc.isInvalid()) {
        return false;
    }
    return loc.offset() <= get_file(loc.fileIdx()).size();
}

bool SourceMapWriter::isValid(MapperRange range) const {
    if (!isValid(range.begin) || !isValid(range.end)) {
        return false;
    }
    return range.begin.fileIdx() == range.end.fileIdx() && range.begin <= range.end;
}

std::string_view SourceMapWriter::filename(MapperLocation loc) const {
    return SourceMap::filename(loc.fileIdx());
}

std::string_view SourceMapWriter::source(MapperLocation loc) const {
    return SourceMap::source(loc.fileIdx());
}

// ══════════════════════════════════════════════════════════════
//              SourceMapWriter маппинг-методы
// ══════════════════════════════════════════════════════════════

bool SourceMapWriter::addRangeMapping(MapperRange trustRange, MapperRange cppRange) {
    if (mappingSuppressed()) {
        return true; // подавлено: синтетические узлы (forward-decl на сайте импорта)
    }
    EXPECT(!trustRange.begin.isInvalid());
    EXPECT(!trustRange.end.isInvalid());
    EXPECT(!cppRange.begin.isInvalid());
    EXPECT(!cppRange.end.isInvalid());

    uint32_t trustKey = trustRange.begin.packed;
    uint32_t cppKey = cppRange.begin.packed;

    auto trustIt = m_forward.find(trustKey);
    if (trustIt != m_forward.end()) {
        FAULT("addRangeMapping: trustKey already mapped: "
              "key={:#x} new=[f{}:{}-f{}:{}] -> [f{}:{}-f{}:{}] "
              "existing=[f{}:{}-f{}:{}] -> [f{}:{}-f{}:{}]",
              trustKey, trustRange.begin.fileIdx().as_index(), trustRange.begin.offset(), trustRange.end.fileIdx().as_index(), trustRange.end.offset(),
              cppRange.begin.fileIdx().as_index(), cppRange.begin.offset(), cppRange.end.fileIdx().as_index(), cppRange.end.offset(),
              trustIt->second.from.begin.fileIdx().as_index(), trustIt->second.from.begin.offset(), trustIt->second.from.end.fileIdx().as_index(),
              trustIt->second.from.end.offset(), trustIt->second.to.begin.fileIdx().as_index(), trustIt->second.to.begin.offset(),
              trustIt->second.to.end.fileIdx().as_index(), trustIt->second.to.end.offset());
    }
    auto cppIt = m_backward.find(cppKey);
    if (cppIt != m_backward.end()) {
        FAULT("addRangeMapping: cppKey already mapped: "
              "key={:#x} new=[f{}:{}-f{}:{}] -> [f{}:{}-f{}:{}] "
              "existing=[f{}:{}-f{}:{}] -> [f{}:{}-f{}:{}]",
              cppKey, trustRange.begin.fileIdx().as_index(), trustRange.begin.offset(), trustRange.end.fileIdx().as_index(), trustRange.end.offset(),
              cppRange.begin.fileIdx().as_index(), cppRange.begin.offset(), cppRange.end.fileIdx().as_index(), cppRange.end.offset(),
              cppIt->second.from.begin.fileIdx().as_index(), cppIt->second.from.begin.offset(), cppIt->second.from.end.fileIdx().as_index(),
              cppIt->second.from.end.offset(), cppIt->second.to.begin.fileIdx().as_index(), cppIt->second.to.begin.offset(),
              cppIt->second.to.end.fileIdx().as_index(), cppIt->second.to.end.offset());
    }

    m_forward[trustKey] = RangeMap{trustRange, cppRange};
    m_backward[cppKey] = RangeMap{cppRange, trustRange};
    m_reader.reset();
    return true;
}

bool SourceMapWriter::addNameMapping(MapperRange trustRange, MapperRange cppRange, std::string_view trustName, std::string_view cppName) {
    if (mappingSuppressed()) {
        return true;
    }
    EXPECT(!trustRange.begin.isInvalid());
    EXPECT(!trustRange.end.isInvalid());
    EXPECT(!cppRange.begin.isInvalid());
    EXPECT(!cppRange.end.isInvalid());

    NameMap info;
    info.rangeMap = RangeMap{trustRange, cppRange};
    info.fromName = trustName;
    info.toName = cppName;
    m_nameMappings.push_back(info);
    m_reader.reset();
    return true;
}

bool SourceMapWriter::addMacroMapping(MapperRange bodyRange, MapperRange defRange) {
    if (mappingSuppressed()) {
        return true;
    }
    EXPECT(!bodyRange.begin.isInvalid());
    EXPECT(!bodyRange.end.isInvalid());
    EXPECT(!defRange.begin.isInvalid());
    EXPECT(!defRange.end.isInvalid());
    if (bodyRange.begin.isOutput()) {
        FAULT("bodyRange must be an input file");
    }
    if (defRange.begin.isOutput()) {
        FAULT("defRange must be an input file");
    }

    uint32_t key = bodyRange.begin.packed;
    if (m_macroForward.find(key) != m_macroForward.end()) {
        return true;
    }

    m_macroForward[key] = RangeMap{bodyRange, defRange};
    m_reader.reset();
    return true;
}

// ══════════════════════════════════════════════════════════════
//              SourceMapWriter::mapStart / mapStop
// ══════════════════════════════════════════════════════════════

MapperRange SourceMapWriter::mapStart(MapperFile from, uint32_t from_begin, uint32_t from_end, MapperFile to) {
    if (from.isInvalid()) {
        FAULT("mapStart: 'from' FileIdx is invalid");
    }
    if (from_begin > from_end) {
        FAULT("mapStart: from_begin ({}) > from_end ({})", from_begin, from_end);
    }

    MapperLocation begin = makeLoc(from, from_begin);
    MapperLocation end = makeLoc(from, from_end);
    return mapStart(MapperRange{begin, end}, to);
}

MapperRange SourceMapWriter::mapStart(MapperRange from, MapperFile to) {
    if (from.begin.isInvalid() || from.end.isInvalid()) {
        FAULT("mapStart(Range): 'from' Range is invalid");
    }
    if (to.isInvalid() || !to.isOutput()) {
        FAULT("mapStart(Range): 'to' FileIdx must be a valid output file");
    }
    if (mappingSuppressed()) {
        return from; // подавлено: не пушим и не маппим
    }

    Location outputBegin = makeLoc(to, get_file(to).size() + 1);

    m_mapStack.push_back({from, outputBegin});
    return from;
}

const SourceMapWriter::MapStartEntry& SourceMapWriter::mapStackTop() const {
    if (m_mapStack.empty()) {
        FAULT("mapStackTop: map stack is empty");
    }
    return m_mapStack.back();
}

MapperRange SourceMapWriter::mapStop(MapperRange from) {
    if (mappingSuppressed()) {
        return from; // подавлено: mapStart не пушил - нечего закрывать
    }
    EXPECT(!m_mapStack.empty());

    MapStartEntry entry = m_mapStack.back();
    m_mapStack.pop_back();

    EXPECT(entry.inputRange == from && "top of map stack does not match 'from'");

    MapperFile to = entry.outputBegin.fileIdx();
    uint32_t currentOffset = get_file(to).size() + 1;
    Location outputEnd = makeLoc(to, currentOffset);

    MapperRange cppRange{entry.outputBegin, outputEnd};

    EXPECT(!entry.inputRange.begin.isInvalid());
    EXPECT(!entry.inputRange.end.isInvalid());
    EXPECT(!cppRange.begin.isInvalid());
    EXPECT(!cppRange.end.isInvalid());

    uint32_t trustKey = entry.inputRange.begin.packed;
    uint32_t cppKey = cppRange.begin.packed;

    auto trustIt = m_forward.find(trustKey);
    if (trustIt != m_forward.end()) {
        FAULT("mapStop: trustKey already mapped: "
              "key={:#x} new=[f{}:{}-f{}:{}] -> [f{}:{}-f{}:{}] "
              "existing=[f{}:{}-f{}:{}] -> [f{}:{}-f{}:{}]",
              trustKey, entry.inputRange.begin.fileIdx().as_index(), entry.inputRange.begin.offset(), entry.inputRange.end.fileIdx().as_index(),
              entry.inputRange.end.offset(), cppRange.begin.fileIdx().as_index(), cppRange.begin.offset(), cppRange.end.fileIdx().as_index(),
              cppRange.end.offset(), trustIt->second.from.begin.fileIdx().as_index(), trustIt->second.from.begin.offset(),
              trustIt->second.from.end.fileIdx().as_index(), trustIt->second.from.end.offset(), trustIt->second.to.begin.fileIdx().as_index(),
              trustIt->second.to.begin.offset(), trustIt->second.to.end.fileIdx().as_index(), trustIt->second.to.end.offset());
    }
    auto cppIt = m_backward.find(cppKey);
    if (cppIt != m_backward.end()) {
        FAULT("mapStop: cppKey already mapped: "
              "key={:#x} new=[f{}:{}-f{}:{}] -> [f{}:{}-f{}:{}] "
              "existing=[f{}:{}-f{}:{}] -> [f{}:{}-f{}:{}]",
              cppKey, entry.inputRange.begin.fileIdx().as_index(), entry.inputRange.begin.offset(), entry.inputRange.end.fileIdx().as_index(),
              entry.inputRange.end.offset(), cppRange.begin.fileIdx().as_index(), cppRange.begin.offset(), cppRange.end.fileIdx().as_index(),
              cppRange.end.offset(), cppIt->second.from.begin.fileIdx().as_index(), cppIt->second.from.begin.offset(),
              cppIt->second.from.end.fileIdx().as_index(), cppIt->second.from.end.offset(), cppIt->second.to.begin.fileIdx().as_index(),
              cppIt->second.to.begin.offset(), cppIt->second.to.end.fileIdx().as_index(), cppIt->second.to.end.offset());
    }

    m_forward[trustKey] = RangeMap{entry.inputRange, cppRange};
    m_backward[cppKey] = RangeMap{cppRange, entry.inputRange};
    m_reader.reset();
    return cppRange;
}

} // namespace trust
