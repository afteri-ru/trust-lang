#pragma once

#include "utils/error.hpp"
#include <cstdint>
#include <cstddef>
#include <type_traits>

namespace trust {

// ── Константы формата упаковки TaggedLocation ──
// Занимает sizeof(uint32_t) байт.
// Упаковка: bits 31-20: RawType.file   (12 бит, 2047 файлов + флаг входа/выхода)
//           bits 19-0:  RawType.offset (20 бит, ~1MB на файл)
struct LocationPack {
    using RawType = uint32_t;

    static constexpr int FILE_BITS = 11;
    static constexpr int MAX_FILES = (1 << FILE_BITS) - 1;
    static constexpr int OUTPUT_FILE_BIT = 1u << (FILE_BITS); // признак выходного файла в TaggedFile

    static constexpr int OFFSET_BITS = 20;
    static constexpr int MAX_OFFSET = (1 << OFFSET_BITS) - 1;
    // static constexpr int MACRO_OFFSET = (1 << OFFSET_BITS) - 1;

    // признак выходного файла в TaggedLocation
    // OUTPUT_FILE_BIT (флаг выходного файла) при сдвиге на 20 оказывается в бите 31.
    static constexpr LocationPack::RawType OUTPUT_LOCATION_BIT = 1u << (FILE_BITS + OFFSET_BITS);
};

static_assert(LocationPack::FILE_BITS + LocationPack::OFFSET_BITS + 1 == 8 * sizeof(int));
static_assert(LocationPack::MAX_FILES == 2'047);
static_assert(LocationPack::MAX_OFFSET == 1'048'575);

// TaggedFile — шаблонный идентификатор файла (входного или выходного).
//   raw = 0         — невалидный
//   raw > 0, бит OUTPUT_FILE_BIT = 0 — входной: raw = index + 1
//   raw > 0, бит OUTPUT_FILE_BIT = 1 — выходной: raw = (index + 1) | (1 << FILE_BITS)
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
        EXPECT(idx + 1u < LocationPack::MAX_FILES);
        return TaggedFile{static_cast<LocationPack::RawType>(idx + 1u)};
    }
    [[nodiscard]] static constexpr TaggedFile make_output(size_t idx) {
        EXPECT(idx + 1u < LocationPack::MAX_FILES);
        return TaggedFile{static_cast<LocationPack::RawType>(idx + 1u) | LocationPack::OUTPUT_FILE_BIT};
    }

    [[nodiscard]] constexpr bool isValid() const {
        check_limit(raw);
        return raw != 0u;
    }
    [[nodiscard]] constexpr bool isOutput() const {
        check_limit(raw);
        return (raw & LocationPack::OUTPUT_FILE_BIT);
    }

    [[nodiscard]] constexpr LocationPack::RawType as_index() const {
        EXPECT(isValid());
        return (raw & LocationPack::MAX_FILES) - 1;
    }

