#pragma once

#include <cstdint>
#include <expected>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <filesystem>
#include <vector>

#include "diag/location.hpp"
#include "diag/msgpack_util.hpp"
#include "utils/cache.hpp"
#include "utils/error.hpp"

namespace trust {

// ══════════════════════════════════════════════════════════════
//  FileEntry — запись о файле (входном или выходном)
// ══════════════════════════════════════════════════════════════
class FileEntry {
  public:
    struct LineColumn {
        size_t line{1};
        size_t column{1};
    };

    FileEntry(std::string filename, std::string source = "");
    FileEntry() = default;

    // ── Accessors ──
    [[nodiscard]] const std::string& getFilename() const noexcept { return m_filename; }
    [[nodiscard]] const std::string& getSource() const noexcept { return m_source; }
    [[nodiscard]] LocationPack::RawType size() const {
        ASSERT(m_source.size() <= LocationPack::MAX_OFFSET_INPUT); // Should be checked when adding data
        return static_cast<LocationPack::RawType>(m_source.size());
    }
    [[nodiscard]] uint64_t getHash() const noexcept;

    // ── Mutators ──
    void setFilename(std::string f);
    void setSource(std::string s);
    void appendSource(std::string_view text);
    void setHashOriginal(std::optional<uint64_t> h) noexcept { m_hash_original = h; }

  protected:
    // Вычислениея строк и колонок по смещению в файле и наоборот
    // Используется SparseCache + линейный поиск
    // Предназначены для исолпьзования только в классе SourceMap
    // у которого есть дополнительный LruCache с O(1) поиском

    template <typename F>
    friend struct SourceMap;
    friend class SourceMapReader;

    // ── Вычисление по offset (1-based) → (line, column) ──
    [[nodiscard]] LineColumn calc_column(size_t offset) const;

    // ── Вычисление по line (1-based) → offset (1-based) ──
    [[nodiscard]] size_t calc_line_offset(size_t line) const;

    // ── Количество строк в файле ──
    [[nodiscard]] size_t calc_line_count() const { return std::count(m_source.begin(), m_source.end(), '\n') + 1; }

  private:
    std::string m_filename;
    std::string m_source;
    mutable std::optional<uint64_t> m_hash;

    std::optional<uint64_t> m_hash_original{std::nullopt};
    mutable SparseCache m_sparseCache;

    void ensureCache() const;
    void invalidateCache() noexcept {
        m_sparseCache.invalidate();
        m_hash.reset();
    }
};

// ══════════════════════════════════════════════════════════════
//  SourceMap<FileIdx>: шаблонный базовый класс
// ══════════════════════════════════════════════════════════════

template <typename FileIdx>
struct SourceMap {
    static_assert(IsTaggedFile<FileIdx>, "SourceMap<FileIdx> requires TaggedFile (MapperFile or ReaderFile)");

    using FileType = TaggedFile<FileIdx>;
    using Location = TaggedLocation<FileIdx>;
    using Range = typename Location::RangeType;

    // Вложенные типы маппинга
    struct RangeMap {
        Range from;
        Range to;

        RangeMap() = default;
        RangeMap(Range f, Range t)
        : from(f)
        , to(t) {
            // clang-format off
            EXPECT(!from.isInvalid() && !to.isInvalid());
            // clang-format on
        }
    };

    struct NameMap {
        RangeMap rangeMap;
        std::string fromName;
        std::string toName;
        std::optional<Range> macroDefRange; // для макросов: диапазон определения

        NameMap() = default;
        NameMap(RangeMap r, std::string tn, std::string cn)
        : rangeMap(r)
        , fromName(std::move(tn))
        , toName(std::move(cn)) {
            EXPECT(!fromName.empty() && !toName.empty());
        }
    };

    std::map<LocationPack::RawType, RangeMap> m_forward;
    std::map<LocationPack::RawType, RangeMap> m_backward;

    // m_macroForward: маппинг тела макроса → его определения (input → input)
    std::map<LocationPack::RawType, RangeMap> m_macroForward;

