#ifndef TYPES_BUILDIN_HPP
#define TYPES_BUILDIN_HPP

#include "types/category.hpp"
#include "utils/error.hpp"

#include <cstdint>
#include <string_view>

namespace trust {

/* ── TypeKind X-macros ─────────────────────────────────── */
#define TRUST_TYPEKINDS                               \
    X(Void, Void, 0, 0)                               \
                                                      \
    X(Int8, Integers, SubInteger::Ix, 0)              \
    X(Int16, Integers, SubInteger::Ix, 1)             \
    X(Int32, Integers, SubInteger::Ix, 2)             \
    X(Int64, Integers, SubInteger::Ix, 3)             \
    X(Bool, Integers, SubInteger::Bool, 0)            \
                                                      \
    X(Float16, Numbers, SubFloat::Fx, 0)              \
    X(Float32, Numbers, SubFloat::Fx, 1)              \
    X(Float64, Numbers, SubFloat::Fx, 2)              \
                                                      \
    X(Rational, Rationals, 0, 0)                      \
    X(RationalAny, Rationals, 1, 0)                   \
                                                      \
    X(StrChar, Strings, SubString::Char, 0)           \
    X(StrWide, Strings, SubString::Wide, 0)           \
                                                      \
    X(DenseTensor, Tensors, SubTensor::Dense, 0)      \
    X(SparseTensor, Tensors, SubTensor::Sparse, 0)    \
    /*        */                                      \
    X(Vector, Templates, SubContainer::Vector, 0)     \
    X(Map, Templates, SubContainer::Map, 0)           \
    X(Deque, Templates, SubContainer::Deque, 0)       \
    X(Set, Templates, SubContainer::Set, 0)           \
    X(MultiMap, Templates, SubContainer::MultiMap, 0) \
    X(MultiSet, Templates, SubContainer::MultiSet, 0)

#define TRUST_TYPEKINDS_ALIAS \
    /*      */                \
    X(Any, Void, 1)           \
    /*      */                \
    X(Char, Int8, 1)          \
    X(Byte, Int8, 2)          \
    X(Word, Int16, 1)         \
    X(DWord, Int32, 1)        \
    X(DDWord, Int64, 1)       \
    X(Integers, Int64, 2)     \
    /*      */                \
    X(Single, Float32, 1)     \
    X(Double, Float64, 1)     \
    X(Numbers, Float64, 2)    \
    /*      */                \
    X(Strings, StrChar, 1)    \
    /*      */                \
    X(Tensors, DenseTensor, 1)

/* ── TypeKind enum generated from X-macro ──────────────── */
enum class TypeKind : type_kind_t {
#define X(name, cat, group, idx) name = KindOps::make_kind_raw(Category::cat, group, static_cast<type_index_t>(idx)),
    TRUST_TYPEKINDS
#undef X
#define X(name, base_kind, alias_val) \
    name = KindOps::make_kind_alias_static(static_cast<type_kind_t>(TypeKind::base_kind), static_cast<type_alias_t>(alias_val)),
        TRUST_TYPEKINDS_ALIAS
#undef X
};

/* ── Stringification generated from X-macro ────────────── */
constexpr std::string_view type_kind_name(TypeKind k) {
    switch (k) {
#define X(name, ...)     \
    case TypeKind::name: \
        return #name;
        TRUST_TYPEKINDS
        TRUST_TYPEKINDS_ALIAS
#undef X
    }
    // ============================================================================
    // VERY IMPORTANT!!!
    // Do not delete this comment!
    // Do not delete or edit following static_assert or FAULT!
    // Stop and ask if you encounter a problem, but under no circumstances change the macro expansion or this comment!
    static_assert(sizeof(uint64_t) == sizeof(TypeKind));
    FAULT("Unknown TypeKind: {:#0}", static_cast<uint64_t>(k));
}

} // namespace trust

#endif