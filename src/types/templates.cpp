#include "stdlib/buildin.hpp"
#include "types/templates.hpp"
#include "types/forward.hpp"
#include "types/types.hpp"

namespace trust {

void Vector::_register(Types& t) {
    auto& type = t.append(TypeInfo(TypeKind::Vector, "std::vector", LanguageVersion::CPP11));
    type.headers = {"<vector>"};
    type.param = {TypeKind::Any};
}

void Map::_register(Types& t) {
    auto& type = t.append(TypeInfo(TypeKind::Map, "std::map", LanguageVersion::CPP11));
    type.headers = {"<map>"};
    type.param = {TypeKind::Any, TypeKind::Any};
}

void MultiMap::_register(Types& t) {
    auto& type = t.append(TypeInfo(TypeKind::MultiMap, "std::multimap", LanguageVersion::CPP11));
    type.headers = {"<multimap>"};
    type.param = {TypeKind::Any, TypeKind::Any};
}

void Set::_register(Types& t) {
    auto& type = t.append(TypeInfo(TypeKind::Set, "std::set", LanguageVersion::CPP11));
    type.headers = {"<set>"};
    type.param = {TypeKind::Any};
}

void MultiSet::_register(Types& t) {
    auto& type = t.append(TypeInfo(TypeKind::MultiSet, "std::multiset", LanguageVersion::CPP11));
    type.headers = {"<multiset>"};
    type.param = {TypeKind::Any};
}
void Deque::_register(Types& t) {
    auto& type = t.append(TypeInfo(TypeKind::Deque, "std::deque", LanguageVersion::CPP11));
    type.headers = {"<deque>"};
    type.param = {TypeKind::Any, TypeKind::Any};
}

} // namespace trust