    std::vector<NameMap> m_nameMappings;
    std::unordered_multimap<std::string, std::string> m_nameBackward;

    // ── Файловая информация ──
    std::vector<FileEntry> m_inputs;
    std::vector<FileEntry> m_outputs;

    // ── Доступ к файловым записям (по FileIdx) ──
    FileEntry& get_file(FileIdx idx);
    const FileEntry& get_file(FileIdx idx) const { return const_cast<SourceMap*>(this)->get_file(idx); }

    // ── Поиск FileIdx по точному совпадению имени файла ──
    FileIdx findFileIdx(std::string_view name) const;

    // ── Поиск FileIdx по basename (без пути) ──
    // Ищет среди input и output файлов, сравнивая filename() с name или
    // std::filesystem::path(filename()).filename() с name.
    FileIdx findFileIdxByBasename(std::string_view name) const;

    // ── filename/source по FileIdx ──
    [[nodiscard]] std::string_view filename(FileIdx idx) const;
    [[nodiscard]] std::string_view source(FileIdx idx) const;

    // ── line_column/loc_from_line с LRU-кешем ──
    // Работают с Location (= TaggedLocation<FileIdx>)
    [[nodiscard]] FileEntry::LineColumn line_column(Location loc) const;
    [[nodiscard]] uint32_t line(Location loc) const { return line_column(loc).line; }
    [[nodiscard]] uint32_t column(Location loc) const { return line_column(loc).column; }
    [[nodiscard]] Location loc_from_line(FileIdx idx, size_t line) const;

    // ── Количество строк в файле по FileIdx ──
    [[nodiscard]] uint32_t lineCount(FileIdx idx) const { return static_cast<uint32_t>(get_file(idx).calc_line_count()); }

    // ── Контрольная сумма файла по FileIdx ──
    [[nodiscard]] uint64_t getFileHash(FileIdx idx) const;

    uint32_t input_count() const { return static_cast<uint32_t>(m_inputs.size()); }
    uint32_t output_count() const { return static_cast<uint32_t>(m_outputs.size()); }
    uint32_t file_count() const { return static_cast<uint32_t>(m_inputs.size()); }

    // ── Создание Location (доступно через friend location.hpp) ──
    [[nodiscard]] Location makeLoc(FileIdx idx, uint32_t offset) const;

    RangeMap makeMap(Range from, Range to) {
        ASSERT(from.begin.fileIdx() == from.begin.fileIdx()); // Must be checked when creating a range
        ASSERT(from.begin.offset() <= from.end.offset());     // Must be checked when creating a range
        ASSERT(to.begin.fileIdx() == to.begin.fileIdx());     // Must be checked when creating a range
        ASSERT(to.begin.offset() <= to.end.offset());         // Must be checked when creating a range

        return RangeMap(from, to);
    }

    // makeMap с int-параметрами — удобная обёртка
    Range makeMap(FileIdx from, size_t from_begin, size_t from_end, FileIdx to, size_t to_begin, size_t to_end) {
        // clang-format off
        return makeMap(
            Range(makeLoc(from, from_begin), makeLoc(from, from_end)),
            Range(makeLoc(to, to_begin), makeLoc(to, to_end))
        ).from;
        // clang-format on
    }

    std::string_view getText(Range range) const;

    static constexpr int DEFAULT_LRU_SIZE = 4;
    SourceMap()
    : m_lc_cache(DEFAULT_LRU_SIZE)
    , m_loc_cache(DEFAULT_LRU_SIZE) {}
    explicit SourceMap(int lru_size)
    : m_lc_cache(lru_size)
    , m_loc_cache(lru_size) {}

