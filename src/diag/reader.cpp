#include "diag/context.hpp"
#include "diag/location.hpp"
#include "diag/mapper.hpp"
#include "utils/error.hpp"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/MD5.h"
#include "trust/version.h"

#include <optional>
#include <zlib.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstring>
#include <cstdint>

namespace trust {

// ══════════════════════════════════════════════════════════════
//                  FileEntry implementation
// ══════════════════════════════════════════════════════════════

FileEntry::FileEntry(std::string filename, std::string source)
: m_filename(std::move(filename))
, m_source(std::move(source))
, m_hash(std::nullopt) {
}

uint64_t FileEntry::getHash() const noexcept {
    if (!m_hash.has_value())
        m_hash = llvm::MD5Hash(std::string_view(m_source));
    return *m_hash;
}

void FileEntry::setFilename(std::string f) {
    m_filename = std::move(f);
    EXPECT(m_source.empty());
    invalidateCache();
}

void FileEntry::appendSource(std::string_view text) {
    m_source.append(text);
    EXPECT(m_source.size() <= LocationPack::MAX_OFFSET);
    invalidateCache();
}

void FileEntry::setSource(std::string s) {
    m_source = std::move(s);
    EXPECT(m_source.size() <= LocationPack::MAX_OFFSET);
    invalidateCache();
}

void FileEntry::ensureCache() const {
    if (m_sparseCache.empty()) {
        m_sparseCache.build(m_source);
    }
}

// ── Вычисление строки/колонки по 1-based offset ──
FileEntry::LineColumn FileEntry::calc_column(size_t offset) const {
    ensureCache();

    EXPECT(offset > 0);

    size_t target = offset - 1; // переводим в 0-based

    // Если кеш пуст (пустой файл) — всё в первой строке
    if (m_sparseCache.empty()) {
        return LineColumn{1, offset};
    }

    // Бинарный поиск по кешу
    const SparseCache::Entry* anchor = m_sparseCache.find_by_offset(target);
    if (!static_cast<bool>(anchor)) {
        return LineColumn{1, offset};
    }

    size_t startOff = anchor->offset;
    size_t line = anchor->line;
    size_t column = 1;

    // Линейный скан от startOff до target
    size_t fileSize = m_source.size();
    for (size_t i = startOff; i < target && i < fileSize; ++i) {
        if (m_source[i] == '\n') {
            ++line;
            column = 1;
        } else {
            ++column;
        }
    }

    return {line, column};
}

// ── Вычисление offset (1-based) по номеру строки (1-based) ──
size_t FileEntry::calc_line_offset(size_t line) const {
    ensureCache();

    // Если кеш пуст — пустой файл
    if (m_sparseCache.empty()) {
        return 1;
    }

    // Бинарный поиск по кешу
    const SparseCache::Entry* anchor = m_sparseCache.find_by_line(line);
    if (!static_cast<bool>(anchor)) {
        return 1;
    }

    size_t startOff = anchor->offset;
    size_t currentLine = anchor->line;
    size_t fileSize = m_source.size();

    // Сканируем от startOff до поиска нужной строки
    for (size_t i = startOff; i < fileSize; ++i) {
        if (currentLine == line) {
            return i + 1;
        }
        if (m_source[i] == '\n') {
            ++currentLine;
        }
    }

    // Если строка не найдена — возвращаем конец файла
    return fileSize + 1;
}

// ══════════════════════════════════════════════════════════════
//          SourceMapReader
// ══════════════════════════════════════════════════════════════

// Параметр baseDir используется для резолвинга относительных filename.
// Если baseDir пустой, используется текущая рабочая директория.

bool SourceMapReader::readFilesFromDisk(std::string_view baseDir) {
    namespace fs = std::filesystem;
    fs::path basePath;
    if (!baseDir.empty()) {
        basePath = fs::absolute(fs::path(baseDir));
    } else {
        std::error_code ec;
        basePath = fs::current_path(ec);
    }

    bool allOk = true;
    for (auto& entry : m_inputs) {
        if (!readSingleFile(entry, basePath))
            allOk = false;
    }

    for (auto& entry : m_outputs) {
        if (!readSingleFile(entry, basePath))
            allOk = false;
    }

    return allOk;
}

bool SourceMapReader::verifyHash(ReaderFile idx) const {
    const auto& entry = get_file(idx);
    if (!entry.m_hash_original.has_value())
        return true;
    return entry.getHash() == *entry.m_hash_original;
}

bool SourceMapReader::readSingleFile(FileEntry& entry, const std::filesystem::path& basePath) {
    const std::string& fname = entry.getFilename();
    if (fname.empty()) {
        entry.setSource("");
        return false;
    }

    namespace fs = std::filesystem;
    fs::path fullPath(fname);
    if (!fullPath.is_absolute())
        fullPath = basePath / fullPath;

    std::ifstream ifs(fullPath, std::ios::in | std::ios::binary);
    if (!ifs) {
        entry.setSource("");
        return false;
    }

    std::ostringstream ss;
    ss << ifs.rdbuf();
    entry.setSource(ss.str());
    return true;
}

bool SourceMapReader::readFileArray(msgpack_object array, std::vector<FileEntry>& files, bool filenames) {
    if (array.type != MSGPACK_OBJECT_ARRAY)
        return false;

    if (filenames) {
        files.reserve(array.via.array.size);
        for (uint32_t i = 0; i < array.via.array.size; ++i) {
            msgpack_object elem = array.via.array.ptr[i];
            if (elem.type != MSGPACK_OBJECT_STR)
                return false;
            files.emplace_back();
            files.back().setFilename(std::string(elem.via.str.ptr, elem.via.str.size));
        }
    } else {
        for (uint32_t i = 0; i < array.via.array.size; ++i) {
            msgpack_object h = array.via.array.ptr[i];
            if (h.type != MSGPACK_OBJECT_POSITIVE_INTEGER)
                return false;
            if (i < files.size())
                files[i].setHashOriginal(h.via.u64);
        }
    }
    return true;
}

// ── Factory: fromMsgpack ──
// Формат на входе: [orig_size_LE4][zlib_compressed][checksum8]

std::unique_ptr<SourceMapReader> SourceMapReader::fromMsgpack(const unsigned char* data, size_t size) {
    auto reader = std::make_unique<SourceMapReader>();

    // Минимальный размер: 4 (orig_size) + 1 (compressed min) + 8 (checksum) = 13
    if (size < 13)
        return nullptr;

    // ── Проверка checksum от [orig_size_LE4 + compressed] ──
    size_t payload_size = size - 8;
    uint64_t stored_checksum = 0;
    for (int i = 0; i < 8; ++i)
        stored_checksum |= static_cast<uint64_t>(data[payload_size + i]) << (i * 8);

    auto checksumArr = llvm::ArrayRef<uint8_t>(data, payload_size);
    auto checksumRes = llvm::MD5::hash(checksumArr);
    uint64_t computed_checksum = 0;
    for (int i = 0; i < 8; ++i)
        computed_checksum |= static_cast<uint64_t>(checksumRes[i]) << (i * 8);
    if (computed_checksum != stored_checksum)
        return nullptr;

    // ── Читаем original_size (4 байта LE) ──
    uLong orig_size = 0;
    for (int i = 0; i < 4; ++i)
        orig_size |= static_cast<uLong>(data[i]) << (i * 8);
    if (orig_size == 0 || orig_size > 1024 * 1024 * 1024) // reasonable limit: 1GB
        return nullptr;

    // ── Распаковываем через zlib (inflate) ──
    uLong compressed_size = static_cast<uLong>(payload_size - 4);
    const auto* compressed_data = data + 4;

    auto decompressed = std::make_unique<unsigned char[]>(orig_size);
    uLong decompressed_size = orig_size;
    int zret = uncompress(decompressed.get(), &decompressed_size, compressed_data, compressed_size);
    if (zret != Z_OK)
        return nullptr;
    if (decompressed_size != orig_size)
        return nullptr;

    // ── Парсим msgpack из распакованных данных ──
    MsgpackReader reader_(decompressed.get(), decompressed_size);
    if (!reader_.is_valid())
        return nullptr;

    const msgpack_object& obj = reader_.root();
    if (obj.type != MSGPACK_OBJECT_ARRAY)
        return nullptr;

    uint32_t array_size = obj.via.array.size;
    if (array_size < kFieldRanges + 1 || array_size > kFieldCount)
        return nullptr;

    msgpack_object* fields = obj.via.array.ptr;

    // [kFieldMajor] major version
    if (fields[kFieldMajor].type != MSGPACK_OBJECT_POSITIVE_INTEGER)
        return nullptr;

    // [kFieldMinor] minor version
    if (fields[kFieldMinor].type != MSGPACK_OBJECT_POSITIVE_INTEGER)
        return nullptr;

    // [kFieldInputFiles] input filenames
    if (!readFileArray(fields[kFieldInputFiles], reader->m_inputs, true))
        return nullptr;

    // [kFieldOutputFiles] output filenames
    if (!readFileArray(fields[kFieldOutputFiles], reader->m_outputs, true))
        return nullptr;

    // [kFieldInputHashes] input_file_hashes
    if (!readFileArray(fields[kFieldInputHashes], reader->m_inputs, false))
        return nullptr;

    // [kFieldOutputHashes] output_file_hashes
    if (!readFileArray(fields[kFieldOutputHashes], reader->m_outputs, false))
        return nullptr;

    // ranges
    if (!reader->unpackRanges(fields[kFieldRanges]))
        return nullptr;

    // names — опционально
    if (array_size > kFieldNames)
        reader->unpackNames(fields[kFieldNames]);

    // macros — опционально
    if (array_size > kFieldMacros)
        reader->unpackMacros(fields[kFieldMacros]);

    return reader;
}

// ── Поиск ──

std::optional<ReaderRange> SourceMapReader::findRange(const std::map<uint32_t, RangeMap>& ranges, ReaderLocation loc) {
    if (ranges.empty() || !loc.isValid())
        return std::nullopt;

    auto it = ranges.upper_bound(loc.packed);

    if (it == ranges.begin())
        return std::nullopt;
    --it;

    if (it->second.from.begin.fileIdx() != loc.fileIdx())
        return std::nullopt;

    if (!it->second.from.end.isValid())
        return std::nullopt;
    if (loc.packed > it->second.from.end.packed)
        return std::nullopt;

    uint32_t delta = loc.packed - it->second.from.begin.packed;
    return ReaderRange{ReaderLocation::fromPacked(it->second.to.begin.packed + delta), ReaderLocation::fromPacked(it->second.to.end.packed + delta)};
}

std::optional<SourceMapReader::Range> SourceMapReader::getMapTrustToCpp(Location trustLoc) const {
    if (!trustLoc.isValid() || trustLoc.isOutput())
        return std::nullopt;

    return findRange(m_forward, trustLoc);
}

std::optional<SourceMapReader::Range> SourceMapReader::getMapCppToTrust(Location cppLoc) const {
    if (!cppLoc.isValid() || !cppLoc.isOutput())
        return std::nullopt;

    return findRange(m_backward, cppLoc);
}

std::optional<SourceMapReader::NameMap> SourceMapReader::getCppName(Location trustLoc, std::string_view trustName) const {
    auto result = findNameInMappings(
        m_nameMappings, trustLoc.packed, [trustName](const NameMap& v) { return v.fromName == trustName; },
        [](const NameMap& v) -> const auto& { return v.rangeMap.from; },
        [](NameMap& result, int offset) {
            result.rangeMap.to.begin = Location{Location::fromPacked(result.rangeMap.to.begin.packed + offset)};
            result.rangeMap.to.end = Location{Location::fromPacked(result.rangeMap.to.end.packed + offset)};
        });
    if (result.has_value())
        return result;

    // Если не найден — проверяем макросы (input → input)
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
        if (entry.from.begin.fileIdx() != trustFileIdx)
            continue;
        if (!entry.from.end.isValid() || !entry.to.end.isValid())
            continue;
        result.push_back(entry);
    }
    return result;
}

