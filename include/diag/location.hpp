#pragma once

#include <cassert>
#include <cstdint>
#include <cstddef>
#include <stdexcept>

namespace trust {

struct SourceIdx {
    int raw = -1;

    static constexpr SourceIdx make(int i) { return {i}; }
    static constexpr SourceIdx none()      { return {-1}; }
    constexpr bool isValid() const         { return raw >= 0; }
};

// Упакованная позиция: (source_idx + 1) << OFFSET_BITS | offset.
// Занимает sizeof(int) байт. Offset ограничен MAX_OFFSET (~4 МБ).
struct SourceLoc {
    static constexpr int OFFSET_BITS = 22;
    static constexpr int SOURCE_SHIFT = OFFSET_BITS;
    static constexpr int MAX_OFFSET = (1 << OFFSET_BITS) - 1;

    int packed = 0;

    static SourceLoc invalid() { return {0}; }

    static constexpr SourceLoc make(SourceIdx idx, int offset) {
        return {((idx.raw + 1) << SOURCE_SHIFT) | offset};
    }

    constexpr bool isValid() const          { return packed != 0; }
    constexpr SourceIdx source_idx() const  { return SourceIdx::make((packed >> SOURCE_SHIFT) - 1); }
    constexpr int offset() const            { return packed & MAX_OFFSET; }
    constexpr SourceLoc inc(size_t size) const {
        assert(offset() + size < MAX_OFFSET);
        return SourceLoc{packed + static_cast<int>(size)};
    }
};

inline constexpr SourceLoc SourceLoc_invalid_const = {0};

static_assert(sizeof(SourceLoc) == sizeof(int), "SourceLoc must be sizeof(int)");
static_assert(SourceLoc::MAX_OFFSET < (1 << SourceLoc::OFFSET_BITS), "MAX_OFFSET must fit in bit field");
static_assert(SourceLoc::SOURCE_SHIFT <= 31, "SOURCE_SHIFT must not exceed sign bit");
static_assert(SourceLoc_invalid_const.packed == 0, "invalid must have packed == 0");
static_assert(!SourceLoc_invalid_const.isValid(), "invalid must return false for isValid()");

// Диапазон. begin == end означает точечную позицию (is_point() == true).
struct SourceRange {
    trust::SourceLoc begin{}, end{};

    static SourceRange point(trust::SourceLoc loc) { return {loc, loc}; }
    bool is_point() const { return begin.packed == end.packed; }
};

class ParserError : public std::runtime_error {
public:
    SourceLoc location;

    explicit ParserError(const char* msg, SourceLoc loc = {})
        : std::runtime_error(msg), location(loc) {}
};

} // namespace trust