  protected:
    using LcCacheKey = uint64_t;
    static_assert(sizeof(LcCacheKey) == sizeof(LocationPack::RawType) * 2);
    mutable LruCache<LcCacheKey, FileEntry::LineColumn> m_lc_cache;
    using LocCacheKey = uint64_t;
    static_assert(sizeof(LocCacheKey) == sizeof(LocationPack::RawType) * 2);
    mutable LruCache<LocCacheKey, Location> m_loc_cache;
};

// ── Forward declarations ──
class SourceMapReader;
struct OutputBuffer;

// ══════════════════════════════════════════════════════════════
//  OutputBuffer: буфер prepend-данных выходного C++ файла
// ══════════════════════════════════════════════════════════════
struct OutputBuffer {
    // ns -> set of unique prefix strings
    std::map<std::string, std::set<std::string>> m_prefixes;

    void prepend(std::string_view text, std::string_view ns = "");
    std::string build(unsigned indentSize = 4) const;
};

// ══════════════════════════════════════════════════════════════
//  SourceMapWriter: mutable (writer space) — интеграция всего
//  функционала из бывшего Context, связанного с SourceMap.
// ══════════════════════════════════════════════════════════════

class SourceMapWriter : public SourceMap<MapperFile> {
  public:
    using TranspileResult = std::expected<std::pair<MapperFile, MapperFile>, std::string>;

    SourceMapWriter();
    explicit SourceMapWriter(std::string_view basePath, std::string_view tempPath = "");

    SourceMapWriter(const SourceMapWriter&) = delete;
    SourceMapWriter& operator=(const SourceMapWriter&) = delete;

    // ── Входные файлы ──

    // Добавляет содержимое источника из памяти.
    // Если normalize == true: path нормализуется (приводится к относительному от baseDir).
    // Если normalize == false: filename проверяется — должен содержать только [a-zA-Z0-9_].
    // Возвращает MapperFile или {0} при ошибке.
    [[nodiscard]] MapperFile add_source(std::string filename, std::string content, bool normalize = true);

    // Загружает файл с диска.
    [[nodiscard]] MapperFile load_file(std::string path);

    uint32_t file_count() const { return static_cast<uint32_t>(m_inputs.size()); }

    // ── Выходные файлы ──

    // add_output: регистрирует выходной файл.
    [[nodiscard]] MapperFile add_output(std::string filename, bool normalize = true);
    bool output_append(MapperFile idx, std::string_view text);
    bool output_prepend(MapperFile idx, std::string_view text, std::string_view ns = "");
    [[nodiscard]] std::string output_result(MapperFile idx) const;
    [[nodiscard]] std::string_view output_body(MapperFile idx) const;
    [[nodiscard]] bool save_output(std::string_view outputDir);
    uint32_t output_count() const { return static_cast<uint32_t>(m_outputs.size()); }

    // ── Создание и валидация Location / Range ──
    [[nodiscard]] MapperRange makeRange(MapperLocation begin, MapperLocation end) const;
    [[nodiscard]] bool isValid(MapperLocation loc) const;
    [[nodiscard]] bool isValid(MapperRange range) const;

    // ── Методы маппинга (SourceMapWriter) ──
    bool addRangeMapping(MapperRange trustRange, MapperRange cppRange);
    bool addNameMapping(MapperRange trustRange, MapperRange cppRange, std::string_view trustName, std::string_view cppName);
    bool addMacroMapping(MapperRange bodyRange, MapperRange defRange);

    // ── Стек для mapStart/mapStop ──
    struct MapStartEntry {
        MapperRange inputRange;
        MapperLocation outputBegin;
    };

    MapperRange mapStart(MapperFile from, uint32_t from_begin, uint32_t from_end, MapperFile to);
    MapperRange mapStart(MapperRange from, MapperFile to);
    MapperRange mapStop(MapperRange from);
    const MapStartEntry& mapStackTop() const;

    // ── toReader ──
    const SourceMapReader* toReader() const;

