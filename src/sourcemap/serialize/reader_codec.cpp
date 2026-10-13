// src/sourcemap/serialize/reader_codec.cpp
// SourceMapReader/FileEntry: msgpack encode/decode

#include "sourcemap/mapper.hpp"
#include "utils/error.hpp"
#include "utils/utils.hpp"
#include "trust/version.h"

#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace trust {

namespace {

using ReaderLocation = SourceMapReader::Location;
using ReaderRangeMap = SourceMapReader::RangeMap;
using ReaderNameMap = SourceMapReader::NameMap;

constexpr uint32_t kRangeEntryFields = 4;

bool readU32(const msgpack_object& o, uint32_t& out) {
    if (o.type != MSGPACK_OBJECT_POSITIVE_INTEGER) {
        return false;
    }
    if (o.via.u64 > std::numeric_limits<uint32_t>::max()) {
        return false; // packed-локация обязана помещаться в uint32; усечение запрещено
    }
    out = static_cast<uint32_t>(o.via.u64);
    return true;
}

void writeRangeFields(MsgpackWriter& wr, const ReaderRangeMap& rm) {
    wr.packArray(kRangeEntryFields);
    wr.packUint32(rm.from.begin.asPacked());
    wr.packUint32(rm.from.end.asPacked());
    wr.packUint32(rm.to.begin.asPacked());
    wr.packUint32(rm.to.end.asPacked());
}

std::pair<size_t, size_t> rangeEntryGroupFn(const std::pair<const uint32_t, ReaderRangeMap>& entry) {
    return {entry.second.from.begin.fileIdx().as_index(), entry.second.to.begin.fileIdx().as_index()};
}

void writeRangeEntry(MsgpackWriter& wr, const std::pair<const uint32_t, ReaderRangeMap>& entry) {
    writeRangeFields(wr, entry.second);
}

std::pair<size_t, size_t> nameEntryGroupFn(const ReaderNameMap& entry) {
    return {entry.rangeMap.from.begin.fileIdx().as_index(), entry.rangeMap.to.begin.fileIdx().as_index()};
}

void writeNameEntry(MsgpackWriter& wr, const ReaderNameMap& entry) {
    const uint32_t fields = kRangeEntryFields + 3u + (entry.macroDefRange.has_value() ? 2u : 0u);
    wr.packArray(fields);
    wr.packUint32(entry.rangeMap.from.begin.asPacked());
    wr.packUint32(entry.rangeMap.from.end.asPacked());
    wr.packUint32(entry.rangeMap.to.begin.asPacked());
    wr.packUint32(entry.rangeMap.to.end.asPacked());
    wr.packString(entry.fromName);
    wr.packString(entry.toName);
    wr.packUint8(entry.macroDefRange.has_value() ? 1u : 0u);
    if (entry.macroDefRange.has_value()) {
        wr.packUint32(entry.macroDefRange->begin.asPacked());
        wr.packUint32(entry.macroDefRange->end.asPacked());
    }
}

/// Группировка записей по (входной файл, выходной файл) и упаковка в
/// array[inCount][outCount][entries...] - единая форма для ranges/names.
template <typename Iter, typename KeyFn, typename WriteFn>
void packGroups(MsgpackWriter& wr, size_t inCount, size_t outCount, Iter begin, Iter end, KeyFn keyFn, WriteFn writeFn) {
    using Entry = typename std::iterator_traits<Iter>::value_type;
    std::vector<std::vector<std::vector<const Entry*>>> groups(inCount, std::vector<std::vector<const Entry*>>(outCount));
    for (Iter it = begin; it != end; ++it) {
        const auto [inIdx, outIdx] = keyFn(*it);
        EXPECT(inIdx < inCount && outIdx < outCount && "source map entry references a file index outside the packed group table");
        groups[inIdx][outIdx].push_back(&*it);
    }
    wr.packArray(static_cast<uint32_t>(inCount));
    for (size_t i = 0; i < inCount; ++i) {
        wr.packArray(static_cast<uint32_t>(outCount));
        for (size_t j = 0; j < outCount; ++j) {
            const auto& group = groups[i][j];
            wr.packArray(static_cast<uint32_t>(group.size()));
            for (const Entry* entry : group) {
                writeFn(wr, *entry);
            }
        }
    }
}

/// Обратная операция к packGroups: array[inCount][outCount][entries...],
/// каждая запись передаётся в createFn(entry, inIdx, outIdx). Формат проверяется строго.
template <typename CreateFn>
bool unpackGroups(msgpack_object groupsArray, uint32_t inCount, uint32_t outCount, CreateFn createFn) {
    if (groupsArray.type != MSGPACK_OBJECT_ARRAY || groupsArray.via.array.size != inCount) {
        return false;
    }
    for (uint32_t i = 0; i < inCount; ++i) {
        const msgpack_object& outGroup = groupsArray.via.array.ptr[i];
        if (outGroup.type != MSGPACK_OBJECT_ARRAY || outGroup.via.array.size != outCount) {
            return false;
        }
        for (uint32_t j = 0; j < outCount; ++j) {
            const msgpack_object& entries = outGroup.via.array.ptr[j];
            if (entries.type != MSGPACK_OBJECT_ARRAY) {
                return false;
            }
            for (uint32_t k = 0; k < entries.via.array.size; ++k) {
                const msgpack_object& entry = entries.via.array.ptr[k];
                if (entry.type != MSGPACK_OBJECT_ARRAY) {
                    return false;
                }
                if (!createFn(&entry, i, j)) {
                    return false;
                }
            }
        }
    }
    return true;
}

/// Проверяет, что запись лежит в своей группе: from - входной файл trustFileRaw, to - выходной outRaw.
bool validRangeGroup(const ReaderRangeMap& rm, uint32_t trustFileRaw, uint32_t outRaw) {
    if (rm.from.begin.isOutput() || rm.from.end.isOutput() || !rm.to.begin.isOutput() || !rm.to.end.isOutput()) {
        return false;
    }
    return rm.from.begin.fileIdx().as_index() == trustFileRaw && rm.to.begin.fileIdx().as_index() == outRaw;
}

bool createRangeEntry(const msgpack_object* entryArr, uint32_t trustFileRaw, uint32_t outRaw, std::map<uint32_t, ReaderRangeMap>& forward,
                      std::map<uint32_t, ReaderRangeMap>& backward) {
    if (entryArr == nullptr || entryArr->type != MSGPACK_OBJECT_ARRAY || entryArr->via.array.size != kRangeEntryFields) {
        return false;
    }
    uint32_t v[kRangeEntryFields];
    for (uint32_t i = 0; i < kRangeEntryFields; ++i) {
        if (!readU32(entryArr->via.array.ptr[i], v[i])) {
            return false;
        }
    }
    const ReaderRangeMap rm{ReaderRange{ReaderLocation::fromPacked(v[0]), ReaderLocation::fromPacked(v[1])},
                            ReaderRange{ReaderLocation::fromPacked(v[2]), ReaderLocation::fromPacked(v[3])}};
    if (!validRangeGroup(rm, trustFileRaw, outRaw)) {
        return false; // повреждённый source map: запись вне своей группы
    }
    forward[rm.from.begin.asPacked()] = rm;
    backward[rm.to.begin.asPacked()] = ReaderRangeMap{rm.to, rm.from};
    return true;
}

bool createNameEntry(const msgpack_object* entryArr, uint32_t trustFileRaw, uint32_t outRaw, std::vector<ReaderNameMap>& nameMappings) {
    if (entryArr == nullptr || entryArr->type != MSGPACK_OBJECT_ARRAY) {
        return false;
    }
    const uint32_t size = entryArr->via.array.size;
    if (size != kRangeEntryFields + 3u && size != kRangeEntryFields + 5u) {
        return false;
    }
    uint32_t v[kRangeEntryFields];
    for (uint32_t i = 0; i < kRangeEntryFields; ++i) {
        if (!readU32(entryArr->via.array.ptr[i], v[i])) {
            return false;
        }
    }
    const msgpack_object& fromNameObj = entryArr->via.array.ptr[kRangeEntryFields];
    const msgpack_object& toNameObj = entryArr->via.array.ptr[kRangeEntryFields + 1u];
    const msgpack_object& hasMacroObj = entryArr->via.array.ptr[kRangeEntryFields + 2u];
    if (fromNameObj.type != MSGPACK_OBJECT_STR || toNameObj.type != MSGPACK_OBJECT_STR || hasMacroObj.type != MSGPACK_OBJECT_POSITIVE_INTEGER) {
        return false;
    }

    ReaderNameMap entry;
    entry.rangeMap = ReaderRangeMap{ReaderRange{ReaderLocation::fromPacked(v[0]), ReaderLocation::fromPacked(v[1])},
                                    ReaderRange{ReaderLocation::fromPacked(v[2]), ReaderLocation::fromPacked(v[3])}};
    if (!validRangeGroup(entry.rangeMap, trustFileRaw, outRaw)) {
        return false; // повреждённый source map: запись вне своей группы
    }
    entry.fromName = std::string(fromNameObj.via.str.ptr, fromNameObj.via.str.size);
    entry.toName = std::string(toNameObj.via.str.ptr, toNameObj.via.str.size);
    if (hasMacroObj.via.u64 != 0) {
        uint32_t mb = 0;
        uint32_t me = 0;
        if (!readU32(entryArr->via.array.ptr[kRangeEntryFields + 3u], mb) || !readU32(entryArr->via.array.ptr[kRangeEntryFields + 4u], me)) {
            return false;
        }
        entry.macroDefRange = ReaderRange{ReaderLocation::fromPacked(mb), ReaderLocation::fromPacked(me)};
    }

    nameMappings.push_back(entry);
    return true;
}

bool createMacroEntry(const msgpack_object* entryArr, uint32_t bodyInIdx, uint32_t defInIdx, std::map<uint32_t, ReaderRangeMap>& macroForward) {
    if (entryArr == nullptr || entryArr->type != MSGPACK_OBJECT_ARRAY || entryArr->via.array.size != kRangeEntryFields) {
        return false;
    }
    uint32_t v[kRangeEntryFields];
    for (uint32_t i = 0; i < kRangeEntryFields; ++i) {
        if (!readU32(entryArr->via.array.ptr[i], v[i])) {
            return false;
        }
    }
    const ReaderRange from{ReaderLocation::fromPacked(v[0]), ReaderLocation::fromPacked(v[1])};
    const ReaderRange to{ReaderLocation::fromPacked(v[2]), ReaderLocation::fromPacked(v[3])};
    if (from.begin.isOutput() || from.end.isOutput() || to.begin.isOutput() || to.end.isOutput()) {
        return false; // macro-запись: обе стороны - входные файлы
    }
    if (from.begin.fileIdx().as_index() != bodyInIdx || to.begin.fileIdx().as_index() != defInIdx) {
        return false; // запись лежит не в своей группе (повреждённый source map)
    }
    macroForward[from.begin.asPacked()] = ReaderRangeMap{from, to};
    return true;
}

} // namespace

