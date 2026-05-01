#include "types/types.hpp"
#include "stdlib/category.hpp"
#include <format>
#include <string>

namespace trust {

void Void::_register(Types &t) {
    t.add(TypeInfo(TypeKind::Void, "void"));
    t.add(TypeInfo(TypeKind::Any, "auto"));
}
std::string TypeInfo::to_string(bool full) {
    std::string result(type_kind_name(id));
    if (!full) {
        return result;
    }
    result += std::format(" cpp_name='{}' ", cpp_name);
    return result;
}

void Types::register_types() {
    Void::_register(*this);
    Integers::_register(*this);
    Float::_register(*this);
    // Complex::_register(*this);
    Rational::_register(*this);
    String::_register(*this);
    Tensor::_register(*this);
    SparseTensor::_register(*this);
    Vector::_register(*this);
    Map::_register(*this);
    Deque::_register(*this);
    Set::_register(*this);
    MultiMap::_register(*this);
    MultiSet::_register(*this);
}

} // namespace trust