    // ── Информация об источнике ──
    [[nodiscard]] std::string_view filename(MapperLocation loc) const;
    [[nodiscard]] std::string_view source(MapperLocation loc) const;
    using SourceMap::column;
    using SourceMap::filename;
    using SourceMap::findFileIdx;
    using SourceMap::get_file;
    using SourceMap::getFileHash;
    using SourceMap::getText;
    using SourceMap::line;
    using SourceMap::line_column;
    using SourceMap::lineCount;
    using SourceMap::loc_from_line;
    using SourceMap::makeLoc;
    using SourceMap::source;

    // ── Поиск FileIdx по пути с нормализацией ──
    MapperFile findFileIdx(std::string_view filePath) const;

    void setBaseDirectory(std::string_view path);
    [[nodiscard]] const std::string& baseDirectory() const noexcept { return m_baseDirectory; }

  private:
    static bool validateSimpleName(std::string_view name);
    std::string normalizePath(std::string_view path) const;

    std::string get_prepend(MapperFile idx, unsigned indentSize = 4) const;
    uint32_t get_output_size(MapperFile idx) const;

    // Буферы prepend-данных, ключ = FileIdx.raw
    std::unordered_map<uint32_t, OutputBuffer> m_outputBuffers;
    mutable std::unique_ptr<SourceMapReader> m_reader;

    std::string m_baseDirectory;
    std::string m_tempDirectory;

    std::vector<MapStartEntry> m_mapStack;
};

// ══════════════════════════════════════════════════════════════
//  SourceMapReader: read-only (reader space)
// ══════════════════════════════════════════════════════════════

class SourceMapReader : public SourceMap<ReaderFile> {
  public:
    using Range = SourceMap<ReaderFile>::Range;
    using Location = SourceMap<ReaderFile>::Location;
    using NameMap = SourceMap<ReaderFile>::NameMap;
    using RangeMap = SourceMap<ReaderFile>::RangeMap;

    SourceMapReader() = default;

    // ── Factory methods ──
    static std::unique_ptr<SourceMapReader> fromMsgpack(const unsigned char* data, size_t size);
    /// Читает embedded source map из ELF-секции .debug_trust_map
    /// Возвращает nullptr, если ELF невалиден, секция не найдена или данные повреждены.
    static std::unique_ptr<SourceMapReader> fromElf(const std::string& elfPath);

    // ── Чтение файлов с диска после десериализации ──
    static bool readFileArray(msgpack_object array, std::vector<FileEntry>& files, bool filenames);

    bool readFilesFromDisk(std::string_view baseDir);

    // Проверяет хеш файла с заданным индексом.
    [[nodiscard]] bool verifyHash(ReaderFile idx) const;

    // ── Поиск диапазона по позиции ──
    std::optional<Range> getMapTrustToCpp(Location trustLoc) const;
    std::optional<Range> getMapCppToTrust(Location cppLoc) const;

    // ── Поиск всех диапазонов по строке/колонке ──
    std::vector<Range> findRangesByLine(ReaderFile idx, uint32_t line, std::optional<uint32_t> column = std::nullopt) const;

    // ── Поиск имени по позиции ──
    std::optional<NameMap> getCppName(Location trustLoc, std::string_view trustName) const;
    std::optional<NameMap> getTrustName(Location cppLoc, std::string_view cppName) const;

    // ── Итерация маппингов по файлу ──
    std::vector<RangeMap> getTrustFileMappings(ReaderFile trustFileIdx) const;

    // ── Доступ к nameMappings ──
    [[nodiscard]] const std::vector<NameMap>& getNameMappings() const { return m_nameMappings; }

    // ── Доступ к маппингам ──
    [[nodiscard]] const std::map<uint32_t, RangeMap>& getForwardMappings() const { return m_forward; }
    [[nodiscard]] const std::map<uint32_t, RangeMap>& getBackwardMappings() const { return m_backward; }

    // ── Поиск макросов ──
    std::optional<Range> getMacroDefRange(Location bodyLoc) const;

    // ── Извлечение слова под курсором ──
    [[nodiscard]] std::optional<std::string> getWordAt(Location loc) const;

    // ── LSP convenience ──
    [[nodiscard]] Location lspToLocation(ReaderFile idx, int line, int character) const;