std::unique_ptr<SourceMapReader> SourceMapReader::fromMsgpack(const unsigned char* data, size_t size) {
    auto reader = std::make_unique<SourceMapReader>();

    // -- Распаковываем через zstd (checksum проверяется внутри) --
    auto decompressed = detail::zstd_decompress(data, size);
    if (decompressed.empty()) {
        return nullptr;
    }

    // -- Парсим msgpack из распакованных данных --
    MsgpackReader reader_(decompressed.data(), decompressed.size());
    if (!reader_.is_valid()) {
        return nullptr;
    }

    const msgpack_object& obj = reader_.root();
    if (obj.type != MSGPACK_OBJECT_ARRAY) {
        return nullptr;
    }

    uint32_t array_size = obj.via.array.size;
    if (array_size < kFieldRanges + 1 || array_size > kFieldCount) {
        return nullptr;
    }

    msgpack_object* fields = obj.via.array.ptr;

    // [kFieldMajor] major version
    if (fields[kFieldMajor].type != MSGPACK_OBJECT_POSITIVE_INTEGER) {
        return nullptr;
    }

    // [kFieldMinor] minor version
    if (fields[kFieldMinor].type != MSGPACK_OBJECT_POSITIVE_INTEGER) {
        return nullptr;
    }

    // [kFieldInputFiles] input filenames
    if (!readFileArray(fields[kFieldInputFiles], reader->m_inputs, true)) {
        return nullptr;
    }

    // [kFieldOutputFiles] output filenames
    if (!readFileArray(fields[kFieldOutputFiles], reader->m_outputs, true)) {
        return nullptr;
    }

    // [kFieldInputHashes] input_file_hashes
    if (!readFileArray(fields[kFieldInputHashes], reader->m_inputs, false)) {
        return nullptr;
    }

    // [kFieldOutputHashes] output_file_hashes
    if (!readFileArray(fields[kFieldOutputHashes], reader->m_outputs, false)) {
        return nullptr;
    }

    // ranges
    if (!reader->unpackRanges(fields[kFieldRanges])) {
        return nullptr;
    }

    // names - опционально
    if (array_size > kFieldNames && !reader->unpackNames(fields[kFieldNames])) {
        return nullptr;
    }

    // macros - опционально
    if (array_size > kFieldMacros && !reader->unpackMacros(fields[kFieldMacros])) {
        return nullptr;
    }

    return reader;
}