std::optional<SourceMapReader::NameMap> SourceMapReader::getTrustName(Location cppLoc, std::string_view cppName) const {
    return findNameInMappings(
        m_nameMappings, cppLoc.packed, [cppName](const NameMap& v) { return v.toName == cppName; },
        [](const NameMap& v) -> const auto& { return v.rangeMap.to; },
        [](NameMap& result, int offset) {
            result.rangeMap.from.begin = Location{Location::fromPacked(result.rangeMap.from.begin.packed + offset)};
            result.rangeMap.from.end = Location{Location::fromPacked(result.rangeMap.from.end.packed + offset)};
        });
}

// ══════════════════════════════════════════════════════════════
//          Шаблонные helper-функции для pack/unpack
// ══════════════════════════════════════════════════════════════

namespace {

// ── packGroups: шаблон для packRanges/packNames ──
// GroupFn(const Entry&) → (inIdx, outIdx)
// WriteFn(MsgpackWriter&, const Entry&) → void (запись entry)
template <typename Iter, typename GroupFn, typename WriteFn>
void packGroups(MsgpackWriter& wr, size_t inputCount, size_t outputCount, Iter begin, Iter end, GroupFn&& groupFn, WriteFn&& writeFn) {
    // 1-й проход: grouping — сохраняем указатели на элементы по группам
    using Entry = std::decay_t<decltype(*begin)>;
    std::vector<std::vector<std::vector<const Entry*>>> groups(inputCount, std::vector<std::vector<const Entry*>>(outputCount));

    for (auto it = begin; it != end; ++it) {
        auto [inIdx, outIdx] = groupFn(*it);
        if (inIdx < inputCount && outIdx < outputCount)
            groups[inIdx][outIdx].push_back(&*it);
    }

    // 2-й проход: sequential write
    wr.packArray(inputCount);
    for (uint32_t inIdx = 0; inIdx < inputCount; ++inIdx) {
        wr.packArray(outputCount);

        for (uint32_t outIdx = 0; outIdx < outputCount; ++outIdx) {
            const auto& g = groups[inIdx][outIdx];
            wr.packArray(g.size());
            for (const auto* entry : g)
                writeFn(wr, *entry);
        }
    }
}

// ── unpackGroups: шаблон для unpackRanges/unpackNames ──
// CreateEntryFn(msgpack_object *fields, uint32_t trustFileRaw,
//               uint32_t outRaw) → bool (false = ошибка)
template <typename CreateEntryFn>
bool unpackGroups(msgpack_object array, uint32_t inputCount, uint32_t outputCount, CreateEntryFn&& createEntry) {
    if (array.type != MSGPACK_OBJECT_ARRAY)
        return false;

    for (uint32_t inIdx = 0; inIdx < array.via.array.size && inIdx < inputCount; ++inIdx) {
        msgpack_object inputGroup = array.via.array.ptr[inIdx];
        if (inputGroup.type != MSGPACK_OBJECT_ARRAY)
            return false;

        uint32_t trustFileRaw = inIdx + 1u;

        for (uint32_t outIdx = 0; outIdx < inputGroup.via.array.size && outIdx < outputCount; ++outIdx) {
            msgpack_object outputGroup = inputGroup.via.array.ptr[outIdx];
            if (outputGroup.type != MSGPACK_OBJECT_ARRAY)
                return false;

            if (outputGroup.via.array.size == 0)
                continue;

            uint32_t outRaw = (outIdx + 1u) | (1u << LocationPack::FILE_BITS);

            for (uint32_t e = 0; e < outputGroup.via.array.size; ++e) {
                msgpack_object entryArr = outputGroup.via.array.ptr[e];
                if (entryArr.type != MSGPACK_OBJECT_ARRAY)
                    return false;

                if (!createEntry(&entryArr, trustFileRaw, outRaw))
                    return false;
            }
        }
    }
    return true;
}

// ── Извлечение пары (inIdx, outIdx) из map-записи (std::pair) ──
auto rangeEntryGroupFn = [](const std::pair<const uint32_t, SourceMapReader::RangeMap>& p) {
    const auto& entry = p.second;
    uint32_t inIdx = entry.from.begin.fileIdx().as_index();
    uint32_t outIdx = entry.to.begin.fileIdx().as_index();
    return std::pair{inIdx, outIdx};
};

// ── Извлечение пары (inIdx, outIdx) из NameMap ──
auto nameEntryGroupFn = [](const SourceMapReader::NameMap& v) {
    uint32_t inIdx = v.rangeMap.from.begin.fileIdx().as_index();
    uint32_t outIdx = v.rangeMap.to.begin.fileIdx().as_index();
    return std::pair{inIdx, outIdx};
};

// ── Запись range entry (из map-записи) ──
void writeRangeEntry(MsgpackWriter& wr, const std::pair<const uint32_t, SourceMapReader::RangeMap>& p) {
    const auto& entry = p.second;
    wr.packArray(kRangeGroupFieldCount);
    wr.packUint32(entry.from.begin.offset());
    wr.packUint32(entry.from.end.offset() - entry.from.begin.offset());
    wr.packUint32(entry.to.begin.offset());
    wr.packUint32(entry.to.end.offset() - entry.to.begin.offset());
}

// ── Запись name entry ──
void writeNameEntry(MsgpackWriter& wr, const SourceMapReader::NameMap& v) {
    wr.packArray(kNameGroupFieldCount);
    wr.packUint32(v.rangeMap.from.begin.offset());
    wr.packUint32(v.rangeMap.from.end.offset() - v.rangeMap.from.begin.offset());
    wr.packUint32(v.rangeMap.to.begin.offset());
    wr.packUint32(v.rangeMap.to.end.offset() - v.rangeMap.to.begin.offset());
    wr.packString(v.fromName);
    wr.packString(v.toName);
}

// ── Запись macro entry (из map-записи m_macroForward) ──
void writeMacroEntry(MsgpackWriter& wr, const std::pair<const uint32_t, SourceMapReader::RangeMap>& p) {
    const auto& entry = p.second;
    wr.packArray(kMacroGroupFieldCount);
    // from = тело макроса, to = определение макроса (input → input)
    wr.packUint32(entry.from.begin.offset());
    wr.packUint32(entry.from.end.offset() - entry.from.begin.offset());
    wr.packUint32(entry.to.begin.offset());
    wr.packUint32(entry.to.end.offset() - entry.to.begin.offset());
}

// ── Создание RangeMap из полей ──
bool createRangeEntry(const msgpack_object* entryArr, uint32_t trustFileRaw, uint32_t outRaw, std::map<uint32_t, SourceMapReader::RangeMap>& forward,
                      std::map<uint32_t, SourceMapReader::RangeMap>& backward) {
    if (entryArr->type != MSGPACK_OBJECT_ARRAY || entryArr->via.array.size < kRangeGroupFieldCount)
        return false;

    msgpack_object* f = entryArr->via.array.ptr;
    if (f[kRangeGroupFieldBeginOff].type != MSGPACK_OBJECT_POSITIVE_INTEGER || f[kRangeGroupFieldDelta].type != MSGPACK_OBJECT_POSITIVE_INTEGER ||
        f[kRangeGroupFieldCppBeginOff].type != MSGPACK_OBJECT_POSITIVE_INTEGER || f[kRangeGroupFieldCppDelta].type != MSGPACK_OBJECT_POSITIVE_INTEGER)
        return false;

    SourceMapReader::RangeMap entry;

    ReaderFile trustFileIdx = ReaderFile::fromRaw(trustFileRaw);
    ReaderFile outFileIdx = ReaderFile::fromRaw(outRaw);

    entry.from.begin = ReaderLocation::makeLoc(trustFileIdx, f[kRangeGroupFieldBeginOff].via.u64);
    entry.from.end = ReaderLocation::makeLoc(trustFileIdx, f[kRangeGroupFieldBeginOff].via.u64 + f[kRangeGroupFieldDelta].via.u64);
    entry.to.begin = ReaderLocation::makeLoc(outFileIdx, f[kRangeGroupFieldCppBeginOff].via.u64);
    entry.to.end = ReaderLocation::makeLoc(outFileIdx, f[kRangeGroupFieldCppBeginOff].via.u64 + f[kRangeGroupFieldCppDelta].via.u64);

    uint32_t trustKey = entry.from.begin.asPacked();
    uint32_t cppKey = entry.to.begin.asPacked();

    auto [itFwd, insertedFwd] = forward.emplace(trustKey, entry);
    if (!insertedFwd)
        return false;

    SourceMapReader::RangeMap bwdEntry{entry.to, entry.from};
    auto [itBwd, insertedBwd] = backward.emplace(cppKey, std::move(bwdEntry));
    if (!insertedBwd) {
        forward.erase(itFwd);
        return false;
    }
    return true;
}

// ── Создание Macro entry (input→input, без OUTPUT_BIT) ──
bool createMacroEntry(const msgpack_object* entryArr, uint32_t bodyFileRaw, uint32_t defFileRaw, std::map<uint32_t, SourceMapReader::RangeMap>& macroForward) {
    if (entryArr->type != MSGPACK_OBJECT_ARRAY || entryArr->via.array.size < kMacroGroupFieldCount)
        return false;

    msgpack_object* f = entryArr->via.array.ptr;
    if (f[kMacroGroupFieldBodyBeginOff].type != MSGPACK_OBJECT_POSITIVE_INTEGER || f[kMacroGroupFieldBodyDelta].type != MSGPACK_OBJECT_POSITIVE_INTEGER ||
        f[kMacroGroupFieldDefBeginOff].type != MSGPACK_OBJECT_POSITIVE_INTEGER || f[kMacroGroupFieldDefDelta].type != MSGPACK_OBJECT_POSITIVE_INTEGER)
        return false;

    SourceMapReader::RangeMap entry;
    ReaderFile bodyFile = ReaderFile::fromRaw(bodyFileRaw);
    ReaderFile defFile = ReaderFile::fromRaw(defFileRaw);

    // from = тело макроса, to = определение макроса
    entry.from.begin = ReaderLocation::makeLoc(bodyFile, f[kMacroGroupFieldBodyBeginOff].via.u64);
    entry.from.end = ReaderLocation::makeLoc(bodyFile, f[kMacroGroupFieldBodyBeginOff].via.u64 + f[kMacroGroupFieldBodyDelta].via.u64);
    entry.to.begin = ReaderLocation::makeLoc(defFile, f[kMacroGroupFieldDefBeginOff].via.u64);
    entry.to.end = ReaderLocation::makeLoc(defFile, f[kMacroGroupFieldDefBeginOff].via.u64 + f[kMacroGroupFieldDefDelta].via.u64);

    uint32_t key = entry.from.begin.asPacked();
    auto [it, inserted] = macroForward.emplace(key, entry);
    return inserted;
}

// ── Создание NameMap из полей ──
bool createNameEntry(const msgpack_object* entryArr, uint32_t trustFileRaw, uint32_t outRaw, std::vector<SourceMapReader::NameMap>& nameMappings,
                     std::unordered_multimap<std::string, std::string>& cppToTrustName) {
    if (entryArr->type != MSGPACK_OBJECT_ARRAY || entryArr->via.array.size < kNameGroupFieldCount)
        return false;

    msgpack_object* f = entryArr->via.array.ptr;
    if (f[kNameGroupFieldBeginOff].type != MSGPACK_OBJECT_POSITIVE_INTEGER || f[kNameGroupFieldDelta].type != MSGPACK_OBJECT_POSITIVE_INTEGER ||
        f[kNameGroupFieldCppBeginOff].type != MSGPACK_OBJECT_POSITIVE_INTEGER || f[kNameGroupFieldCppDelta].type != MSGPACK_OBJECT_POSITIVE_INTEGER ||
        f[kNameGroupFieldTrustName].type != MSGPACK_OBJECT_STR || f[kNameGroupFieldCppName].type != MSGPACK_OBJECT_STR)
        return false;

    SourceMapReader::NameMap info;

    ReaderFile trustFileIdx = ReaderFile::fromRaw(trustFileRaw);
    ReaderFile outFileIdx = ReaderFile::fromRaw(outRaw);

    info.rangeMap.from.begin = ReaderLocation::makeLoc(trustFileIdx, f[kNameGroupFieldBeginOff].via.u64);
    info.rangeMap.from.end = ReaderLocation::makeLoc(trustFileIdx, f[kNameGroupFieldBeginOff].via.u64 + f[kNameGroupFieldDelta].via.u64);
    info.rangeMap.to.begin = ReaderLocation::makeLoc(outFileIdx, f[kNameGroupFieldCppBeginOff].via.u64);
    info.rangeMap.to.end = ReaderLocation::makeLoc(outFileIdx, f[kNameGroupFieldCppBeginOff].via.u64 + f[kNameGroupFieldCppDelta].via.u64);
    info.fromName.assign(f[kNameGroupFieldTrustName].via.str.ptr, f[kNameGroupFieldTrustName].via.str.size);
    info.toName.assign(f[kNameGroupFieldCppName].via.str.ptr, f[kNameGroupFieldCppName].via.str.size);

    nameMappings.push_back(info);
    cppToTrustName.emplace(info.toName, info.fromName);
    return true;
}

} // anonymous namespace

