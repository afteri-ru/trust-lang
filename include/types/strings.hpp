#ifndef TYPES_STRING_HPP
#define TYPES_STRING_HPP

#include <string>

#include "types/forward.hpp"
#include "types/value.hpp"

namespace trust {

class String : public ValueBase<String, TypeKind::StrChar> {
  public:
    String()
    : value_("")
    , real_kind_(TypeKind::StrChar) {}
    String(std::string v, TypeKind real)
    : value_(std::move(v))
    , real_kind_(real) {}

    TypeKind kind() const override { return real_kind_; }
    const std::string& get() const { return value_; }
    void set(std::string v) { value_ = std::move(v); }
    TypeKind real_kind() const { return real_kind_; }

    Value& convert_to(TypeKind, Value& dest) const override {
        (void)dest;
        throw std::invalid_argument("Use Types::convert() instead");
    }

    std::string to_string(bool with_type_info) const override {
        std::string val = "\"" + value_ + "\"";
        if (with_type_info) {
            return "[" + std::string{value_name(real_kind_)} + "] " + val;
        }
        return val;
    }
    static void _register(Types& t);

  private:
    std::string value_;
    TypeKind real_kind_;
};

} // namespace trust

#endif // TYPES_STRING_HPP