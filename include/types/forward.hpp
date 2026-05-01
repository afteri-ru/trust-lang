#ifndef TYPES_FORWARD_HPP
#define TYPES_FORWARD_HPP

#include <string>
#include <variant>

#include "stdlib/buildin.hpp"
#include "diag/error.hpp"

namespace trust {

/* ── LanguageVersion X-macro ─────────────────────────── */
#define TRUST_LANGUAGE_VERSIONS \
    X(C, "c")                   \
    X(CPP11, "c++11")           \
    X(CPP17, "c++17")           \
    X(CPP20, "c++20")           \
    X(CPP23, "c++23")           \
    X(ModuleCPP20, "c++20")     \
    X(ModuleCPP23, "c++23")

enum class LanguageVersion : uint8_t {
#define X(name, str) name,
    TRUST_LANGUAGE_VERSIONS
#undef X
};

/* ── Stringification from X-macro ────────────────────── */
constexpr const char *language_version_string(LanguageVersion v) {
    switch (v) {
#define X(name, str)            \
    case LanguageVersion::name: \
        return str;
        TRUST_LANGUAGE_VERSIONS
#undef X
    }
    FAULT("Unknown LanguageVersion: {}", static_cast<uint8_t>(v));
}

/* ── Forward declarations ────────────────────────────── */
class Types;
class Value;
class Void;
class Integers;
class Float;
// class Complex;
class Rational;
class String;
class Tensor;
class SparseTensor;
class Vector;

using Any = std::variant<Void, Integers, Float, Rational, String, Tensor, SparseTensor, Vector>;

// ── Runtime conversion forward declarations ──
Any runtime_convert(const Any &val, TypeKind target);
std::string stringify_value(const Any &val, bool wi = false);

} // namespace trust

#endif