// ══════════════════════════════════════════════════════════════
//          packRanges / packNames  (обёртки)
// ══════════════════════════════════════════════════════════════
//
// Группировка маппингов по парам (input, output).
// Формат: [[[beginOff, delta, cppBeginOff, cppDelta,
//            trustName, cppName], ...], ...]
//
// trustFileIdx/cppFileIdx не хранятся — вычисляются из позиции
// во внешнем/внутреннем массиве.
//
// delta = endOffset - beginOffset (0 для точечной позиции).
//
// Для range: entry = [beginOff, delta, cppBeginOff, cppDelta].
// Для name: entry = [beginOff, delta, cppBeginOff, cppDelta,
//                                          trustName, cppName].
//
// Группировка маппингов по парам (input, output).
// Формат: [[[beginOff, delta, cppBeginOff, cppDelta,
//            trustName, cppName], ...], ...]
//
// trustFileIdx/cppFileIdx не хранятся — вычисляются из позиции
// во внешнем/внутреннем массиве.
//
// delta = endOffset - beginOffset (0 для точечной позиции).
//
// Для range: entry = [beginOff, delta, cppBeginOff, cppDelta].
// Для name: entry = [beginOff, delta, cppBeginOff, cppDelta,
//                                          trustName, cppName].

void SourceMapReader::packRanges(MsgpackWriter& wr, const std::map<uint32_t, RangeMap>& forward) const {
    packGroups(wr, m_inputs.size(), m_outputs.size(), forward.begin(), forward.end(), rangeEntryGroupFn, writeRangeEntry);
}