void SourceMapReader::packRanges(MsgpackWriter& wr, const std::map<uint32_t, RangeMap>& forward) const {
    packGroups(wr, m_inputs.size(), m_outputs.size(), forward.begin(), forward.end(), rangeEntryGroupFn, writeRangeEntry);
}

void SourceMapReader::packNames(MsgpackWriter& wr, const std::vector<NameMap>& nameMappings) const {
    packGroups(wr, m_inputs.size(), m_outputs.size(), nameMappings.begin(), nameMappings.end(), nameEntryGroupFn, writeNameEntry);
}

// -- packMacros: упаковка macro map (input→input) --
void SourceMapReader::packMacros(MsgpackWriter& wr) const {
    size_t inputCount = m_inputs.size();
    // Группируем по (bodyInIdx, defInIdx)
    std::vector<std::vector<std::vector<const std::pair<const uint32_t, RangeMap>*>>> groups(
        inputCount, std::vector<std::vector<const std::pair<const uint32_t, RangeMap>*>>(inputCount));

    for (const auto& entry : m_macroForward) {
        uint32_t bodyInIdx = entry.second.from.begin.fileIdx().as_index();
        uint32_t defInIdx = entry.second.to.begin.fileIdx().as_index();
        EXPECT(bodyInIdx < inputCount && defInIdx < inputCount && "macro map references a file index outside the input file table");
        groups[bodyInIdx][defInIdx].push_back(&entry);
    }

    wr.packArray(inputCount);
    for (uint32_t i = 0; i < inputCount; ++i) {
        wr.packArray(inputCount);
        for (uint32_t j = 0; j < inputCount; ++j) {
            const auto& g = groups[i][j];
            wr.packArray(g.size());
            for (const auto* entry : g) {
                writeRangeEntry(wr, *entry);
            }
        }
    }
}

