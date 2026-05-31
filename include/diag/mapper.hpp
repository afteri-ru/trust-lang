#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
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
        ASSERT(m_source.size() <= LocationPack::MAX_OFFSET); // Should be checked when adding data
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
            EXPECT(from.is_valid() && to.is_valid()) ;
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
    mutable LruCache<uint32_t, FileEntry::LineColumn> m_lc_cache;
    using LocCacheKey = uint64_t;
    static_assert(sizeof(LocCacheKey) == sizeof(LocationPack::RawType) * 2);
    mutable LruCache<LocCacheKey, Location> m_loc_cache;
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
    // Читает содержимое m_inputs и m_outputs с диска.
    // filename всегда относительные — они резолвятся относительно baseDir.
    // Если baseDir пустой, используется текущая рабочая директория.
    // Возвращает true, если все файлы успешно прочитаны (хеши могут не совпадать).
    // Читает массив filename-ов или хешей из msgpack-поля
    // Если filenames == true — читает имена файлов, иначе — хеши
    static bool readFileArray(msgpack_object array, std::vector<FileEntry>& files, bool filenames);

    bool readFilesFromDisk(std::string_view baseDir);

    // Проверяет хеш файла с заданным индексом.
    // Возвращает true, если хеш текущего содержимого совпадает с оригинальным (из msgpack).
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
    // Возвращает диапазон определения макроса по позиции в теле макроса
    std::optional<Range> getMacroDefRange(Location bodyLoc) const;

    // ── Извлечение слова под курсором ──
    // Определяет границы [a-zA-Z0-9_] вокруг позиции loc и возвращает текст слова.
    // Возвращает std::nullopt, если под курсором не буквенно-цифровой символ.
    [[nodiscard]] std::optional<std::string> getWordAt(Location loc) const;

    // ── LSP convenience ──
    // Конвертирует LSP 0-based (line, character) в Location (1-based offset внутри файла idx)
    [[nodiscard]] Location lspToLocation(ReaderFile idx, int line, int character) const;

    // ── Фрагмент URL ──
    // Преобразует Range в строковый фрагмент "L{startLine},{startCol}-{endLine},{endCol}"
    [[nodiscard]] std::string rangeToFragmentString(Range range) const;

    // ── Поиск полного RangeMap по позиции ──
    // Автоматически выбирает m_forward (для input-файлов) или m_backward (для output-файлов)
    [[nodiscard]] std::optional<RangeMap> findRangeMap(Location loc) const;

    // ══════════════════════════════════════════════════════════════
    //  High-level convenience методы (упрощение клиентского кода)
    // ══════════════════════════════════════════════════════════════

    /// Проверяет расширение файла: .src (исходный файл TrustLang).
    /// .trust — бинарный скомпилированный модуль — не является исходным файлом.
    [[nodiscard]] static bool isTrustFileExt(const std::string& path) noexcept;

    /// Проверяет расширение сгенерированного C++ файла: .cppt или .hppt
    /// (результат транспиляции .src файлов). Не путать с собственными .cpp/.hpp
    /// файлами компилятора — они не проходят через SourceMap.
    [[nodiscard]] static bool isCppFileExt(const std::string& path) noexcept;

    /// Поиск FileIdx по пути с fallback на basename.
    /// Заменяет последовательность findFileIdx() + findFileIdxByBasename().
    [[nodiscard]] ReaderFile findFile(const std::string& path) const;

    /// Трансляция trust → cpp по имени файла и строке.
    /// findFile(trustPath) → loc_from_line → getMapTrustToCpp.
    /// Возвращает диапазон в C++ файле или nullopt.
    [[nodiscard]] std::optional<Range> findTrustToCpp(const std::string& trustPath, int line) const;

    /// Трансляция cpp → trust по имени файла и строке.
    /// findFile(cppPath) → loc_from_line → getMapCppToTrust.
    /// Возвращает диапазон в trust-файле или nullopt.
    [[nodiscard]] std::optional<Range> findCppToTrust(const std::string& cppPath, int line) const;

    /// Трансляция cpp → trust с возвратом (trustFilename, trustLine).
    /// Удобная обёртка над findCppToTrust для DAP-хендлеров.
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

    // Шаблон для поиска имени по позиции в nameMappings
    // nameMatcher(v) — проверяет совпадение имени
    // rangeGetter(v) — возвращает ссылку на Range для проверки позиции
    // rangeAdjuster(result, offset) — корректирует противоположный Range в result
    template <typename NameMatcher, typename RangeGetter, typename RangeAdjuster>
    static std::optional<NameMap> findNameInMappings(const std::vector<NameMap>& nameMappings, uint32_t locPacked, NameMatcher&& nameMatcher,
                                                     RangeGetter&& rangeGetter, RangeAdjuster&& rangeAdjuster);
};

// ══════════════════════════════════════════════════════════════
//      Mapper template implementations (inline)
// ══════════════════════════════════════════════════════════════

template <typename FileIdx>
FileEntry& SourceMap<FileIdx>::get_file(FileIdx idx) {

    SourceMap::FileType::check_limit(idx.raw);
    EXPECT(idx.isValid());

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
    EXPECT(range.is_valid());
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
    if (exact.isValid())
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
    const auto* cached = m_lc_cache.lookup(loc.offset());
    if (cached != nullptr) {
        return *cached;
    }
    if (!loc.isValid()) {
        return FileEntry::LineColumn{1, 1};
    }
    auto result = get_file(FileIdx{loc.fileIdx().raw}).calc_column(loc.offset());
    m_lc_cache.insert(loc.offset(), result);
    return result;
}

template <typename FileIdx>
typename SourceMap<FileIdx>::Location SourceMap<FileIdx>::loc_from_line(FileIdx idx, size_t line) const {
    static constexpr size_t MAX_LINE = LocationPack::MAX_OFFSET;
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

template <typename NameMatcher, typename RangeGetter, typename RangeAdjuster>
std::optional<SourceMapReader::NameMap> SourceMapReader::findNameInMappings(const std::vector<NameMap>& nameMappings, uint32_t locPacked,
                                                                            NameMatcher&& nameMatcher, RangeGetter&& rangeGetter,
                                                                            RangeAdjuster&& rangeAdjuster) {
    for (const auto& v : nameMappings) {
        const auto& r = rangeGetter(v);
        if (nameMatcher(v) && locPacked >= r.begin.packed && locPacked <= r.end.packed) {
            int offset = locPacked - r.begin.packed;
            NameMap result = v;
            rangeAdjuster(result, offset);
            return result;
        }
    }
    return std::nullopt;
}

} // namespace trust