void SourceMapReader::packNames(MsgpackWriter& wr, const std::vector<NameMap>& nameMappings) const {
    packGroups(wr, m_inputs.size(), m_outputs.size(), nameMappings.begin(), nameMappings.end(), nameEntryGroupFn, writeNameEntry);
}

// ── packMacros: упаковка macro map (input→input) ──
void SourceMapReader::packMacros(MsgpackWriter& wr) const {
    size_t inputCount = m_inputs.size();
    // Группируем по (bodyInIdx, defInIdx)
    std::vector<std::vector<std::vector<const std::pair<const uint32_t, RangeMap>*>>> groups(
        inputCount, std::vector<std::vector<const std::pair<const uint32_t, RangeMap>*>>(inputCount));

    for (const auto& entry : m_macroForward) {
        uint32_t bodyInIdx = entry.second.from.begin.fileIdx().as_index();
        uint32_t defInIdx = entry.second.to.begin.fileIdx().as_index();
        if (bodyInIdx < inputCount && defInIdx < inputCount)
            groups[bodyInIdx][defInIdx].push_back(&entry);
    }

    wr.packArray(inputCount);
    for (uint32_t i = 0; i < inputCount; ++i) {
        wr.packArray(inputCount);
        for (uint32_t j = 0; j < inputCount; ++j) {
            const auto& g = groups[i][j];
            wr.packArray(g.size());
            for (const auto* entry : g)
                writeMacroEntry(wr, *entry);
        }
    }
}