    // ── Фрагмент URL ──
    [[nodiscard]] std::string rangeToFragmentString(Range range) const;

    // ── Поиск полного RangeMap по позиции ──
    [[nodiscard]] std::optional<RangeMap> findRangeMap(Location loc) const;

    // ══════════════════════════════════════════════════════════════
    //  High-level convenience методы
    // ══════════════════════════════════════════════════════════════

    [[nodiscard]] static bool isTrustFileExt(const std::string& path) noexcept;
    [[nodiscard]] static bool isCppFileExt(const std::string& path) noexcept;
    /// True, если имя source-map файла помечено как фиктивный (in-memory) источник
    /// префиксом '@' — такого файла нет на диске, искать его не нужно.
    [[nodiscard]] static bool isInMemoryName(std::string_view name) noexcept;
    [[nodiscard]] ReaderFile findFile(const std::string& path) const;
    [[nodiscard]] std::optional<Range> findTrustToCpp(const std::string& trustPath, int line) const;
    [[nodiscard]] std::optional<Range> findCppToTrust(const std::string& cppPath, int line) const;
    [[nodiscard]] std::optional<std::pair<std::string, int>> calcCppToTrustLine(const std::string& cppPath, int cppLine) const;

    // ── Сериализация (msgpack) ──
    void packRanges(MsgpackWriter& wr, const std::map<uint32_t, RangeMap>& forward) const;
    void packNames(MsgpackWriter& wr, const std::vector<NameMap>& nameMappings) const;
    void packMacros(MsgpackWriter& wr) const;

    [[nodiscard]] std::vector<unsigned char> packToMsgpack() const;

    void setFiles(std::vector<FileEntry> inputs, std::vector<FileEntry> outputs) {
        m_inputs = std::move(inputs);
        m_outputs = std::move(outputs);
    }

  private:
    bool unpackRanges(msgpack_object rangesArray);
    bool unpackNames(msgpack_object namesArray);
    bool unpackMacros(msgpack_object macrosArray);

    static bool readSingleFile(FileEntry& entry, const std::filesystem::path& basePath);

    static std::optional<Range> findRange(const std::map<uint32_t, RangeMap>& ranges, Location loc);