    static constexpr void check_limit(LocationPack::RawType value) {
        // clang-format off
        EXPECT((value & !(LocationPack::OUTPUT_FILE_BIT | LocationPack::MAX_FILES)) == 0);
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
    friend constexpr bool operator!=(const TaggedFile& a, const TaggedFile& b) { return !(a == b); }
    friend constexpr bool operator<(const TaggedFile& a, const TaggedFile& b) { return a.raw < b.raw; }
    friend constexpr bool operator>(const TaggedFile& a, const TaggedFile& b) { return a.raw > b.raw; }
    friend constexpr bool operator<=(const TaggedFile& a, const TaggedFile& b) { return a.raw <= b.raw; }
    friend constexpr bool operator>=(const TaggedFile& a, const TaggedFile& b) { return a.raw >= b.raw; }

    // SourceMap<MapperFile> и SourceMap<ReaderFile> имеют доступ к raw
    template <typename F>
    friend struct SourceMap;
    friend class Context;
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

    // static TaggedLocation invalid() { return TaggedLocation{0u}; }

    // Кросс-теговая конверсия: все TaggedLocation имеют одинаковый packed-формат,
    // разрешена только explicit конверсия (static_cast), т.к. теги разные.
    template <typename OtherTag>
    constexpr explicit TaggedLocation(const TaggedLocation<OtherTag>& other)
    : packed(other.packed) {}

    // Статический конструктор из упакованного значения (для реализации)
    [[nodiscard]] static constexpr TaggedLocation fromPacked(LocationPack::RawType p) { return TaggedLocation{p}; }

    // Статический конструктор от FileIdx + offset (для реализации)
    [[nodiscard]] static constexpr TaggedLocation makeLoc(FileIdx idx, size_t off) {
        EXPECT(off <= LocationPack::MAX_OFFSET);
        return TaggedLocation{idx, off};
    }

    [[nodiscard]] constexpr bool isValid() const { return packed != 0u; }
    [[nodiscard]] constexpr bool isOutput() const { return fileIdx().isOutput(); }

    // Извлекает FileIdx (с флагом в бите FILE_BITS)
    [[nodiscard]] constexpr FileIdx fileIdx() const { return FileIdx::fromRaw(packed >> LocationPack::OFFSET_BITS); }

    // Публичный доступ к упакованному значению (для SourceMapReader и маппингов)
    [[nodiscard]] constexpr LocationPack::RawType asPacked() const { return packed; }

    // Публичный доступ к offset (read-only) — смещение в файле (1-based)
    [[nodiscard]] constexpr LocationPack::RawType offset() const { return packed & LocationPack::MAX_OFFSET; }

    // ── Операторы сравнения
    friend constexpr bool operator==(const Location& a, size_t b) { return a.offset() == b; }
    friend constexpr bool operator==(size_t a, const Location& b) { return a == b.offset(); }
    friend constexpr bool operator!=(const Location& a, size_t b) { return a.offset() != b; }
    friend constexpr bool operator!=(size_t a, const Location& b) { return a != b.offset(); }
    friend constexpr bool operator<(const Location& a, size_t b) { return a.offset() < b; }
    friend constexpr bool operator<(size_t a, const Location& b) { return a < b.offset(); }
    friend constexpr bool operator>(const Location& a, size_t b) { return a.offset() > b; }
    friend constexpr bool operator>(size_t a, const Location& b) { return a > b.offset(); }
    friend constexpr bool operator<=(const Location& a, size_t b) { return a.offset() <= b; }
    friend constexpr bool operator<=(size_t a, const Location& b) { return a <= b.offset(); }
    friend constexpr bool operator>=(const Location& a, size_t b) { return a.offset() >= b; }
    friend constexpr bool operator>=(size_t a, const Location& b) { return a >= b.offset(); }

    // ── Арифметические операторы (offset + смещение)
    friend constexpr LocationPack::RawType operator+(const Location& loc, size_t val) { return loc.offset() + val; }
    friend constexpr LocationPack::RawType operator+(size_t val, const Location& loc) { return val + loc.offset(); }
    friend constexpr LocationPack::RawType operator-(const Location& loc, size_t val) {
        EXPECT(loc.offset() > val);
        return loc.offset() - val;
    }

    friend constexpr bool operator==(const Location& a, const Location& b) {
        EXPECT(a.fileIdx() == b.fileIdx());
        return a.packed == b.packed;
    }
    friend constexpr bool operator!=(const Location& a, const Location& b) {
        EXPECT(a.fileIdx() == b.fileIdx());
        return !(a == b);
    }
    friend constexpr bool operator<(const Location& a, const Location& b) {
        EXPECT(a.fileIdx() == b.fileIdx());
        return a.packed < b.packed;
    }
    friend constexpr bool operator>(const Location& a, const Location& b) {
        EXPECT(a.fileIdx() == b.fileIdx());
        return a.packed > b.packed;
    }
    friend constexpr bool operator<=(const Location& a, const Location& b) {
        EXPECT(a.fileIdx() == b.fileIdx());
        return a.packed <= b.packed;
    }
    friend constexpr bool operator>=(const Location& a, const Location& b) {
        EXPECT(a.fileIdx() == b.fileIdx());
        return a.packed >= b.packed;
    }

    // Вложенный тип диапазона, параметризованный тем же тегом
    struct RangeType {

        RangeType(Location b, Location e)
        : begin(b)
        , end(e) {
            EXPECT(is_valid());
        }

        [[nodiscard]] static RangeType point(Location loc) { return {loc, loc}; }
        [[nodiscard]] bool is_point() const { return begin.packed == end.packed; }
        [[nodiscard]] bool is_valid() { return begin.isValid() && end.isValid() && begin.fileIdx() == end.fileIdx() && begin <= end; }

        friend constexpr bool operator==(const RangeType& a, const RangeType& b) { return a.begin == b.begin && a.end == b.end; }
        friend constexpr bool operator!=(const RangeType& a, const RangeType& b) { return !(a == b); }
        friend constexpr bool operator<(const RangeType& a, const RangeType& b) { return a.begin < b.begin; }
        friend constexpr bool operator>(const RangeType& a, const RangeType& b) { return a.begin > b.begin; }
        friend constexpr bool operator<=(const RangeType& a, const RangeType& b) { return a.begin <= b.begin; }
        friend constexpr bool operator>=(const RangeType& a, const RangeType& b) { return a.begin >= b.begin; }

        RangeType() = default;
        Location begin{}, end{};
    };

    [[nodiscard]] Location inc(size_t size) const {
        EXPECT(size < LocationPack::MAX_OFFSET);
        EXPECT(packed + size < LocationPack::MAX_OFFSET);
        return Location{packed + static_cast<LocationPack::RawType>(size)};
    }
    [[nodiscard]] Location dec(size_t size) const {
        EXPECT(packed > size);
        return Location{packed - static_cast<LocationPack::RawType>(size)};
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
    friend class SourceMapReader;

    // Конструктор от упакованного значения (для восстановления из packed)
    [[nodiscard]] constexpr explicit TaggedLocation(LocationPack::RawType p)
    : packed(p) {}

    // Конструктор от FileIdx + offset: упаковывает в packed
    // FileIdx.raw хранится целиком (с флагом в бите 11),
    // при сдвиге на OFFSET_BITS бит 11 → бит 31.
    constexpr TaggedLocation(FileIdx idx, size_t off)
    : packed(0) {
        EXPECT(idx.isValid());
        packed = (idx.raw << LocationPack::OFFSET_BITS) | (off & LocationPack::MAX_OFFSET);
    }
};

// ── Псевдонимы для двух пространств ──
using MapperLocation = TaggedLocation<MapperFile>::Location;
using MapperRange = TaggedLocation<MapperFile>::RangeType;

using ReaderLocation = TaggedLocation<ReaderFile>::Location;
using ReaderRange = TaggedLocation<ReaderFile>::RangeType;

} // namespace trust