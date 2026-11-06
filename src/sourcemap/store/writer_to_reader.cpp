#include "sourcemap/writer.hpp"

#include "sourcemap/serialize/reader.hpp"
#include "utils/error.hpp"

#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace trust {

SourceMapWriter::~SourceMapWriter() = default;

const SourceMapReader* SourceMapWriter::toReader() const {
    if (m_reader) {
        return m_reader.get();
    }

    auto reader = std::make_unique<SourceMapReader>();

    std::deque<FileEntry> finalizedInputs(m_inputs.begin(), m_inputs.end());
    std::deque<FileEntry> finalizedOutputs;
    std::vector<uint64_t> prependSizes(m_outputs.size(), 0);

    for (size_t i = 0; i < m_outputs.size(); ++i) {
        MapperFile idx = MapperFile::make_output(static_cast<uint32_t>(i));
        uint32_t raw = idx.raw;
        auto it = m_outputBuffers.find(raw);
        std::string fullContent;
        if (it != m_outputBuffers.end()) {
            std::string prefixStr = it->second.build();
            if (!prefixStr.empty()) {
                prependSizes[i] = static_cast<uint64_t>(prefixStr.size());
                fullContent.reserve(prefixStr.size() + m_outputs[i].size());
                fullContent.append(prefixStr);
                fullContent.append(m_outputs[i].getSource());
            } else {
                fullContent = std::string(m_outputs[i].getSource());
            }
        } else {
            fullContent = std::string(m_outputs[i].getSource());
        }
        finalizedOutputs.emplace_back(m_outputs[i].getFilename(), std::move(fullContent));
    }

    auto convertMap = [](auto& srcMap, auto& dstMap) {
        for (auto& [key, entry] : srcMap) {
            ReaderRange readerFrom{static_cast<ReaderLocation>(entry.from.begin), static_cast<ReaderLocation>(entry.from.end)};
            ReaderRange readerTo{static_cast<ReaderLocation>(entry.to.begin), static_cast<ReaderLocation>(entry.to.end)};
            dstMap[key] = SourceMapReader::RangeMap{readerFrom, readerTo};
        }
    };
    convertMap(m_forward, reader->m_forward);
    convertMap(m_macroForward, reader->m_macroForward);
    convertMap(m_backward, reader->m_backward);

    for (auto& entry : m_nameMappings) {
        ReaderRange readerFrom{static_cast<ReaderLocation>(entry.rangeMap.from.begin), static_cast<ReaderLocation>(entry.rangeMap.from.end)};
        ReaderRange readerTo{static_cast<ReaderLocation>(entry.rangeMap.to.begin), static_cast<ReaderLocation>(entry.rangeMap.to.end)};
        SourceMapReader::RangeMap readerRange{readerFrom, readerTo};
        reader->m_nameMappings.emplace_back(std::move(readerRange), std::move(entry.fromName), std::move(entry.toName));
    }

    auto offsetPrepends = [&prependSizes](auto& entries, bool cppIsFrom) {
        for (auto& [key, entry] : entries) {
            (void)key;
            auto& outRange = cppIsFrom ? entry.from : entry.to;
            auto cppIdx = outRange.begin.fileIdx();
            if (!cppIdx.isOutput()) {
                continue;
            }
            uint32_t outIdx = cppIdx.as_index();
            uint64_t prependSize = prependSizes[outIdx];
            if (prependSize == 0) {
                continue;
            }
            uint32_t beginOff = outRange.begin.offset();
            using LocType = decltype(outRange.begin);
            outRange.begin = LocType::makeLoc(cppIdx, beginOff + static_cast<uint32_t>(prependSize));
            if (!outRange.end.isInvalid()) {
                uint32_t endOff = outRange.end.offset();
                outRange.end = LocType::makeLoc(cppIdx, endOff + static_cast<uint32_t>(prependSize));
            }
        }
    };
    offsetPrepends(reader->m_backward, true);
    offsetPrepends(reader->m_forward, false);

    // Ключи m_backward - это cpp-begin (устанавливаются в mapStop как body-выровненные
    // `get_file(to).size()+1`). offsetPrepends сдвинул cpp-RANGE (value) на prependSize,
    // но ключ остался body-выровненным. Без пере-ключения findRangeMap/findRange
    // (upper_bound по ключу) не совпадёт с full-выровненным запросом (lspToLocation),
    // и обратный маппинг попадёт в «соседний» statement. Пере-ключаем на from.begin.
    {
        std::map<uint32_t, SourceMapReader::RangeMap> rekeyed;
        for (auto& [oldKey, entry] : reader->m_backward) {
            (void)oldKey;
            rekeyed[entry.from.begin.packed] = std::move(entry);
        }
        reader->m_backward = std::move(rekeyed);
    }

    for (auto& entry : reader->m_nameMappings) {
        auto& outRange = entry.rangeMap.to;
        auto cppIdx = outRange.begin.fileIdx();
        if (!cppIdx.isOutput()) {
            continue;
        }
        uint32_t outIdx = cppIdx.as_index();
        uint64_t prependSize = prependSizes[outIdx];
        if (prependSize == 0) {
            continue;
        }
        uint32_t beginOff = outRange.begin.offset();
        outRange.begin = ReaderLocation::makeLoc(cppIdx, beginOff + static_cast<uint32_t>(prependSize));
        if (!outRange.end.isInvalid()) {
            uint32_t endOff = outRange.end.offset();
            outRange.end = ReaderLocation::makeLoc(cppIdx, endOff + static_cast<uint32_t>(prependSize));
        }
    }

    reader->m_inputs = std::move(finalizedInputs);
    reader->m_outputs = std::move(finalizedOutputs);

    m_reader = std::move(reader);
    return m_reader.get();
}

} // namespace trust