    template <typename NameMatcher>
    static std::optional<NameMap> findNameInMappings(const std::vector<NameMap>& nameMappings, uint32_t locPacked, NameMatcher&& nameMatcher,
                                                     Range RangeMap::* rangeMember);
};

// ══════════════════════════════════════════════════════════════
//      SourceMap template implementations (inline)
// ══════════════════════════════════════════════════════════════

template <typename FileIdx>
FileEntry& SourceMap<FileIdx>::get_file(FileIdx idx) {

    SourceMap::FileType::check_limit(idx.raw);
    EXPECT(!idx.isInvalid());

    size_t index = idx.as_index();
    if (idx.isOutput()) {
        EXPECT(index < m_outputs.size());
        return m_outputs[index];
    }

    EXPECT(index < m_inputs.size());
    return m_inputs[index];
}
template <typename FileIdx>
std::string_view SourceMap<FileIdx>::getText(Range range) const {
    EXPECT(!range.isInvalid());
    std::string_view src = source(range.begin.fileIdx());
    LocationPack::RawType off = range.begin.offset() - 1;
    LocationPack::RawType len = range.end.offset() - range.begin.offset();
    if (off + len > src.size()) {
        FAULT("getText: range [{},{}] out of bounds (source size={})", off, off + len, src.size());
    }
    return src.substr(off, len);
}

template <typename FileIdx>
FileIdx SourceMap<FileIdx>::findFileIdx(std::string_view name) const {
    for (uint32_t i = 0; i < m_inputs.size(); ++i) {
        if (m_inputs[i].getFilename() == name)
            return FileIdx::make_input(i);
    }
    for (uint32_t i = 0; i < m_outputs.size(); ++i) {
        if (m_outputs[i].getFilename() == name)
            return FileIdx::make_output(i);
    }
    return FileIdx{0};
}

template <typename FileIdx>
FileIdx SourceMap<FileIdx>::findFileIdxByBasename(std::string_view name) const {
    // Сначала точное совпадение (полный путь)
    FileIdx exact = findFileIdx(name);
    if (!exact.isInvalid())
        return exact;

    // Поиск по basename
    namespace fs = std::filesystem;
    for (uint32_t i = 0; i < m_inputs.size(); ++i) {
        std::string_view fname = m_inputs[i].getFilename();
        if (fname == name || fs::path(fname).filename() == name)
            return FileIdx::make_input(i);
    }
    for (uint32_t i = 0; i < m_outputs.size(); ++i) {
        std::string_view fname = m_outputs[i].getFilename();
        if (fname == name || fs::path(fname).filename() == name)
            return FileIdx::make_output(i);
    }
    return FileIdx{0};
}

template <typename FileIdx>
std::string_view SourceMap<FileIdx>::filename(FileIdx idx) const {
    return get_file(idx).getFilename();
}

template <typename FileIdx>
std::string_view SourceMap<FileIdx>::source(FileIdx idx) const {
    return get_file(idx).getSource();
}

template <typename FileIdx>
FileEntry::LineColumn SourceMap<FileIdx>::line_column(Location loc) const {
    // Ключ включает fileIdx — иначе при нескольких input/output файлах кэш
    // коллизирует по одинаковым offset'ам разных файлов (даёт неверные line/col).
    LcCacheKey key = (static_cast<LcCacheKey>(loc.fileIdx().raw) << sizeof(LocationPack::RawType) * 8) | static_cast<LcCacheKey>(loc.offset());
    const auto* cached = m_lc_cache.lookup(key);
    if (cached != nullptr) {
        return *cached;
    }
    if (loc.isInvalid()) {
        return FileEntry::LineColumn{1, 1};
    }
    auto result = get_file(FileIdx{loc.fileIdx().raw}).calc_column(loc.offset());
    m_lc_cache.insert(key, result);
    return result;
}

template <typename FileIdx>
typename SourceMap<FileIdx>::Location SourceMap<FileIdx>::loc_from_line(FileIdx idx, size_t line) const {
    static constexpr size_t MAX_LINE = LocationPack::MAX_OFFSET_INPUT;
    if (line > MAX_LINE) {
        FAULT("loc_from_line: line {} exceeds MAX_OFFSET {}", line, MAX_LINE);
    }
    uint32_t line32 = static_cast<uint32_t>(line);
    LocCacheKey key = (static_cast<LocCacheKey>(idx.raw) << sizeof(LocationPack::RawType) * 8) | line32;
    const auto* cached = m_loc_cache.lookup(key);
    if (cached != nullptr) {
        return *cached;
    }
    uint32_t offset = get_file(idx).calc_line_offset(line32);
    Location result{TaggedLocation<FileIdx>::makeLoc(idx, offset)};
    m_loc_cache.insert(key, result);
    return result;
}

template <typename FileIdx>
typename SourceMap<FileIdx>::Location SourceMap<FileIdx>::makeLoc(FileIdx idx, uint32_t offset) const {
    return Location{idx, offset};
}

template <typename FileIdx>
uint64_t SourceMap<FileIdx>::getFileHash(FileIdx idx) const {
    return get_file(idx).getHash();
}

// ── SourceMapReader template methods ──

template <typename NameMatcher>
std::optional<SourceMapReader::NameMap> SourceMapReader::findNameInMappings(const std::vector<NameMap>& nameMappings, uint32_t locPacked,
                                                                            NameMatcher&& nameMatcher, Range RangeMap::* rangeMember) {
    for (const auto& v : nameMappings) {
        const Range& r = v.rangeMap.*rangeMember;
        if (nameMatcher(v) && locPacked >= r.begin.packed && locPacked <= r.end.packed)
            return v;
    }
    return std::nullopt;
}

} // namespace trust