bool SourceMapReader::unpackRanges(msgpack_object rangesArray) {
    return unpackGroups(rangesArray, static_cast<uint32_t>(m_inputs.size()), static_cast<uint32_t>(m_outputs.size()),
                        [this](const msgpack_object* entryArr, uint32_t trustFileRaw, uint32_t outRaw) {
                            return createRangeEntry(entryArr, trustFileRaw, outRaw, m_forward, m_backward);
                        });
}

bool SourceMapReader::unpackNames(msgpack_object namesArray) {
    return unpackGroups(namesArray, static_cast<uint32_t>(m_inputs.size()), static_cast<uint32_t>(m_outputs.size()),
                        [this](const msgpack_object* entryArr, uint32_t trustFileRaw, uint32_t outRaw) {
                            return createNameEntry(entryArr, trustFileRaw, outRaw, m_nameMappings);
                        });
}

std::vector<unsigned char> SourceMapReader::packToMsgpack() const {
    MsgpackWriter wr;
    wr.packArray(kFieldCount);
    wr.packUint8(TRUST_VERSION_MAJOR);
    wr.packUint8(TRUST_VERSION_MINOR);

    // input filenames
    wr.packArray(m_inputs.size());
    for (const auto& f : m_inputs) {
        wr.packString(f.getFilename());
    }

    // output filenames
    wr.packArray(m_outputs.size());
    for (const auto& f : m_outputs) {
        wr.packString(f.getFilename());
    }

    // input hashes
    wr.packArray(m_inputs.size());
    for (const auto& f : m_inputs) {
        wr.packUint64(f.getHash());
    }

    // output hashes
    wr.packArray(m_outputs.size());
    for (const auto& f : m_outputs) {
        wr.packUint64(f.getHash());
    }

    // ranges
    packRanges(wr, m_forward);

    // names
    packNames(wr, m_nameMappings);

    // macros
    packMacros(wr);

    // -- Сжимаем через zstd --
    msgpack_sbuffer sbuf = std::move(wr).take_sbuf();
    auto compressed = detail::zstd_compress(reinterpret_cast<const unsigned char*>(sbuf.data), sbuf.size);
    msgpack_sbuffer_destroy(&sbuf);
    if (compressed.empty()) {
        FAULT("packToMsgpack: zstd compress failed");
    }

    return compressed;
}

bool SourceMapReader::unpackMacros(msgpack_object macrosArray) {
    if (macrosArray.type != MSGPACK_OBJECT_ARRAY) {
        return false;
    }

    uint32_t inputCount = static_cast<uint32_t>(m_inputs.size());
    if (macrosArray.via.array.size != inputCount) {
        return false;
    }

    for (uint32_t bodyInIdx = 0; bodyInIdx < inputCount; ++bodyInIdx) {
        msgpack_object bodyGroup = macrosArray.via.array.ptr[bodyInIdx];
        if (bodyGroup.type != MSGPACK_OBJECT_ARRAY || bodyGroup.via.array.size != inputCount) {
            return false;
        }

        for (uint32_t defInIdx = 0; defInIdx < inputCount; ++defInIdx) {
            msgpack_object defGroup = bodyGroup.via.array.ptr[defInIdx];
            if (defGroup.type != MSGPACK_OBJECT_ARRAY) {
                return false;
            }

            if (defGroup.via.array.size == 0) {
                continue;
            }

            for (uint32_t e = 0; e < defGroup.via.array.size; ++e) {
                msgpack_object entryArr = defGroup.via.array.ptr[e];
                if (entryArr.type != MSGPACK_OBJECT_ARRAY) {
                    return false;
                }

                if (!createMacroEntry(&entryArr, bodyInIdx, defInIdx, m_macroForward)) {
                    return false;
                }
            }
        }
    }
    return true;
}

} // namespace trust
