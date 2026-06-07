#pragma once

#include "utils/error.hpp"
#include <cstdint>
#include <cstddef>
#include <type_traits>

namespace trust {

// ── Константы формата упаковки TaggedFile и TaggedLocation ──
// Полная замена битовой раскладки, без обратной совместимости.
// ── TaggedFile.raw ──
//   raw = 0                         — невалидный
//   bit 31 = 0: входной файл:       raw = index + 1 (1..511)
//   bit 31 = 1: выходной файл:      raw = (index + 1) | OUTPUT_FILE_BIT (1..31)
// ── TaggedLocation.packed ──
//   bit 31 = 0: входной файл:       bits 30-22 = index+1 (9 бит, 511)
//                                   bits 21-0  = offset (22 бита, ~4MB)
//   bit 31 = 1: выходной файл:      bits 30-26 = index+1 (5 бит, 31)
//                                   bits 25-0  = offset (26 бит, ~64MB)
struct LocationPack {
    using RawType = uint32_t;

    // ── Для TaggedFile ──
    static constexpr int FILE_BITS_INPUT = 9;
    static constexpr int MAX_FILES_INPUT = (1 << FILE_BITS_INPUT) - 1; // 511

    static constexpr int FILE_BITS_OUTPUT = 5;
    static constexpr int MAX_FILES_OUTPUT = (1 << FILE_BITS_OUTPUT) - 1; // 31

    static constexpr RawType OUTPUT_FILE_BIT = 1u << 31; // флаг выходного файла (в TaggedFile и TaggedLocation)

    // ── Для TaggedLocation: входные файлы ──
    static constexpr int OFFSET_BITS_INPUT = 22;
    static constexpr int MAX_OFFSET_INPUT = (1 << OFFSET_BITS_INPUT) - 1; // 4'194'303 (~4MB)

    // ── Для TaggedLocation: выходные файлы ──
    static constexpr int OFFSET_BITS_OUTPUT = 26;
    static constexpr int MAX_OFFSET_OUTPUT = (1 << OFFSET_BITS_OUTPUT) - 1; // 67'108'863 (~64MB)
};

static_assert(LocationPack::FILE_BITS_INPUT + LocationPack::OFFSET_BITS_INPUT + 1 == 32);
static_assert(LocationPack::MAX_FILES_INPUT == 511);
static_assert(LocationPack::MAX_OFFSET_INPUT == 4'194'303);

static_assert(LocationPack::FILE_BITS_OUTPUT + LocationPack::OFFSET_BITS_OUTPUT + 1 == 32);
static_assert(LocationPack::MAX_FILES_OUTPUT == 31);
static_assert(LocationPack::MAX_OFFSET_OUTPUT == 67'108'863);

// TaggedFile — шаблонный идентификатор файла (входного или выходного).
template <typename Tag>
struct TaggedFile {
    explicit constexpr TaggedFile()
    : raw(0) {}
    explicit constexpr TaggedFile(LocationPack::RawType r)
    : raw(r) {
        check_limit(raw);
    }

    // Создание из index
    [[nodiscard]] static constexpr TaggedFile make_input(size_t idx) {
        EXPECT(idx + 1u < LocationPack::MAX_FILES_INPUT);
        return TaggedFile{static_cast<LocationPack::RawType>(idx + 1u)};
    }
    [[nodiscard]] static constexpr TaggedFile make_output(size_t idx) {
        EXPECT(idx + 1u < LocationPack::MAX_FILES_OUTPUT);
        return TaggedFile{static_cast<LocationPack::RawType>(idx + 1u) | LocationPack::OUTPUT_FILE_BIT};
    }

    [[nodiscard]] constexpr bool isInvalid() const {
        check_limit(raw);
        return raw == 0u;
    }
    [[nodiscard]] constexpr bool isOutput() const {
        check_limit(raw);
        return (raw & LocationPack::OUTPUT_FILE_BIT) != 0;
    }

    [[nodiscard]] constexpr LocationPack::RawType as_index() const {
        EXPECT(!isInvalid());
        return (raw & ~LocationPack::OUTPUT_FILE_BIT) - 1;
    }

    static constexpr void check_limit(LocationPack::RawType value) {
        // clang-format off
        EXPECT(((value & ~LocationPack::OUTPUT_FILE_BIT) < LocationPack::MAX_FILES_INPUT
                || ((value & ~LocationPack::OUTPUT_FILE_BIT) < LocationPack::MAX_FILES_OUTPUT && (value & LocationPack::OUTPUT_FILE_BIT)))
               && "file index out of range");
        // clang-format on
    }