// ══════════════════════════════════════════════════════════════
//          Десериализация (msgpack)
// ══════════════════════════════════════════════════════════════
//
// Обратная операция к packRanges/packNames.
// Формат — симметричный: [[[entry, ...], ...], ...]
// outIdx = outGroupIdx (так как все группы присутствуют, включая пустые).

bool SourceMapReader::unpackRanges(msgpack_object rangesArray) {
    return unpackGroups(rangesArray, static_cast<uint32_t>(m_inputs.size()), static_cast<uint32_t>(m_outputs.size()),
                        [this](const msgpack_object* entryArr, uint32_t trustFileRaw, uint32_t outRaw) {
                            return createRangeEntry(entryArr, trustFileRaw, outRaw, m_forward, m_backward);
                        });
}

bool SourceMapReader::unpackNames(msgpack_object namesArray) {
    return unpackGroups(namesArray, static_cast<uint32_t>(m_inputs.size()), static_cast<uint32_t>(m_outputs.size()),
                        [this](const msgpack_object* entryArr, uint32_t trustFileRaw, uint32_t outRaw) {
                            return createNameEntry(entryArr, trustFileRaw, outRaw, m_nameMappings, m_nameBackward);
                        });
}

// ══════════════════════════════════════════════════════════════
//          Полная сериализация — packToMsgpack
// ══════════════════════════════════════════════════════════════
// Формат на выходе: [orig_size_LE4][zlib_compressed][checksum8]

