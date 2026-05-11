#ifndef TYPES_FLOAT_HPP
#define TYPES_FLOAT_HPP

#include <cstdint>
#include <string>

#include "types/forward.hpp"
#include "types/value.hpp"

namespace trust {

class Integers;
class Complex;

class Float : public ValueBase<Float, TypeKind::Float64> {
  public:
    Float()
    : value_(0.0)
    , real_kind_(TypeKind::Float64) {}
    Float(double v, TypeKind real)
    : value_(v)
    , real_kind_(real) {}

    TypeKind kind() const override { return real_kind_; }
    double get() const { return value_; }
    void set(double v) { value_ = v; }
    TypeKind real_kind() const { return real_kind_; }

    bool would_overflow_to(TypeKind t) const;
    bool would_overflow_to_int(TypeKind t) const;

    Value& convert_to(TypeKind t, Value& dest) const override;

    std::string to_string(bool with_type_info) const override { return format_label(with_type_info, value_name(real_kind_), std::to_string(value_)); }
    static void _register(Types& t);

  private:
    double value_;
    TypeKind real_kind_;
};

} // namespace trust

#endif // TYPES_FLOAT_HPP