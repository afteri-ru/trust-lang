#pragma once

#include "diag/location.hpp"
#include <string>
#include <stdexcept>

namespace trust {

// X-macro for type kinds
// Usage: FOR_EACH_TYPE_KIND(FIELD) generates type_name, cpp_type, etc.
#define FOR_EACH_TYPE_KIND(V)          \
    V(Int, "int", "int")               \
    V(String, "string", "std::string") \
    V(Void, "void", "void")            \
    V(Bool, "bool", "bool")            \
    V(UserType, "usertype", "")

enum class TypeKind {
#define TYPE_ENUM(name, _, __) name,
    FOR_EACH_TYPE_KIND(TYPE_ENUM)
#undef TYPE_ENUM
};

// TypeInfo — wrapper for type information (builtin or user-defined)
struct TypeInfo {
    TypeKind kind;
    std::string user_type_name; // Used when kind == TypeKind::UserType

    static TypeInfo builtin(TypeKind k) { TypeInfo ti; ti.kind = k; ti.user_type_name = ""; return ti; }
    static TypeInfo user(std::string name) { TypeInfo ti; ti.kind = TypeKind::UserType; ti.user_type_name = std::move(name); return ti; }

    bool is_builtin() const { return kind != TypeKind::UserType; }
    bool is_user() const { return kind == TypeKind::UserType; }
    [[nodiscard]] std::string to_string() const;
    [[nodiscard]] std::string to_cpp() const;

    [[nodiscard]] bool operator==(const TypeInfo &other) const noexcept { return kind == other.kind && user_type_name == other.user_type_name; }
    [[nodiscard]] bool operator!=(const TypeInfo &other) const noexcept { return !(*this == other); }

    // Parse a type string (e.g., "int", "string", "MyEnum") into TypeInfo
    [[nodiscard]] static TypeInfo parse(const std::string &s);
};

} // namespace trust