std::vector<unsigned char> SourceMapReader::packToMsgpack() const {
    MsgpackWriter wr;
    wr.packArray(kFieldCount);
    wr.packUint8(TRUST_VERSION_MAJOR);
    wr.packUint8(TRUST_VERSION_MINOR);

    // input filenames
    wr.packArray(m_inputs.size());
    for (const auto& f : m_inputs)
        wr.packString(f.getFilename());

    // output filenames
    wr.packArray(m_outputs.size());
    for (const auto& f : m_outputs)
        wr.packString(f.getFilename());

    // input hashes
    wr.packArray(m_inputs.size());
    for (const auto& f : m_inputs)
        wr.packUint64(f.getHash());

    // output hashes
    wr.packArray(m_outputs.size());
    for (const auto& f : m_outputs)
        wr.packUint64(f.getHash());

    // ranges
    packRanges(wr, m_forward);

    // names
    packNames(wr, m_nameMappings);

    // macros
    packMacros(wr);

    // ── Получаем бинарный msgpack-буфер ──
    msgpack_sbuffer sbuf = std::move(wr).take_sbuf();
    uLong orig_size = static_cast<uLong>(sbuf.size);
    const auto* raw = reinterpret_cast<const unsigned char*>(sbuf.data);

    // ── Сжимаем через zlib (deflate) ──
    uLong compressed_capacity = compressBound(orig_size);
    auto compressed = std::make_unique<unsigned char[]>(compressed_capacity);
    uLong compressed_size = compressed_capacity;
    if (compress2(compressed.get(), &compressed_size, raw, orig_size, Z_DEFAULT_COMPRESSION) != Z_OK) {
        msgpack_sbuffer_destroy(&sbuf);
        FAULT("packToMsgpack: zlib compress failed");
    }

    msgpack_sbuffer_destroy(&sbuf);

    // ── Формируем результат: [orig_size_LE4][compressed][checksum8] ──
    std::vector<unsigned char> result;
    result.reserve(4 + compressed_size + 8);

    // orig_size (4 байта LE)
    for (int i = 0; i < 4; ++i)
        result.push_back(static_cast<unsigned char>((orig_size >> (i * 8)) & 0xFF));

    // compressed data
    result.insert(result.end(), compressed.get(), compressed.get() + compressed_size);

    // checksum от [orig_size_LE4 + compressed]
    auto checksumArr = llvm::ArrayRef<uint8_t>(result.data(), result.size());
    auto checksumRes = llvm::MD5::hash(checksumArr);
    uint64_t checksum = 0;
    for (int i = 0; i < 8; ++i)
        checksum |= static_cast<uint64_t>(checksumRes[i]) << (i * 8);
    for (int i = 0; i < 8; ++i)
        result.push_back(static_cast<unsigned char>((checksum >> (i * 8)) & 0xFF));

    return result;
}

// ══════════════════════════════════════════════════════════════
//          SourceMapReader::getWordAt
// ══════════════════════════════════════════════════════════════

