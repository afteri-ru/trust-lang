#pragma once

// include/sourcemap/store/source_map.hpp
// SourceMap<FileIdx>: шаблонная база хранения файлов и маппингов в tagged-пространстве позиций.

#include "sourcemap/store/file_entry.hpp"

#include <cstdint>
#include <deque>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace trust {

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

    // -- Файловая информация --
    // Неперемещаемый контейнер: на буферы FileEntry ссылаются std::string_view в Term/AST.
    // deque не перемещает элементы при росте (в отличие от vector, где SSO-строки меняют адрес).
    std::deque<FileEntry> m_inputs;
    std::deque<FileEntry> m_outputs;

    // -- Доступ к файловым записям (по FileIdx) --
    FileEntry& get_file(FileIdx idx);
    const FileEntry& get_file(FileIdx idx) const { return const_cast<SourceMap*>(this)->get_file(idx); }

    // -- Поиск FileIdx по точному совпадению имени файла --
    FileIdx findFileIdx(std::string_view name) const;

    // -- Поиск FileIdx по basename (без пути) --
    // Ищет среди input и output файлов, сравнивая filename() с name или
    // std::filesystem::path(filename()).filename() с name.
    FileIdx findFileIdxByBasename(std::string_view name) const;

    // -- filename/source по FileIdx --
    [[nodiscard]] std::string_view filename(FileIdx idx) const;
    [[nodiscard]] std::string_view source(FileIdx idx) const;

    // -- line_column/loc_from_line с LRU-кешем --
    // Работают с Location (= TaggedLocation<FileIdx>)
    [[nodiscard]] FileEntry::LineColumn line_column(Location loc) const;
    [[nodiscard]] uint32_t line(Location loc) const { return line_column(loc).line; }
    [[nodiscard]] uint32_t column(Location loc) const { return line_column(loc).column; }
    [[nodiscard]] Location loc_from_line(FileIdx idx, size_t line) const;

    // -- Количество строк в файле по FileIdx --
    [[nodiscard]] uint32_t lineCount(FileIdx idx) const { return static_cast<uint32_t>(get_file(idx).calc_line_count()); }

    // -- Контрольная сумма файла по FileIdx --
    [[nodiscard]] uint64_t getFileHash(FileIdx idx) const;

    uint32_t input_count() const { return static_cast<uint32_t>(m_inputs.size()); }
    uint32_t output_count() const { return static_cast<uint32_t>(m_outputs.size()); }
    uint32_t file_count() const { return static_cast<uint32_t>(m_inputs.size()); }

    // -- Создание Location (доступно через friend location.hpp) --
    [[nodiscard]] Location makeLoc(FileIdx idx, uint32_t offset) const;

    RangeMap makeMap(Range from, Range to) {
        ASSERT(from.begin.fileIdx() == from.begin.fileIdx()); // Must be checked when creating a range
        ASSERT(from.begin.offset() <= from.end.offset());     // Must be checked when creating a range
        ASSERT(to.begin.fileIdx() == to.begin.fileIdx());     // Must be checked when creating a range
        ASSERT(to.begin.offset() <= to.end.offset());         // Must be checked when creating a range

        return RangeMap(from, to);
    }

    // makeMap с int-параметрами - удобная обёртка
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
        if (m_inputs[i].getFilename() == name) {
            return FileIdx::make_input(i);
        }
    }
    for (uint32_t i = 0; i < m_outputs.size(); ++i) {
        if (m_outputs[i].getFilename() == name) {
            return FileIdx::make_output(i);
        }
    }
    return FileIdx{0};
}

template <typename FileIdx>
FileIdx SourceMap<FileIdx>::findFileIdxByBasename(std::string_view name) const {
    // Сначала точное совпадение (полный путь)
    FileIdx exact = findFileIdx(name);
    if (!exact.isInvalid()) {
        return exact;
    }

    // Поиск по basename
    namespace fs = std::filesystem;
    for (uint32_t i = 0; i < m_inputs.size(); ++i) {
        std::string_view fname = m_inputs[i].getFilename();
        if (fname == name || fs::path(fname).filename() == name) {
            return FileIdx::make_input(i);
        }
    }
    for (uint32_t i = 0; i < m_outputs.size(); ++i) {
        std::string_view fname = m_outputs[i].getFilename();
        if (fname == name || fs::path(fname).filename() == name) {
            return FileIdx::make_output(i);
        }
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
    // Ключ включает fileIdx - иначе при нескольких input/output файлах кэш
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

} // namespace trust
