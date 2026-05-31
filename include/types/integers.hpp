#ifndef TYPES_INTEGER_HPP
#define TYPES_INTEGER_HPP

#include <cstdint>
#include <climits>
#include <string>

#include "types/category.hpp"
#include "types/forward.hpp"
#include "types/buildin.hpp"
#include "types/value.hpp"

namespace trust {

class Float;
class Complex;

class Integers : public ValueBase<Integers, TypeKind::Integers> {
  public:
    Integers()
    : value_(0)
    , real_kind_(TypeKind::Int64) {}
    Integers(int64_t v, TypeKind real)
    : value_(v)
    , real_kind_(real) {}

    TypeKind kind() const override { return real_kind_; }
    int64_t get() const { return value_; }
    void set(int64_t v) { value_ = v; }
    TypeKind real_kind() const { return real_kind_; }

    bool would_overflow_to(TypeKind t) const;

    Value& convert_to(TypeKind t, Value& dest) const override;

    std::string to_string(bool with_type_info) const override { return format_label(with_type_info, value_name(real_kind_), std::to_string(value_)); }
    static void _register(Types& t);

  private:
    int64_t value_;
    TypeKind real_kind_;
};

} // namespace trust

#endif // TYPES_INTEGER_HPP