std::optional<std::string> SourceMapReader::getWordAt(Location loc) const {
    if (!loc.isValid())
        return std::nullopt;

    std::string_view src = source(loc.fileIdx());
    if (src.empty())
        return std::nullopt;

    // offset — 1-based, переводим в 0-based
    uint32_t off = loc.offset();
    if (off < 1 || off > src.size())
        return std::nullopt;
    size_t pos = off - 1;

    // Проверяем, что символ под курсором — буква, цифра или _
    auto is_word_char = [](char c) -> bool { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'; };

    if (!is_word_char(src[pos]))
        return std::nullopt;

    // Идём влево до начала слова
    size_t start = pos;
    while (start > 0 && is_word_char(src[start - 1]))
        --start;

    // Идём вправо до конца слова
    size_t end = pos;
    while (end < src.size() && is_word_char(src[end]))
        ++end;

    return std::string(src.substr(start, end - start));
}

// ══════════════════════════════════════════════════════════════
//          SourceMapReader::lspToLocation
// ══════════════════════════════════════════════════════════════

SourceMapReader::Location SourceMapReader::lspToLocation(ReaderFile idx, int line, int character) const {
    if (line < 0)
        FAULT("lspToLocation: negative line ({})", line);
    if (character < 0)
        FAULT("lspToLocation: negative character ({})", character);

    Location loc = loc_from_line(idx, static_cast<size_t>(line) + 1);
    uint32_t off = loc.offset() + static_cast<uint32_t>(character);
    static constexpr uint32_t MAX_OFF = LocationPack::MAX_OFFSET;
    if (off > MAX_OFF)
        FAULT("lspToLocation: offset {} exceeds MAX_OFFSET {}", off, MAX_OFF);
    return makeLoc(loc.fileIdx(), off);
}

// ══════════════════════════════════════════════════════════════
//          SourceMapReader::rangeToFragmentString
// ══════════════════════════════════════════════════════════════

std::string SourceMapReader::rangeToFragmentString(Range range) const {
    auto start = line_column(range.begin);
    auto end = line_column(range.end);
    return std::format("L{},{}-{},{}", start.line, start.column, end.line, end.column);
}

// ══════════════════════════════════════════════════════════════
//          SourceMapReader: high-level convenience методы
// ══════════════════════════════════════════════════════════════

bool SourceMapReader::isTrustFileExt(const std::string& path) noexcept {
    return path.ends_with(".src") || path.ends_with(".trust");
}

bool SourceMapReader::isCppFileExt(const std::string& path) noexcept {
    return path.size() >= 4 && (path.rfind(".cpp") == path.size() - 4 || path.rfind(".hpp") == path.size() - 4 || path.rfind(".h") == path.size() - 2);
}

ReaderFile SourceMapReader::findFile(const std::string& path) const {
    ReaderFile idx = findFileIdx(path);
    if (!idx.isValid()) {
        std::string base = std::filesystem::path(path).filename().string();
        idx = findFileIdxByBasename(base);
    }
    return idx;
}

std::optional<SourceMapReader::Range> SourceMapReader::findTrustToCpp(const std::string& trustPath, int line) const {
    ReaderFile fidx = findFile(trustPath);
    if (!fidx.isValid() || fidx.isOutput())
        return std::nullopt;
    Location loc = loc_from_line(fidx, line);
    return getMapTrustToCpp(loc);
}

std::optional<SourceMapReader::Range> SourceMapReader::findCppToTrust(const std::string& cppPath, int line) const {
    ReaderFile fidx = findFile(cppPath);
    if (!fidx.isValid() || !fidx.isOutput())
        return std::nullopt;
    Location loc = loc_from_line(fidx, line);
    return getMapCppToTrust(loc);
}

std::optional<std::pair<std::string, int>> SourceMapReader::calcCppToTrustLine(const std::string& cppPath, int cppLine) const {
    auto mapping = findCppToTrust(cppPath, cppLine);
    if (!mapping.has_value())
        return std::nullopt;
    ReaderFile tIdx = mapping->begin.fileIdx();
    return std::make_pair(std::string(filename(tIdx)), static_cast<int>(line(mapping->begin)));
}

// ══════════════════════════════════════════════════════════════
//          SourceMapReader::findRangeMap
// ══════════════════════════════════════════════════════════════

std::optional<SourceMapReader::RangeMap> SourceMapReader::findRangeMap(Location loc) const {
    if (!loc.isValid())
        return std::nullopt;

    // Если файл output — ищем по backward (cpp → trust), иначе по forward (trust → cpp)
    const auto& ranges = loc.fileIdx().isOutput() ? m_backward : m_forward;
    if (ranges.empty())
        return std::nullopt;

    auto it = ranges.upper_bound(loc.packed);
    if (it == ranges.begin())
        return std::nullopt;
    --it;

    if (it->second.from.begin.fileIdx() != loc.fileIdx())
        return std::nullopt;
    if (!it->second.from.end.isValid())
        return std::nullopt;
    if (loc.packed > it->second.from.end.packed)
        return std::nullopt;

    return it->second;
}

// ══════════════════════════════════════════════════════════════
//          SourceMapReader::findRangesByLine
// ══════════════════════════════════════════════════════════════

std::vector<SourceMapReader::Range> SourceMapReader::findRangesByLine(ReaderFile idx, uint32_t line, std::optional<uint32_t> column) const {
    if (!idx.isValid())
        FAULT("findRangesByLine: invalid ReaderFileIdx");

    // Получаем Location начала строки
    Location lineStart = loc_from_line(idx, line);
    if (!lineStart.isValid())
        return {};

    // Если колонка задана — смещаем offset (1-based, по умолчанию 1 = начало строки)
    uint32_t col = column.value_or(1);
    // column 1-based → смещение 0-based: column - 1
    uint32_t offset = lineStart + (col - 1);
    if (offset > LocationPack::MAX_OFFSET)
        return {};
    Location loc = Location::fromPacked((lineStart.packed & ~LocationPack::MAX_OFFSET) | offset);

    // Выбираем map: output → m_backward, input → m_forward
    const auto& ranges = idx.isOutput() ? m_backward : m_forward;

    // Собираем все подходящие диапазоны
    std::vector<Range> result;
    ReaderFile fileId = loc.fileIdx();

    for (const auto& [key, entry] : ranges) {
        (void)key;

        // Фильтр: файл должен совпадать со стороной "from"
        if (entry.from.begin.fileIdx() != fileId)
            continue;
        if (!entry.from.end.isValid() || !entry.to.end.isValid())
            continue;

        // Проверка: loc внутри [from.begin, from.end]
        if (loc.packed < entry.from.begin.packed || loc.packed > entry.from.end.packed)
            continue;

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

// ══════════════════════════════════════════════════════════════
//          Context::mapStart / mapStop
// ══════════════════════════════════════════════════════════════

Context::Range Context::mapStart(MapperFile from, uint32_t from_begin, uint32_t from_end, MapperFile to) {
    if (!from.isValid())
        FAULT("mapStart: 'from' FileIdx is invalid");
    if (from_begin > from_end)
        FAULT("mapStart: from_begin ({}) > from_end ({})", from_begin, from_end);

    // Делегируем во вторую перегрузку mapStart(Range, MapperFile)
    MapperLocation begin = makeLoc(from, from_begin);
    MapperLocation end = makeLoc(from, from_end);
    return mapStart(MapperRange{begin, end}, to);
}

MapperRange Context::mapStart(MapperRange from, MapperFile to) {
    if (!from.begin.isValid() || !from.end.isValid())
        FAULT("mapStart(Range): 'from' Range is invalid");
    if (!to.isValid() || !to.isOutput())
        FAULT("mapStart(Range): 'to' FileIdx must be a valid output file");

    // Сохраняем текущую позицию выходного файла как Location
    Location outputBegin = makeLoc(to, get_file(to).size() + 1);

    m_mapStack.push_back({from, outputBegin});
    return from;
}

const Context::MapStartEntry& Context::mapStackTop() const {
    if (m_mapStack.empty())
        FAULT("mapStackTop: map stack is empty");
    return m_mapStack.back();
}

Context::Range Context::mapStop(MapperRange from) {
    EXPECT(!m_mapStack.empty());

    MapStartEntry entry = m_mapStack.back();
    m_mapStack.pop_back();

    EXPECT(entry.inputRange == from);

    // Текущая позиция выходного файла — размер m_source
    // size() возвращает 0-based размер, но Location ожидает 1-based offset
    MapperFile to = entry.outputBegin.fileIdx();
    uint32_t currentOffset = get_file(to).size() + 1;
    Location outputEnd = makeLoc(to, currentOffset);

    // Диапазон в выходном файле
    Range cppRange{entry.outputBegin, outputEnd};
    // + // Создаём RangeMap
    //     +if (!addRangeMapping(entry.inputRange, cppRange)) +
    //     FAULT("mapStop: addRangeMapping failed");
    // +

    // Проверки, как в addRangeMapping
    EXPECT(entry.inputRange.begin.isValid());
    EXPECT(entry.inputRange.end.isValid());
    EXPECT(cppRange.begin.isValid());
    EXPECT(cppRange.end.isValid());

    uint32_t trustKey = entry.inputRange.begin.packed;
    uint32_t cppKey = cppRange.begin.packed;

    EXPECT(m_forward.find(trustKey) == m_forward.end());
    EXPECT(m_backward.find(cppKey) == m_backward.end());

    m_forward[trustKey] = RangeMap{entry.inputRange, cppRange};
    m_backward[cppKey] = RangeMap{cppRange, entry.inputRange};
    m_reader.reset();

    return cppRange;
}

// ══════════════════════════════════════════════════════════════
//          unpackMacros & getMacroDefRange
// ══════════════════════════════════════════════════════════════

bool SourceMapReader::unpackMacros(msgpack_object macrosArray) {
    if (macrosArray.type != MSGPACK_OBJECT_ARRAY)
        return false;

    uint32_t inputCount = static_cast<uint32_t>(m_inputs.size());
    if (macrosArray.via.array.size != inputCount)
        return false;

    for (uint32_t bodyInIdx = 0; bodyInIdx < inputCount; ++bodyInIdx) {
        msgpack_object bodyGroup = macrosArray.via.array.ptr[bodyInIdx];
        if (bodyGroup.type != MSGPACK_OBJECT_ARRAY || bodyGroup.via.array.size != inputCount)
            return false;

        uint32_t bodyFileRaw = bodyInIdx + 1u;

        for (uint32_t defInIdx = 0; defInIdx < inputCount; ++defInIdx) {
            msgpack_object defGroup = bodyGroup.via.array.ptr[defInIdx];
            if (defGroup.type != MSGPACK_OBJECT_ARRAY)
                return false;

            if (defGroup.via.array.size == 0)
                continue;

            uint32_t defFileRaw = defInIdx + 1u;

            for (uint32_t e = 0; e < defGroup.via.array.size; ++e) {
                msgpack_object entryArr = defGroup.via.array.ptr[e];
                if (entryArr.type != MSGPACK_OBJECT_ARRAY)
                    return false;

                if (!createMacroEntry(&entryArr, bodyFileRaw, defFileRaw, m_macroForward))
                    return false;
            }
        }
    }
    return true;
}

// Возвращает диапазон определения макроса по позиции в теле макроса
std::optional<SourceMapReader::Range> SourceMapReader::getMacroDefRange(Location bodyLoc) const {
    return findRange(m_macroForward, bodyLoc);
}

// ══════════════════════════════════════════════════════════════
//          SourceMapReader::fromElf — читает embedded source map
//          из ELF-секции .debug_trust_map
// ══════════════════════════════════════════════════════════════

namespace {

// ELF64 structures
struct Elf64_Ehdr {
    unsigned char e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
};

struct Elf64_Shdr {
    uint32_t sh_name;
    uint32_t sh_type;
    uint64_t sh_flags;
    uint64_t sh_addr;
    uint64_t sh_offset;
    uint64_t sh_size;
    uint32_t sh_link;
    uint32_t sh_info;
    uint64_t sh_addralign;
    uint64_t sh_entsize;
};

// ELF magic
constexpr unsigned char ELF_MAGIC[] = {0x7f, 'E', 'L', 'F'};
constexpr int ELFCLASS64 = 2;
constexpr int ELFDATA2LSB = 1;

} // anonymous namespace

std::unique_ptr<SourceMapReader> SourceMapReader::fromElf(const std::string& elfPath) {
    // Open ELF file
    std::ifstream file(elfPath, std::ios::binary);
    if (!file)
        return nullptr;

    // Read ELF header
    Elf64_Ehdr ehdr;
    file.read(reinterpret_cast<char*>(&ehdr), sizeof(ehdr));
    if (!file || file.gcount() != static_cast<std::streamsize>(sizeof(ehdr)))
        return nullptr;

    // Validate ELF magic
    if (memcmp(ehdr.e_ident, ELF_MAGIC, 4) != 0)
        return nullptr;

    // Validate 64-bit, little-endian
    if (ehdr.e_ident[4] != ELFCLASS64)
        return nullptr;
    if (ehdr.e_ident[5] != ELFDATA2LSB)
        return nullptr;

    // Validate section header table
    if (ehdr.e_shoff == 0 || ehdr.e_shnum == 0 || ehdr.e_shentsize != sizeof(Elf64_Shdr))
        return nullptr;

    // Read section header string table
    if (ehdr.e_shstrndx >= ehdr.e_shnum)
        return nullptr;

    std::vector<Elf64_Shdr> sections(ehdr.e_shnum);
    file.seekg(static_cast<std::streamoff>(ehdr.e_shoff));
    file.read(reinterpret_cast<char*>(sections.data()), static_cast<std::streamsize>(ehdr.e_shnum * sizeof(Elf64_Shdr)));
    if (!file)
        return nullptr;

    // Read section name string table
    const auto& shstrtab_hdr = sections[ehdr.e_shstrndx];
    std::vector<char> shstrtab(static_cast<size_t>(shstrtab_hdr.sh_size));
    file.seekg(static_cast<std::streamoff>(shstrtab_hdr.sh_offset));
    file.read(shstrtab.data(), static_cast<std::streamsize>(shstrtab_hdr.sh_size));
    if (!file)
        return nullptr;

    // Find .debug_trust_map section
    for (const auto& shdr : sections) {
        if (shdr.sh_name >= shstrtab.size())
            continue;

        const char* name = shstrtab.data() + shdr.sh_name;
        if (strcmp(name, ".debug_trust_map") != 0)
            continue;

        if (shdr.sh_size < 13) // минимальный размер: 4 (orig_size) + 1 (compressed) + 8 (checksum)
            return nullptr;

        // Read section data
        std::vector<unsigned char> section_data(static_cast<size_t>(shdr.sh_size));
        file.seekg(static_cast<std::streamoff>(shdr.sh_offset));
        file.read(reinterpret_cast<char*>(section_data.data()), static_cast<std::streamsize>(shdr.sh_size));
        if (!file)
            return nullptr;

        // Delegate to fromMsgpack
        return fromMsgpack(section_data.data(), section_data.size());
    }

    // Section not found
    return nullptr;
}

} // namespace trust