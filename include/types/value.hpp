#ifndef TYPES_VALUE_HPP
#define TYPES_VALUE_HPP

#include "types/forward.hpp"
#include <memory>
#include <string>

namespace trust {

class Value {
  public:
    virtual ~Value() = default;
    virtual std::unique_ptr<Value> clone() const = 0;
    virtual TypeKind kind() const = 0;
    virtual Value &convert_to(TypeKind target, Value &dest) const = 0;
    virtual std::string to_string(bool) const = 0;

  protected:
    static constexpr std::string_view value_name(TypeKind k) { return type_kind_name(k); }
};

template <typename Derived, TypeKind Kind>
class ValueBase : public Value {
  public:
    TypeKind kind() const override { return Kind; }

    std::unique_ptr<Value> clone() const override { return std::make_unique<Derived>(static_cast<const Derived &>(*this)); }

  protected:
    std::string format_label(bool with_type_info, std::string_view label, std::string_view raw) const {
        if (!with_type_info)
            return std::string{raw};
        return "[" + std::string{label} + "] " + std::string{raw};
    }

    ~ValueBase() override = default;
};

template <typename Derived, TypeKind Kind>
class SimpleValue : public ValueBase<Derived, Kind> {
  public:
    Value &convert_to(TypeKind target, Value &dest) const override {
        if (dest.kind() == target) {
            dest = static_cast<const Derived &>(*this);
        }
        return dest;
    }
};

class Void : public SimpleValue<Void, TypeKind::Void> {
  public:
    std::string to_string(bool with_type_info) const override { return format_label(with_type_info, "Void", "void"); }
    static void _register(Types &t);
};

} // namespace trust

#endif // TYPES_VALUE_HPP
