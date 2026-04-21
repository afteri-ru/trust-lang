#include "types/type_info.hpp"

namespace trust {

TypeInfo TypeInfo::parse(const std::string &s) {
#define TYPE_CHECK(name, str, _) \
    if (s == str) \
        return TypeInfo::builtin(TypeKind::name);
    FOR_EACH_TYPE_KIND(TYPE_CHECK)
#undef TYPE_CHECK
    return TypeInfo::user(s);
}

std::string TypeInfo::to_string() const {
    if (is_user()) return user_type_name;
#define TYPE_TO_STR(name, str, _) \
    if (kind == TypeKind::name) return str;
    FOR_EACH_TYPE_KIND(TYPE_TO_STR)
#undef TYPE_TO_STR
    return "unknown";
}

std::string TypeInfo::to_cpp() const {
    if (is_user()) return user_type_name;
#define TYPE_TO_CPP(name, _, cpp) \
    if (kind == TypeKind::name) return cpp;
    FOR_EACH_TYPE_KIND(TYPE_TO_CPP)
#undef TYPE_TO_CPP
    return "auto";
}

} // namespace trust