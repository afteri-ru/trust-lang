#include "types/type_info.hpp"
#include <cstring>

namespace trust {

// ---- Runtime type_id mapping ----

std::type_index type_index_for(TypeKind k) {
#define TYPE_TO_TYPEINDEX(name, _, cpp_type, __) \
    case TypeKind::name:                         \
        return std::type_index(typeid(cpp_type));
    switch (k) { FOR_EACH_TYPE_KIND(TYPE_TO_TYPEINDEX) }
#undef TYPE_TO_TYPEINDEX
    return std::type_index(typeid(void));
}

std::optional<TypeKind> kind_from_type_index(const std::type_index &ti) {
#define CHECK_TYPEINDEX(name, _, cpp_type, __)   \
    if (ti == std::type_index(typeid(cpp_type))) \
        return TypeKind::name;
    FOREACH_REAL_TYPE(CHECK_TYPEINDEX)
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
#define INIT_BUILTIN(name, _, __, fmt)                           \
    {                                                            \
        TypeRequirements req(TypeKind::name, OutputFormat::fmt); \
        registry_[TypeKind::name] = req;                         \
    }
    FOR_EACH_TYPE_KIND(INIT_BUILTIN)
#undef INIT_BUILTIN
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

std::string kind_name(TypeKind k) {
    switch (k) {
#define CASE_NAME(name, _, __, ___) \
    case TypeKind::name:            \
        return #name;
        FOR_EACH_TYPE_KIND(CASE_NAME)
#undef CASE_NAME
    }
    throw std::invalid_argument("Unknown TypeKind");
}

std::optional<TypeKind> kind_from_string(std::string_view s) {
    if (s.empty())
        return std::nullopt;
#define CHECK_NAME(name, _, __, ___) \
    if (s == #name)                  \
        return TypeKind::name;
    FOR_EACH_TYPE_KIND(CHECK_NAME)
#undef CHECK_NAME
    return std::nullopt;
}

TypeInfo TypeInfo::parse(const std::string &s) {
    if (auto k = kind_from_string(s))
        return TypeInfo::builtin(*k);
    return TypeInfo::user(s);
}

std::string TypeInfo::to_string() const {
    if (is_user())
        return user_type_name;
    return kind_name(kind);
}

std::string TypeInfo::to_cpp() const {
    if (is_user())
        return user_type_name;
#define TYPE_TO_CPP(name, cpp, __, ___) \
    if (kind == TypeKind::name)         \
        return cpp;
    FOR_EACH_TYPE_KIND(TYPE_TO_CPP)
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