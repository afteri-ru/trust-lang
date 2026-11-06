#include "sourcemap/writer.hpp"

#include "sourcemap/serialize/reader.hpp"
#include "sourcemap/store/macro_lookup.hpp"
#include "sourcemap/store/word_at.hpp"
#include "utils/error.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace trust {

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

bool SourceMapWriter::addRangeMapping(MapperRange trustRange, MapperRange outRange) {
    if (mappingSuppressed()) {
        return true; // подавлено: синтетические узлы (forward-decl на сайте импорта)
    }
    EXPECT(!trustRange.begin.isInvalid());
    EXPECT(!trustRange.end.isInvalid());
    EXPECT(!outRange.begin.isInvalid());
    EXPECT(!outRange.end.isInvalid());

    uint32_t trustKey = trustRange.begin.packed;
    uint32_t outKey = outRange.begin.packed;

    auto trustIt = m_forward.find(trustKey);
    if (trustIt != m_forward.end()) {
        FAULT("addRangeMapping: trustKey already mapped: "
              "key={:#x} new=[f{}:{}-f{}:{}] -> [f{}:{}-f{}:{}] "
              "existing=[f{}:{}-f{}:{}] -> [f{}:{}-f{}:{}]",
              trustKey, trustRange.begin.fileIdx().as_index(), trustRange.begin.offset(), trustRange.end.fileIdx().as_index(), trustRange.end.offset(),
              outRange.begin.fileIdx().as_index(), outRange.begin.offset(), outRange.end.fileIdx().as_index(), outRange.end.offset(),
              trustIt->second.from.begin.fileIdx().as_index(), trustIt->second.from.begin.offset(), trustIt->second.from.end.fileIdx().as_index(),
              trustIt->second.from.end.offset(), trustIt->second.to.begin.fileIdx().as_index(), trustIt->second.to.begin.offset(),
              trustIt->second.to.end.fileIdx().as_index(), trustIt->second.to.end.offset());
    }
    auto cppIt = m_backward.find(outKey);
    if (cppIt != m_backward.end()) {
        FAULT("addRangeMapping: outKey already mapped: "
              "key={:#x} new=[f{}:{}-f{}:{}] -> [f{}:{}-f{}:{}] "
              "existing=[f{}:{}-f{}:{}] -> [f{}:{}-f{}:{}]",
              outKey, trustRange.begin.fileIdx().as_index(), trustRange.begin.offset(), trustRange.end.fileIdx().as_index(), trustRange.end.offset(),
              outRange.begin.fileIdx().as_index(), outRange.begin.offset(), outRange.end.fileIdx().as_index(), outRange.end.offset(),
              cppIt->second.from.begin.fileIdx().as_index(), cppIt->second.from.begin.offset(), cppIt->second.from.end.fileIdx().as_index(),
              cppIt->second.from.end.offset(), cppIt->second.to.begin.fileIdx().as_index(), cppIt->second.to.begin.offset(),
              cppIt->second.to.end.fileIdx().as_index(), cppIt->second.to.end.offset());
    }

    m_forward[trustKey] = RangeMap{trustRange, outRange};
    m_backward[outKey] = RangeMap{outRange, trustRange};
    m_reader.reset();
    return true;
}

bool SourceMapWriter::addNameMapping(MapperRange trustRange, MapperRange outRange, std::string_view trustName, std::string_view outName) {
    if (mappingSuppressed()) {
        return true;
    }
    EXPECT(!trustRange.begin.isInvalid());
    EXPECT(!trustRange.end.isInvalid());
    EXPECT(!outRange.begin.isInvalid());
    EXPECT(!outRange.end.isInvalid());

    NameMap info;
    info.rangeMap = RangeMap{trustRange, outRange};
    info.fromName = trustName;
    info.toName = outName;
    m_nameMappings.push_back(info);
    m_reader.reset();
    return true;
}

bool SourceMapWriter::addMacroMapping(MapperRange useRange, MapperRange definitionRange) {
    if (mappingSuppressed()) {
        return true;
    }
    EXPECT(!useRange.begin.isInvalid());
    EXPECT(!useRange.end.isInvalid());
    EXPECT(!definitionRange.begin.isInvalid());
    EXPECT(!definitionRange.end.isInvalid());
    if (useRange.begin.isOutput()) {
        FAULT("useRange must be an input file");
    }
    if (definitionRange.begin.isOutput()) {
        FAULT("definitionRange must be an input file");
    }

    uint32_t key = useRange.begin.packed;
    if (m_macroForward.find(key) != m_macroForward.end()) {
        // Вложенный вызов переиспользует диапазон внешнего — существующая пара уже описывает раскрытие.
        return true;
    }

    m_macroForward[key] = RangeMap{useRange, definitionRange};
    m_reader.reset();
    return true;
}

