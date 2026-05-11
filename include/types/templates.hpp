#ifndef TYPES_TEMPLATE_HPP
#define TYPES_TEMPLATE_HPP

#include <cstddef>
#include <string>
#include <vector>
#include <deque>
#include <map>
#include <set>

#include "types/forward.hpp"
#include "types/value.hpp"

namespace trust {

class Vector : public ValueBase<Vector, TypeKind::Vector> {
  public:
    Vector()
    : real_kind_(TypeKind::Int64) {}
    Vector(std::vector<int64_t> v, TypeKind real)
    : data_(std::move(v))
    , real_kind_(real) {}

    const std::vector<int64_t>& get() const { return data_; }
    TypeKind element_kind() const { return real_kind_; }
    size_t size() const { return data_.size(); }

    Value& convert_to(TypeKind, Value& dest) const override {
        (void)dest;
        throw std::invalid_argument("Use Types::convert() instead");
    }

    std::string to_string(bool with_type_info) const override {
        std::string r;
        if (with_type_info) {
            r = "[" + std::string{value_name(kind())} + "] {";
        } else {
            r = "{";
        }
        for (size_t i = 0; i < data_.size(); ++i) {
            if (i)
                r += ", ";
            r += std::to_string(data_[i]);
        }
        return r + "}";
    }
    static void _register(Types& t);

  private:
    std::vector<int64_t> data_;
    TypeKind real_kind_;
};

class Map : public SimpleValue<Map, TypeKind::Map> {
  public:
    Map() = default;
    std::string to_string(bool with_type_info) const override { return format_label(with_type_info, "Map", "Map{}"); }
    static void _register(Types& t);
};

class Deque : public SimpleValue<Deque, TypeKind::Deque> {
  public:
    Deque() = default;
    std::string to_string(bool with_type_info) const override { return format_label(with_type_info, "Deque", "Deque{}"); }
    static void _register(Types& t);
};

class Set : public SimpleValue<Set, TypeKind::Set> {
  public:
    Set() = default;
    std::string to_string(bool with_type_info) const override { return format_label(with_type_info, "Set", "Set{}"); }
    static void _register(Types& t);
};

class MultiMap : public SimpleValue<MultiMap, TypeKind::MultiMap> {
  public:
    MultiMap() = default;
    std::string to_string(bool with_type_info) const override { return format_label(with_type_info, "MultiMap", "MultiMap{}"); }
    static void _register(Types& t);
};

class MultiSet : public SimpleValue<MultiSet, TypeKind::MultiSet> {
  public:
    MultiSet() = default;
    std::string to_string(bool with_type_info) const override { return format_label(with_type_info, "MultiSet", "MultiSet{}"); }
    static void _register(Types& t);
};

} // namespace trust

#endif // TYPES_TEMPLATE_HPP