    // Кросс-теговая конверсия: преобразует TaggedFile другого тега в текущий
    template <typename Tag2>
    [[nodiscard]] static constexpr TaggedFile from(const TaggedFile<Tag2>& other) {
        return TaggedFile{other.raw};
    }

    // Статический конструктор из сырого значения (для реализации)
    static constexpr TaggedFile fromRaw(LocationPack::RawType r) { return TaggedFile{r}; }

    friend constexpr bool operator==(const TaggedFile& a, const TaggedFile& b) { return a.raw == b.raw; }
    friend constexpr auto operator<=>(const TaggedFile& a, const TaggedFile& b) { return a.raw <=> b.raw; }

    // SourceMap<MapperFile> и SourceMap<ReaderFile> имеют доступ к raw
    template <typename F>
    friend struct SourceMap;
    friend class Context;
    friend class SourceMapWriter;
    template <typename Tag2>
    friend struct TaggedFile;
    template <typename Tag2>
    friend struct TaggedLocation;

  private:
    LocationPack::RawType raw = 0;
};

// ── Теги для tagged-типов ──
struct MapperFileTag {};
struct ReaderFileTag {};

using MapperFile = TaggedFile<MapperFileTag>;
using ReaderFile = TaggedFile<ReaderFileTag>;

// Concept: IsTaggedFile
template <typename T>
concept IsTaggedFile = std::is_same_v<T, MapperFile> || std::is_same_v<T, ReaderFile>;

template <typename FileIdx>
struct TaggedLocation {
    static_assert(IsTaggedFile<FileIdx>, "TaggedLocation<FileIdx> requires TaggedFile (MapperFile or ReaderFile)");

    using Location = TaggedLocation<FileIdx>;

    constexpr TaggedLocation()
    : packed(0) {}

    // Кросс-теговая конверсия: все TaggedLocation имеют одинаковый packed-формат,
    // разрешена только explicit конверсия (static_cast), т.к. теги разные.
    template <typename OtherTag>
    constexpr explicit TaggedLocation(const TaggedLocation<OtherTag>& other)
    : packed(other.packed) {}

    // Статический конструктор из упакованного значения (для реализации)
    [[nodiscard]] static constexpr TaggedLocation fromPacked(LocationPack::RawType p) { return TaggedLocation{p}; }

    // Статический конструктор от FileIdx + offset (для реализации)
    [[nodiscard]] static constexpr TaggedLocation makeLoc(FileIdx idx, size_t off) {
        if (idx.isOutput()) {
            EXPECT(off <= LocationPack::MAX_OFFSET_OUTPUT);
            LocationPack::RawType indexRaw = idx.raw & LocationPack::MAX_FILES_OUTPUT;
            return TaggedLocation{static_cast<LocationPack::RawType>((indexRaw << LocationPack::OFFSET_BITS_OUTPUT) | LocationPack::OUTPUT_FILE_BIT | off)};
        } else {
            EXPECT(off <= LocationPack::MAX_OFFSET_INPUT);
            return TaggedLocation{static_cast<LocationPack::RawType>((idx.raw << LocationPack::OFFSET_BITS_INPUT) | (off & LocationPack::MAX_OFFSET_INPUT))};
        }
    }

    [[nodiscard]] constexpr bool isInvalid() const { return packed == 0u; }
    [[nodiscard]] constexpr bool isOutput() const { return (packed & LocationPack::OUTPUT_FILE_BIT) != 0; }

    // Извлекает FileIdx (с флагом в бите 31)
    [[nodiscard]] constexpr FileIdx fileIdx() const {
        if (isOutput()) {
            LocationPack::RawType index = (packed >> LocationPack::OFFSET_BITS_OUTPUT) & LocationPack::MAX_FILES_OUTPUT;
            return FileIdx::fromRaw(index | LocationPack::OUTPUT_FILE_BIT);
        } else {
            LocationPack::RawType index = packed >> LocationPack::OFFSET_BITS_INPUT;
            return FileIdx::fromRaw(index);
        }
    }

    // Публичный доступ к упакованному значению (для SourceMapReader и маппингов)
    [[nodiscard]] constexpr LocationPack::RawType asPacked() const { return packed; }

    // Публичный доступ к offset (read-only) — смещение в файле (1-based)
    [[nodiscard]] constexpr LocationPack::RawType offset() const {
        if (isOutput())
            return packed & LocationPack::MAX_OFFSET_OUTPUT;
        else
            return packed & LocationPack::MAX_OFFSET_INPUT;
    }

