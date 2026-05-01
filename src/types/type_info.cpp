#include "types/buildin.hpp"
#include "runtime/rational.hpp"
#include "runtime/tensor.hpp"
#include <cstring>

// Helper macro: map short format name to OutputFormat
#define __FORMAT(f) OutputFormat::Traditional

namespace trust {

// ---- Runtime type_id mapping ----

// NOTE: Only types with complete definitions are handled here.
// Vector types and TensorHandle need their full definitions.
std::type_index type_index_for(TypeKind k) {
#define TYPE_TO_TYPEINDEX(name, cpp_type, ___t, ____f) \
    case TypeKind::name:                               \
        return std::type_index(typeid(cpp_type));
    switch (k) {
        TYPE_TO_TYPEINDEX(Void, void, _, _)
        TYPE_TO_TYPEINDEX(Any, std::any, _, _)
        TYPE_TO_TYPEINDEX(Int, int, _, _)
        TYPE_TO_TYPEINDEX(Bool, bool, _, _)
        TYPE_TO_TYPEINDEX(Double, double, _, _)
        TYPE_TO_TYPEINDEX(Int64, int64_t, _, _)
        TYPE_TO_TYPEINDEX(String, std::string, _, _)
        TYPE_TO_TYPEINDEX(WString, std::wstring, _, _)
        TYPE_TO_TYPEINDEX(Rational, Rational, _, _)
        TYPE_TO_TYPEINDEX(VectorInt64, std::vector<int64_t>, _, _)
        TYPE_TO_TYPEINDEX(VectorDouble, std::vector<double>, _, _)
        TYPE_TO_TYPEINDEX(Tensor, TensorHandle, _, _)
        TYPE_TO_TYPEINDEX(Unsupported, void, _, _) // sentinel type
    case TypeKind::UserType:                       // user types — not type-erased
        break;
    }
#undef TYPE_TO_TYPEINDEX
    return std::type_index(typeid(void));
}

std::optional<TypeKind> kind_from_type_index(const std::type_index &ti) {
#define CHECK_TYPEINDEX(name, cpp_type)          \
    if (ti == std::type_index(typeid(cpp_type))) \
        return TypeKind::name;
    CHECK_TYPEINDEX(Void, void)
    CHECK_TYPEINDEX(Any, std::any)
    CHECK_TYPEINDEX(Int, int)
    CHECK_TYPEINDEX(Bool, bool)
    CHECK_TYPEINDEX(Double, double)
    CHECK_TYPEINDEX(Int64, int64_t)
    CHECK_TYPEINDEX(String, std::string)
    CHECK_TYPEINDEX(WString, std::wstring)
    CHECK_TYPEINDEX(Rational, Rational)
    CHECK_TYPEINDEX(VectorInt64, std::vector<int64_t>)
    CHECK_TYPEINDEX(VectorDouble, std::vector<double>)
    CHECK_TYPEINDEX(Tensor, TensorHandle)
#undef CHECK_TYPEINDEX
    return std::nullopt;
}

// --- TypeRequirementsRegistry ---

TypeRequirementsRegistry &TypeRequirementsRegistry::instance() {
    static TypeRequirementsRegistry inst;
    static bool initialized = false;
    if (!initialized) {
        inst.init_builtins();
        initialized = true;
    }
    return inst;
}

void TypeRequirementsRegistry::init_builtins() {
    // Initialize all type kinds with Traditional format (default minimum)
#define INIT_BI(k, t, c, f) registry_[TypeKind::k] = TypeRequirements(TypeKind::k, OutputFormat::Traditional);
    TRUST_TYPE_KINDS(INIT_BI)
#undef INIT_BI
}

const TypeRequirements &TypeRequirementsRegistry::get(TypeKind kind) const {
    auto it = registry_.find(kind);
    if (it != registry_.end()) {
        return it->second;
    }
    static const TypeRequirements empty_req;
    return empty_req;
}

void TypeRequirementsRegistry::register_type(TypeKind kind, TypeRequirements req) {
    req.kind = kind;
    registry_[kind] = std::move(req);
}

std::vector<std::string> TypeRequirementsRegistry::collect_headers(const std::vector<TypeKind> &used_kinds) const {
    std::vector<std::string> result;
    for (auto kind : used_kinds) {
        auto it = registry_.find(kind);
        if (it != registry_.end()) {
            for (const auto &hdr : it->second.headers) {
                if (std::find(result.begin(), result.end(), hdr) == result.end()) {
                    result.push_back(hdr);
                }
            }
        }
    }
    return result;
}

std::vector<std::string> TypeRequirementsRegistry::collect_link_libs(const std::vector<TypeKind> &used_kinds) const {
    std::vector<std::string> result;
    for (auto kind : used_kinds) {
        auto it = registry_.find(kind);
        if (it != registry_.end()) {
            for (const auto &lib : it->second.link_libs) {
                if (std::find(result.begin(), result.end(), lib) == result.end()) {
                    result.push_back(lib);
                }
            }
        }
    }
    return result;
}

bool TypeRequirementsRegistry::is_format_compatible(TypeKind kind, OutputFormat current_format) const {
    auto it = registry_.find(kind);
    if (it == registry_.end()) {
        return true;
    }
    return static_cast<int>(current_format) >= static_cast<int>(it->second.min_format);
}

// --- TypeInfo ---

TypeInfo TypeInfo::builtin(TypeKind k) {
    TypeInfo ti;
    ti.kind = k;
    ti.user_type_name = "";
    return ti;
}

TypeInfo TypeInfo::user(std::string name) {
    TypeInfo ti;
    ti.kind = TypeKind::UserType;
    ti.user_type_name = std::move(name);
    return ti;
}

bool TypeInfo::is_builtin() const {
    return kind != TypeKind::UserType;
}

bool TypeInfo::is_user() const {
    return kind == TypeKind::UserType;
}

TypeInfo TypeInfo::parse(const std::string &s) {
    if (auto k = kind_from_string(s))
        return TypeInfo::builtin(*k);
    return TypeInfo::user(s);
}

std::string TypeInfo::to_string() const {
    if (is_user())
        return user_type_name;
    return std::string(kind_name(kind));
}

std::string TypeInfo::to_cpp() const {
    if (is_user())
        return user_type_name;
#define TYPE_TO_CPP(name, cpp_type, ___, __) \
    if (kind == TypeKind::name)              \
        return #cpp_type;
    TRUST_TYPE_KINDS(TYPE_TO_CPP)
#undef TYPE_TO_CPP
    return "auto";
}

bool TypeInfo::operator==(const TypeInfo &other) const {
    return kind == other.kind && user_type_name == other.user_type_name;
}

bool TypeInfo::operator!=(const TypeInfo &other) const {
    return !(*this == other);
}

const TypeRequirements &TypeInfo::requirements() const {
    return TypeRequirementsRegistry::instance().get(kind);
}

} // namespace trust