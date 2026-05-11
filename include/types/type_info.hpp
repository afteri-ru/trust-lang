#ifndef TYPES_TYPE_INFO_HPP
#define TYPES_TYPE_INFO_HPP

#include "types/forward.hpp"

#include <string>
#include <string_view>
#include <vector>

#include "stdlib/buildin.hpp"

namespace trust {

// ---- TypeInfo ----
struct TypeInfo {
    TypeKind id;
    std::string cpp_name;
    std::vector<std::string> headers, libraries;
    LanguageVersion min_version;
    std::vector<TypeKind> param;

    TypeInfo() = default;
    TypeInfo(TypeKind i, std::string_view cpp, LanguageVersion ver = LanguageVersion::CPP11)
    : id(i)
    , cpp_name(cpp)
    , min_version(ver) {}

    std::string to_string(bool short_mode = true);
};

} // namespace trust

#endif // TYPES_TYPE_INFO_HPP