    // ── Операторы сравнения (C++20 spaceship) — сравниваем (fileIdx, offset)
    friend constexpr bool operator==(const Location& a, size_t b) { return a.offset() == b; }
    friend constexpr bool operator==(size_t a, const Location& b) { return a == b.offset(); }
    friend constexpr auto operator<=>(const Location& a, size_t b) { return a.offset() <=> b; }
    friend constexpr auto operator<=>(size_t a, const Location& b) { return a <=> b.offset(); }

    // ── Арифметические операторы (offset + смещение)
    friend constexpr LocationPack::RawType operator+(const Location& loc, size_t val) { return loc.offset() + val; }
    friend constexpr LocationPack::RawType operator+(size_t val, const Location& loc) { return val + loc.offset(); }
    friend constexpr LocationPack::RawType operator-(const Location& loc, size_t val) {
        EXPECT(loc.offset() > val && "offset subtraction underflows");
        return loc.offset() - val;
    }

    friend constexpr bool operator==(const Location& a, const Location& b) { return a.fileIdx() == b.fileIdx() && a.offset() == b.offset(); }
    friend constexpr auto operator<=>(const Location& a, const Location& b) {
        if (auto cmp = a.fileIdx() <=> b.fileIdx(); cmp != 0)
            return cmp;
        return a.offset() <=> b.offset();
    }

    // Вложенный тип диапазона, параметризованный тем же тегом
    struct RangeType {

        RangeType(FileIdx idx, size_t beginOff, size_t endOff)
        : begin(Location::makeLoc(idx, beginOff))
        , end(Location::makeLoc(idx, endOff)) {
            EXPECT(beginOff <= endOff);
        }

        RangeType(Location b, Location e)
        : begin(b)
        , end(e) {
            EXPECT(b <= e);
        }

        [[nodiscard]] static RangeType point(Location loc) { return {loc, loc}; }
        [[nodiscard]] bool is_point() const { return begin == end; }
        [[nodiscard]] bool isInvalid() const { return begin.isInvalid() || end.isInvalid() || begin.fileIdx() != end.fileIdx() || begin > end; }

        friend constexpr bool operator==(const RangeType& a, const RangeType& b) { return a.begin == b.begin && a.end == b.end; }
        friend constexpr auto operator<=>(const RangeType& a, const RangeType& b) { return a.begin <=> b.begin; }

        RangeType() = default;
        Location begin{}, end{};
    };

    [[nodiscard]] Location inc(size_t size) const {
        FileIdx f = fileIdx();
        LocationPack::RawType off = offset() + static_cast<LocationPack::RawType>(size);
        return makeLoc(f, off);
    }
    [[nodiscard]] Location dec(size_t size) const {
        FileIdx f = fileIdx();
        LocationPack::RawType off = offset() - static_cast<LocationPack::RawType>(size);
        return makeLoc(f, off);
    }

  protected:
    LocationPack::RawType packed = 0;

    // Mapper<FileIdx> и Mapper<ReaderFileIdx> имеют доступ к packed
    template <typename F>
    friend struct SourceMap;
    // Другие инстанциации TaggedLocation имеют доступ к packed для кросс-теговой конверсии
    template <typename OtherTag>
    friend struct TaggedLocation;
    friend class Context;
    friend class SourceMapWriter;
    friend class SourceMapReader;

    // Конструктор от упакованного значения (для восстановления из packed)
    [[nodiscard]] constexpr explicit TaggedLocation(LocationPack::RawType p)
    : packed(p) {}

    // Конструктор от FileIdx + offset: упаковывает в packed
    constexpr TaggedLocation(FileIdx idx, size_t off)
    : packed(0) {
        EXPECT(!idx.isInvalid());
        if (idx.isOutput()) {
            EXPECT(off <= LocationPack::MAX_OFFSET_OUTPUT);
            LocationPack::RawType indexRaw = idx.raw & LocationPack::MAX_FILES_OUTPUT;
            packed = (indexRaw << LocationPack::OFFSET_BITS_OUTPUT) | LocationPack::OUTPUT_FILE_BIT | static_cast<LocationPack::RawType>(off);
        } else {
            EXPECT(off <= LocationPack::MAX_OFFSET_INPUT);
            packed = (idx.raw << LocationPack::OFFSET_BITS_INPUT) | (off & LocationPack::MAX_OFFSET_INPUT);
        }
    }
};

// ── Псевдонимы для двух пространств ──
using MapperLocation = TaggedLocation<MapperFile>::Location;
using MapperRange = TaggedLocation<MapperFile>::RangeType;

using ReaderLocation = TaggedLocation<ReaderFile>::Location;
using ReaderRange = TaggedLocation<ReaderFile>::RangeType;

} // namespace trust