// Кадры — от внутреннего к внешнему; visited/самоссылка останавливают обход. Имя — по НАЧАЛУ точки
// использования: диагностика внутри вызова может указывать на его середину, где слова-имени нет.
std::vector<ExpansionFrame> SourceMapWriter::macroExpansionChain(MapperRange useRange) const {
    std::vector<ExpansionFrame> frames;
    if (useRange.begin.isInvalid()) {
        return frames;
    }
    std::unordered_set<uint32_t> visited;
    MapperLocation current = useRange.begin;
    while (visited.insert(current.packed).second) {
        const RangeMap* entry = findEnclosingExpansion(m_macroForward, current);
        if (entry == nullptr || entry->to.begin == current) {
            break;
        }
        const std::string_view name = wordAt(source(entry->from.begin), entry->from.begin.offset());
        frames.push_back(ExpansionFrame{entry->to, std::string(name)});
        current = entry->to.begin;
    }
    return frames;
}

MapperRange SourceMapWriter::mapStart(MapperRange from, MapperFile to, bool recordRange) {
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

    m_mapStack.push_back({from, outputBegin, recordRange});
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

    // Запись создана только для позиции имени: range-mapping принадлежит другому артефакту (R40).
    if (!entry.recordRange) {
        return from;
    }

    MapperFile to = entry.outputBegin.fileIdx();
    uint32_t currentOffset = get_file(to).size() + 1;
    Location outputEnd = makeLoc(to, currentOffset);

    MapperRange outRange{entry.outputBegin, outputEnd};

    EXPECT(!entry.inputRange.begin.isInvalid());
    EXPECT(!entry.inputRange.end.isInvalid());
    EXPECT(!outRange.begin.isInvalid());
    EXPECT(!outRange.end.isInvalid());

    uint32_t trustKey = entry.inputRange.begin.packed;
    uint32_t outKey = outRange.begin.packed;

    auto trustIt = m_forward.find(trustKey);
    if (trustIt != m_forward.end()) {
        FAULT("mapStop: trustKey already mapped: "
              "key={:#x} new=[f{}:{}-f{}:{}] -> [f{}:{}-f{}:{}] "
              "existing=[f{}:{}-f{}:{}] -> [f{}:{}-f{}:{}]",
              trustKey, entry.inputRange.begin.fileIdx().as_index(), entry.inputRange.begin.offset(), entry.inputRange.end.fileIdx().as_index(),
              entry.inputRange.end.offset(), outRange.begin.fileIdx().as_index(), outRange.begin.offset(), outRange.end.fileIdx().as_index(),
              outRange.end.offset(), trustIt->second.from.begin.fileIdx().as_index(), trustIt->second.from.begin.offset(),
              trustIt->second.from.end.fileIdx().as_index(), trustIt->second.from.end.offset(), trustIt->second.to.begin.fileIdx().as_index(),
              trustIt->second.to.begin.offset(), trustIt->second.to.end.fileIdx().as_index(), trustIt->second.to.end.offset());
    }
    auto cppIt = m_backward.find(outKey);
    if (cppIt != m_backward.end()) {
        FAULT("mapStop: outKey already mapped: "
              "key={:#x} new=[f{}:{}-f{}:{}] -> [f{}:{}-f{}:{}] "
              "existing=[f{}:{}-f{}:{}] -> [f{}:{}-f{}:{}]",
              outKey, entry.inputRange.begin.fileIdx().as_index(), entry.inputRange.begin.offset(), entry.inputRange.end.fileIdx().as_index(),
              entry.inputRange.end.offset(), outRange.begin.fileIdx().as_index(), outRange.begin.offset(), outRange.end.fileIdx().as_index(),
              outRange.end.offset(), cppIt->second.from.begin.fileIdx().as_index(), cppIt->second.from.begin.offset(),
              cppIt->second.from.end.fileIdx().as_index(), cppIt->second.from.end.offset(), cppIt->second.to.begin.fileIdx().as_index(),
              cppIt->second.to.begin.offset(), cppIt->second.to.end.fileIdx().as_index(), cppIt->second.to.end.offset());
    }

    m_forward[trustKey] = RangeMap{entry.inputRange, outRange};
    m_backward[outKey] = RangeMap{outRange, entry.inputRange};
    m_reader.reset();
    return outRange;
}

} // namespace trust
