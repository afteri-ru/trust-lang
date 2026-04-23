#pragma once

#include <string>
#include <string_view>
#include <optional>
#include <stdexcept>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <typeindex>

namespace trust {

enum class OutputFormat {
    Traditional, // #include-based (default)
    Cpp20Module, // export module (C++20)
    Cpp23Module, // export module (C++23, with improvements)
};

// Forward declaration
struct TypeRequirements;

// X-macro for type kinds
// V(Kind, cpp_type_str, cpp_type, min_format)
#define FOR_EACH_TYPE_KIND(V)                          \
    V(Int, "int", int, Traditional)                    \
    V(String, "std::string", std::string, Traditional) \
    V(None, "void", void, Traditional)                 \
    V(Bool, "bool", bool, Traditional)                 \
    V(Expected, "std::expected", void, Cpp23Module)    \
    V(UserType, "", void, Traditional)

enum class TypeKind {
#define TYPE_ENUM(name, _, __, ___) name,
    FOR_EACH_TYPE_KIND(TYPE_ENUM)
#undef TYPE_ENUM
        Void = None, // Backward-compat alias
};

// ---- Compile-time type mapping ----

// Primary trait: TypeKind → C++ type
template <TypeKind K>
struct type_kind_traits;

#define DEFINE_TRAIT(name, _, cpp_type, __)   \
    template <>                               \
    struct type_kind_traits<TypeKind::name> { \
        using type = cpp_type;                \
    };
FOR_EACH_TYPE_KIND(DEFINE_TRAIT)
#undef DEFINE_TRAIT

template <TypeKind K>
using kind_type_t = typename type_kind_traits<K>::type;

// Reverse mapping: C++ type → TypeKind (fails to instantiate for unmapped types)
template <typename T>
struct type_to_kind;

// Only real types get a reverse mapping (skip void-filled: None, Expected, UserType)
#define FOREACH_REAL_TYPE(V)                           \
    V(Int, "int", int, Traditional)                    \
    V(String, "std::string", std::string, Traditional) \
    V(Bool, "bool", bool, Traditional)

#define DEFINE_REVERSE_IMPL(name, _, cpp_type, __)        \
    template <>                                           \
    struct type_to_kind<cpp_type> {                       \
        static constexpr TypeKind value = TypeKind::name; \
    };

FOREACH_REAL_TYPE(DEFINE_REVERSE_IMPL)
#undef DEFINE_REVERSE_IMPL

template <typename T>
inline constexpr TypeKind type_to_kind_v = type_to_kind<T>::value;

// ---- Runtime type_id mapping ----

// Get std::type_index for a TypeKind (returns typeid(void) for UserType/Expected)
[[nodiscard]] std::type_index type_index_for(TypeKind k);

// Resolve std::type_index back to TypeKind (std::nullopt if unknown)
[[nodiscard]] std::optional<TypeKind> kind_from_type_index(const std::type_index &ti);

// ---- String conversion ----

// Convert TypeKind to its string representation (throws on unknown)
[[nodiscard]] std::string kind_name(TypeKind k);

// Convert string to TypeKind (std::nullopt if not a builtin kind)
[[nodiscard]] std::optional<TypeKind> kind_from_string(std::string_view s);

// ---- TypeRequirements — stores type requirements for headers, libs and format ----
struct TypeRequirements {
    TypeKind kind = TypeKind::UserType;
    OutputFormat min_format = OutputFormat::Traditional;
    std::vector<std::string> headers;
    std::vector<std::string> link_libs;

    TypeRequirements() = default;
    explicit TypeRequirements(TypeKind k) : kind(k) {}
    TypeRequirements(TypeKind k, OutputFormat fmt, std::vector<std::string> hdrs = {}, std::vector<std::string> libs = {})
        : kind(k), min_format(fmt), headers(std::move(hdrs)), link_libs(std::move(libs)) {}
};

// ---- TypeRequirementsRegistry ----
class TypeRequirementsRegistry {
  public:
    static TypeRequirementsRegistry &instance();

    const TypeRequirements &get(TypeKind kind) const;
    void register_type(TypeKind kind, TypeRequirements req);
    std::vector<std::string> collect_headers(const std::vector<TypeKind> &used_kinds) const;
    std::vector<std::string> collect_link_libs(const std::vector<TypeKind> &used_kinds) const;
    bool is_format_compatible(TypeKind kind, OutputFormat current_format) const;
    void init_builtins();

  private:
    TypeRequirementsRegistry() = default;
    std::unordered_map<TypeKind, TypeRequirements> registry_;
};

// ---- TypeInfo ----
struct TypeInfo {
    TypeKind kind;
    std::string user_type_name; // Used when kind == TypeKind::UserType

    static TypeInfo builtin(TypeKind k);
    static TypeInfo user(std::string name);

    [[nodiscard]] bool is_builtin() const;
    [[nodiscard]] bool is_user() const;
    [[nodiscard]] std::string to_string() const;
    [[nodiscard]] std::string to_cpp() const;

    [[nodiscard]] bool operator==(const TypeInfo &other) const;
    [[nodiscard]] bool operator!=(const TypeInfo &other) const;

    const TypeRequirements &requirements() const;

    static TypeInfo parse(const std::string &s);
};

} // namespace trust