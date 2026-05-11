#include "types/strings.hpp"
#include "stdlib/category.hpp"
#include "types/types.hpp"

namespace trust {

void String::_register(Types& t) {
    t.add(TypeInfo(TypeKind::StrChar, "std::string"));
    t.add(TypeInfo(TypeKind::StrWide, "std::wstring"));
    t.add(TypeInfo(TypeKind::Strings, "std::string"));
    t.add_headers(TypeKind::Strings, {"<string>"});
}

} // namespace trust