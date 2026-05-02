#pragma once

#include <cstdint>
#include <cstddef>

namespace trust {

// FileIdx — идентификатор файла (входного или выходного).
//   raw = 0         — невалидный
//   raw > 0, бит FILEIDX_BITS = 0 — входной: raw = index + 1
//   raw > 0, бит FILEIDX_BITS = 1 — выходной: raw = (index + 1) | OUTPUT_FLAG
struct FileIdx {
    static constexpr uint32_t FILEIDX_BITS = 11;
    uint32_t raw = 0;

    constexpr bool isOutput() const {
        return (raw & (1u << FILEIDX_BITS)) != 0u;
    }
};

// Упакованная позиция: (fileIdx.raw без старшего бита) << FILEIDX_SHIFT | offset.
// Старший бит (31) — флаг выходного файла (OUTPUT_FLAG).
// Занимает sizeof(int) байт.
//
// Упаковка для входного файла:
//   bits 30-20: (fileIdx.raw)        (FILEIDX_BITS бит, 2047 файлов)
//   bits 19-0:  offset               (OFFSET_BITS бит, ~1MB на файл)
//
// Упаковка для выходного файла:
//   bits 31:    OUTPUT_FLAG
//   bits 30-20: (fileIdx.raw без OUTPUT_FLAG)  (FILEIDX_BITS бит, 2047 файлов)
//   bits 19-0:  offset                          (OFFSET_BITS бит, ~1MB на файл)
struct SourceLoc {
static constexpr int OFFSET_BITS  = 20;
static constexpr int FILEIDX_BITS = FileIdx::FILEIDX_BITS;
static constexpr int FILEIDX_SHIFT = OFFSET_BITS; // = 20
static constexpr uint32_t OUTPUT_FLAG  = 1u << 31;
static constexpr int MAX_OFFSET   = (1 << OFFSET_BITS) - 1;

uint32_t packed = 0;

constexpr SourceLoc() : packed(0) {}

static SourceLoc invalid() { return SourceLoc{0u}; }

// Конструктор от упакованного значения (для восстановления из packed)
constexpr SourceLoc(uint32_t p) : packed(p) {}

// Конструктор от FileIdx + offset: упаковывает в packed
constexpr SourceLoc(FileIdx idx, int off) : packed(0) {
    if (idx.raw == 0) return;
    uint32_t base = idx.raw;
    uint32_t flag = (base & (1u << FileIdx::FILEIDX_BITS)) ? OUTPUT_FLAG : 0u;
    base &= ~(1u << FileIdx::FILEIDX_BITS);
    packed = flag | (base << FILEIDX_SHIFT) | static_cast<uint32_t>(off & MAX_OFFSET);
}

constexpr bool isValid() const { return packed != 0u; }
constexpr bool isOutput() const { return (packed & OUTPUT_FLAG) != 0u; }

// Извлекает FileIdx (с сохранением OUTPUT_FLAG для выходных)
constexpr FileIdx fileIdx() const {
    uint32_t base = (packed & ~OUTPUT_FLAG) >> FILEIDX_SHIFT; // 1-based, без флага
    uint32_t flag = (packed & OUTPUT_FLAG) ? (1u << FILEIDX_BITS) : 0u;
    return FileIdx{base | flag};
}

constexpr int offset() const { return static_cast<int>(packed & MAX_OFFSET); }
constexpr SourceLoc inc(size_t size) const {
    return SourceLoc{packed + static_cast<uint32_t>(size)};
}
constexpr SourceLoc dec(size_t size) const {
    return SourceLoc{packed - static_cast<uint32_t>(size)};
}
};

inline constexpr SourceLoc SourceLoc_invalid_const{};

static_assert(sizeof(SourceLoc) == sizeof(int), "SourceLoc must be sizeof(int)");
static_assert(SourceLoc::MAX_OFFSET == (1 << SourceLoc::OFFSET_BITS) - 1, "MAX_OFFSET must fit in bit field");
static_assert(SourceLoc::FILEIDX_SHIFT <= 31, "FILEIDX_SHIFT must not exceed sign bit");
static_assert(SourceLoc_invalid_const.packed == 0u, "invalid must have packed == 0");
static_assert(!SourceLoc_invalid_const.isValid(), "invalid must return false for isValid()");

// Диапазон. begin == end означает точечную позицию (is_point() == true).
struct SourceRange {
    SourceLoc begin{}, end{};

    static SourceRange point(SourceLoc loc) { return {loc, loc}; }
    bool is_point() const { return begin.packed == end.packed; }
};

} // namespace trust