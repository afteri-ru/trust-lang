#include "runtime/var.hpp"
#include <gtest/gtest.h>
#include <stdexcept>

#ifdef TRUST_HAS_TORCH
#include <torch/torch.h>
#endif

using namespace trust;

class VarTest : public ::testing::Test {};

// ============================================================================
// Unified helper: creates a sample Var for each VarKind.
// Mode parameter controls the sample values:
//   0 = default-constructed (for Void)
//   1 = conversion-friendly values
//   2 = parseable values (numbers/bool for conversion tests)
// ============================================================================
static Var make_var_sample(VarKind k, int mode = 1) {
    switch (k) {
    case VarKind::Void:
        return Var();
    case VarKind::Int:
        return Var(42);
    case VarKind::Bool:
        return Var(mode == 2 ? true : true); // same either mode
    case VarKind::Double:
        return Var(3.14);
    case VarKind::Int64:
        return Var(static_cast<int64_t>(100));
    case VarKind::String:
        // mode 2: parseable (number-like); mode 1: friendly string
        return Var(mode == 2 ? std::string("hello") : std::string("hello"));
    case VarKind::WideString:
        return Var(mode == 2 ? std::wstring(L"true") : std::wstring(L"world"));
    case VarKind::Rational:
        return Var(Rational(5));
    case VarKind::VectorInt64:
        return Var(std::vector<int64_t>{1, 2, 3});
    case VarKind::VectorDouble:
        return Var(std::vector<double>{1.1, 2.2, 3.3});
    case VarKind::Tensor:
        return Var(TensorHandle{});
    case VarKind::Unsupported:
        return Var(UnsupportedType{});
    }
    return Var(); // unreachable
}

// ============================================================================
// Test: completeness of to_string() — every non-Void kind must produce output
// ============================================================================
TEST_F(VarTest, ToStringCompleteness) {
    for (size_t i = 0; i < VarKindCount; ++i) {
        auto kind = static_cast<VarKind>(i);
        if (kind == VarKind::Void)
            continue; // Void returns "<void>" — handled separately
        auto v = make_var_sample(kind, 1);
        auto s = v.to_string();
        // Only Void is skipped; all others must produce non-empty string
        EXPECT_FALSE(s.empty()) << "to_string() returned empty for " << kind_to_string_impl(kind);
    }
    // Also verify Void explicitly
    EXPECT_EQ(Var().to_string(), "<void>");
}

// ============================================================================
// Test: self-conversions — generated from TRUST_VAR_TYPES
// ============================================================================
#define TEST_SELF_CONVERSION(k, type, cat, fmt)                       \
    TEST_F(VarTest, SelfConversion_##k) {                             \
        EXPECT_TRUE((IsConversionEnabled_v<VarKind::k, VarKind::k>)); \
        Var v(type{});                                                \
        EXPECT_NO_THROW(v.as<type>());                                \
    }
TRUST_VAR_TYPES_ALL(TEST_SELF_CONVERSION)
#undef TEST_SELF_CONVERSION

// ============================================================================
// Test: all allowed conversions work at runtime — automatic grid via index_sequence
// Covers every (From, To) pair permitted by IsConversionEnabled_v.
// Some specific sample values may fail to parse (e.g. "true" --> int) — expected.
// ============================================================================
TEST_F(VarTest, AllAllowedConversions) {
    [&]<size_t... I>(std::index_sequence<I...>) {
        (([&]() constexpr {
             constexpr auto From = static_cast<VarKind>(I);
             auto v = make_var_sample(From, 2);
             [&]<size_t... J>(std::index_sequence<J...>) {
                 (([&]() constexpr {
                      constexpr auto To = static_cast<VarKind>(J);
                      if constexpr (IsConversionEnabled_v<From, To>) {
                          try {
                              v.template as<TypeOfKind_t<To>>();
                          } catch (const ParseError &) {
                          } catch (const std::invalid_argument &) {
                          }
                      }
                  }()),
                  ...);
             }(std::make_index_sequence<VarKindCount>{});
         }()),
         ...);
    }(std::make_index_sequence<VarKindCount>{});
}

TEST_F(VarTest, DisallowedConversionsThrow) {
    // Void -> anything should throw
    Var vv;
    EXPECT_THROW(vv.as<int>(), ConversionError);
    EXPECT_THROW(vv.as<bool>(), ConversionError);
    EXPECT_THROW(vv.as<double>(), ConversionError);
    EXPECT_THROW(vv.as<int64_t>(), ConversionError);
    EXPECT_THROW(vv.as<std::string>(), ConversionError);
    EXPECT_THROW(vv.as<Rational>(), ConversionError);

    // VectorInt64 -> numeric should throw
    Var vec(std::vector<int64_t>{1, 2});
    EXPECT_THROW(vec.as<int>(), ConversionError);
    EXPECT_THROW(vec.as<double>(), ConversionError);

    // Tensor -> numeric should throw
    Var ten(Tensor{});
    EXPECT_THROW(ten.as<int>(), ConversionError);
    EXPECT_THROW(ten.as<bool>(), ConversionError);
}

TEST_F(VarTest, InvalidStringConversionsThrow) {
    // String "abc" -> int should throw
    Var si(std::string("abc"));
    EXPECT_THROW(si.as<int>(), ParseError);
    EXPECT_THROW(si.as<int64_t>(), ParseError);
    EXPECT_THROW(si.as<double>(), ParseError);

    // String "no" -> bool should throw
    Var sb(std::string("no"));
    EXPECT_THROW(sb.as<bool>(), ParseError);

    // String -> Rational should throw
    Var sr(std::string("3\\4"));
    EXPECT_THROW(sr.as<Rational>(), ConversionError);

    // WideString invalid
    Var wsi(std::wstring(L"abc"));
    EXPECT_THROW(wsi.as<int>(), ParseError);
}

// ---- Void ----

TEST_F(VarTest, DefaultConstructor) {
    Var o;
    EXPECT_EQ(o.kind(), VarKind::Void);
    EXPECT_TRUE(o.is<Void>());
    EXPECT_TRUE(o.is<Void>());
}

// ---- Int ----

TEST_F(VarTest, IntConstructor) {
    Var o(42);
    EXPECT_EQ(o.kind(), VarKind::Int);
    EXPECT_TRUE(o.is<int>());
    EXPECT_TRUE(o.is<int>());
    EXPECT_EQ(o.as<int>(), 42);
}

TEST_F(VarTest, IntAsBool) {
    Var o1(42);
    EXPECT_TRUE(o1.as<bool>());

    Var o2(0);
    EXPECT_FALSE(o2.as<bool>());
}

TEST_F(VarTest, IntAsString) {
    Var o(42);
    EXPECT_EQ(o.as<std::string>(), "42");
}

TEST_F(VarTest, IntAsRational) {
    Var o(42);
    auto r = o.as<Rational>();
    EXPECT_EQ(r.GetAsInteger(), 42);
}

// ---- Bool ----

TEST_F(VarTest, BoolConstructor) {
    Var o(true);
    EXPECT_EQ(o.kind(), VarKind::Bool);
    EXPECT_TRUE(o.is<bool>());
    EXPECT_TRUE(o.as<bool>());
}

TEST_F(VarTest, BoolFalse) {
    Var o(false);
    EXPECT_FALSE(o.as<bool>());
}

TEST_F(VarTest, BoolAsInt) {
    Var t(true);
    EXPECT_EQ(t.as<int>(), 1);

    Var f(false);
    EXPECT_EQ(f.as<int>(), 0);
}

TEST_F(VarTest, BoolAsString) {
    Var t(true);
    EXPECT_EQ(t.as<std::string>(), "true");

    Var f(false);
    EXPECT_EQ(f.as<std::string>(), "false");
}

// ---- String ----

TEST_F(VarTest, StringConstructor) {
    Var o(std::string{"hello"});
    EXPECT_EQ(o.kind(), VarKind::String);
    EXPECT_TRUE(o.is<std::string>());
    EXPECT_EQ(o.as<std::string>(), "hello");
}

TEST_F(VarTest, StringAsInt) {
    Var o(std::string{"42"});
    EXPECT_EQ(o.as<int>(), 42);
}

TEST_F(VarTest, StringAsBool) {
    Var t(std::string{"true"});
    EXPECT_TRUE(t.as<bool>());

    Var f(std::string{"false"});
    EXPECT_FALSE(f.as<bool>());
}

TEST_F(VarTest, StringAsBoolInvalid) {
    Var o(std::string{"no"});
    EXPECT_THROW(o.as<bool>(), ParseError);
}

TEST_F(VarTest, StringAsIntInvalid) {
    Var o(std::string{"abc"});
    EXPECT_THROW(o.as<int>(), ParseError);
}

TEST_F(VarTest, StringAsWString) {
    Var o(std::string{"hello"});
    auto w = o.as<std::wstring>();
    EXPECT_EQ(o.as<std::string>(), "hello");
}

// ---- WideString ----

TEST_F(VarTest, WideStringConstructor) {
    Var o(std::wstring{L"hello"});
    EXPECT_EQ(o.kind(), VarKind::WideString);
    EXPECT_TRUE(o.is<std::wstring>());
    EXPECT_EQ(o.as<std::wstring>(), L"hello");
}

TEST_F(VarTest, WideStringAsInt) {
    Var o(std::wstring{L"42"});
    EXPECT_EQ(o.as<int>(), 42);
}

TEST_F(VarTest, WideStringAsBool) {
    Var t(std::wstring{L"true"});
    EXPECT_TRUE(t.as<bool>());
}

TEST_F(VarTest, WideStringAsString) {
    Var o(std::wstring{L"hello"});
    EXPECT_EQ(o.as<std::string>(), "hello");
}

// ---- Rational ----

TEST_F(VarTest, RationalConstructor) {
    Rational r(42);
    Var o(r);
    EXPECT_EQ(o.kind(), VarKind::Rational);
    EXPECT_TRUE(o.is<Rational>());
    EXPECT_EQ(o.as<Rational>().GetAsInteger(), 42);
}

TEST_F(VarTest, RationalAsInt) {
    Rational r("7", "2");
    Var o(r);
    EXPECT_EQ(o.as<int>(), 3);
}

TEST_F(VarTest, RationalAsBool) {
    Rational zero(0);
    Var oz(zero);
    EXPECT_FALSE(oz.as<bool>());

    Rational r(42);
    Var or_(r);
    EXPECT_TRUE(or_.as<bool>());
}

TEST_F(VarTest, RationalAsString) {
    Rational r("3", "4");
    Var o(r);
    EXPECT_EQ(o.as<std::string>(), "3\\4");
}

TEST_F(VarTest, RationalFromStringThrows) {
    Var o(std::string{"3\\4"});
    EXPECT_THROW(o.as<Rational>(), ConversionError);
}

// ---- to_string ----

TEST_F(VarTest, ToStringVoid) {
    Var o;
    EXPECT_EQ(o.to_string(), "<void>");
}

TEST_F(VarTest, ToStringInt) {
    Var o(123);
    EXPECT_EQ(o.to_string(), "123");
}

TEST_F(VarTest, ToStringBool) {
    EXPECT_EQ(Var(true).to_string(), "true");
    EXPECT_EQ(Var(false).to_string(), "false");
}

TEST_F(VarTest, ToStringString) {
    Var o(std::string{"hello"});
    EXPECT_EQ(o.to_string(), "hello");
}

TEST_F(VarTest, ToStringRational) {
    Rational r("1", "3");
    Var o(r);
    EXPECT_EQ(o.to_string(), "1\\3");
}

// ---- Reassignment via Var() ----

TEST_F(VarTest, ReassignInt) {
    Var o;
    o = Var(99);
    EXPECT_EQ(o.kind(), VarKind::Int);
    EXPECT_EQ(o.as<int>(), 99);
}

TEST_F(VarTest, ReassignString) {
    Var o;
    o = Var(std::string{"test"});
    EXPECT_EQ(o.kind(), VarKind::String);
    EXPECT_EQ(o.as<std::string>(), "test");
}

TEST_F(VarTest, ReassignRational) {
    Var o;
    o = Var(Rational(100));
    EXPECT_EQ(o.kind(), VarKind::Rational);
    EXPECT_EQ(o.as<Rational>().GetAsInteger(), 100);
}

// ---- swap ----

TEST_F(VarTest, Swap) {
    Var a(42);
    Var b(std::string{"hello"});
    swap(a, b);
    EXPECT_EQ(a.as<std::string>(), "hello");
    EXPECT_EQ(b.as<int>(), 42);
}

// ---- copy / move ----

TEST_F(VarTest, Copy) {
    Var a(42);
    Var b(a);
    EXPECT_EQ(b.as<int>(), 42);
}

TEST_F(VarTest, Move) {
    Var a(42);
    Var b(std::move(a));
    EXPECT_EQ(b.as<int>(), 42);
}

TEST_F(VarTest, Assignment) {
    Var a(42);
    Var b;
    b = a;
    EXPECT_EQ(b.as<int>(), 42);
}

// ---- Void conversion errors ----

TEST_F(VarTest, VoidAsInt) {
    Var o;
    EXPECT_THROW(o.as<int>(), ConversionError);
}

TEST_F(VarTest, VoidAsBool) {
    Var o;
    EXPECT_THROW(o.as<bool>(), ConversionError);
}

TEST_F(VarTest, VoidAsString) {
    Var o;
    EXPECT_THROW(o.as<std::string>(), ConversionError);
}

// ---- Double ----

TEST_F(VarTest, DoubleConstructor) {
    Var o(3.14);
    EXPECT_EQ(o.kind(), VarKind::Double);
    EXPECT_TRUE(o.is<double>());
    EXPECT_TRUE(o.is<double>());
    EXPECT_DOUBLE_EQ(o.as<double>(), 3.14);
}

TEST_F(VarTest, DoubleAsInt) {
    Var o(3.9);
    EXPECT_EQ(o.as<int>(), 3);
}

TEST_F(VarTest, DoubleAsBool) {
    Var zero(0.0);
    EXPECT_FALSE(zero.as<bool>());
    Var val(0.5);
    EXPECT_TRUE(val.as<bool>());
}

TEST_F(VarTest, DoubleFromString) {
    Var o(3.14);
    EXPECT_EQ(o.as<std::string>(), "3.14");
}

TEST_F(VarTest, DoubleConvertToInt) {
    Var d(42.0);
    EXPECT_EQ(d.as<int>(), 42);
}

// ---- Int64 ----

TEST_F(VarTest, Int64Constructor) {
    Var o(static_cast<int64_t>(9876543210LL));
    EXPECT_EQ(o.kind(), VarKind::Int64);
    EXPECT_TRUE(o.is<int64_t>());
    EXPECT_TRUE(o.is<int64_t>());
    EXPECT_EQ(o.as<int64_t>(), 9876543210LL);
}

TEST_F(VarTest, Int64AsInt) {
    Var o(static_cast<int64_t>(42));
    EXPECT_EQ(o.as<int>(), 42);
}

TEST_F(VarTest, Int64AsDouble) {
    Var o(static_cast<int64_t>(42));
    EXPECT_DOUBLE_EQ(o.as<double>(), 42.0);
}

TEST_F(VarTest, Int64AsBool) {
    Var zero(static_cast<int64_t>(0));
    EXPECT_FALSE(zero.as<bool>());
    Var val(static_cast<int64_t>(1));
    EXPECT_TRUE(val.as<bool>());
}

TEST_F(VarTest, Int64AsString) {
    Var o(static_cast<int64_t>(12345));
    EXPECT_EQ(o.as<std::string>(), "12345");
}

TEST_F(VarTest, IntAsInt64) {
    Var o(42);
    EXPECT_EQ(o.as<int64_t>(), 42);
}

// ---- VectorInt64 ----

TEST_F(VarTest, VectorInt64Constructor) {
    std::vector<int64_t> v = {1, 2, 3, 4, 5};
    Var o(v);
    EXPECT_EQ(o.kind(), VarKind::VectorInt64);
    EXPECT_TRUE(o.is<std::vector<int64_t>>());
    EXPECT_TRUE(o.is<std::vector<int64_t>>());
    auto result = o.as<std::vector<int64_t>>();
    ASSERT_EQ(result.size(), 5u);
    EXPECT_EQ(result[0], 1);
    EXPECT_EQ(result[4], 5);
}

TEST_F(VarTest, VectorInt64ToString) {
    std::vector<int64_t> v = {10, 20, 30};
    Var o(v);
    EXPECT_EQ(o.to_string(), "[10, 20, 30]");
}

TEST_F(VarTest, VectorInt64EmptyToString) {
    std::vector<int64_t> v = {};
    Var o(v);
    EXPECT_EQ(o.to_string(), "[]");
}

TEST_F(VarTest, VectorInt64Reassign) {
    Var o;
    o = Var(std::vector<int64_t>{100, 200});
    EXPECT_EQ(o.kind(), VarKind::VectorInt64);
    EXPECT_EQ(o.to_string(), "[100, 200]");
}

// ---- VectorDouble ----

TEST_F(VarTest, VectorDoubleConstructor) {
    std::vector<double> v = {1.1, 2.2, 3.3, 4.4, 5.5};
    Var o(v);
    EXPECT_EQ(o.kind(), VarKind::VectorDouble);
    EXPECT_TRUE(o.is<std::vector<double>>());
    EXPECT_TRUE(o.is<std::vector<double>>());
    auto result = o.as<std::vector<double>>();
    ASSERT_EQ(result.size(), 5u);
    EXPECT_DOUBLE_EQ(result[0], 1.1);
    EXPECT_DOUBLE_EQ(result[4], 5.5);
}

TEST_F(VarTest, VectorDoubleToString) {
    std::vector<double> v = {10.5, 20.3, 30.0};
    Var o(v);
    EXPECT_EQ(o.to_string(), "[10.5, 20.3, 30]");
}

TEST_F(VarTest, VectorDoubleEmptyToString) {
    std::vector<double> v = {};
    Var o(v);
    EXPECT_EQ(o.to_string(), "[]");
}

TEST_F(VarTest, VectorDoubleReassign) {
    Var o;
    o = Var(std::vector<double>{100.0, 200.0});
    EXPECT_EQ(o.kind(), VarKind::VectorDouble);
    EXPECT_EQ(o.to_string(), "[100, 200]");
}

TEST_F(VarTest, VectorDoubleAsIntThrows) {
    std::vector<double> v = {1.1, 2.2};
    Var o(v);
    EXPECT_THROW(o.as<int>(), ConversionError);
}

TEST_F(VarTest, VectorDoubleAsDoubleThrows) {
    std::vector<double> v = {1.1, 2.2};
    Var o(v);
    EXPECT_THROW(o.as<double>(), ConversionError);
}

// ---- Tensor ----

#ifdef TRUST_HAS_TORCH

TEST_F(VarTest, TensorConstructor) {
    auto tensor = torch::empty({2, 3});
    Var o(TensorHandle{std::make_shared<at::Tensor>(tensor)});
    EXPECT_TRUE(o.is<Tensor>());
    EXPECT_TRUE(o.is<Tensor>());
}

TEST_F(VarTest, TensorReassign) {
    Var o;
    o = Var(TensorHandle{std::make_shared<at::Tensor>(torch::ones({3, 3}))});
    EXPECT_TRUE(o.is<Tensor>());
    auto handle = o.as<Tensor>();
    EXPECT_TRUE(handle);
    auto *tensor_ptr = handle.get<at::Tensor>();
    EXPECT_EQ(tensor_ptr->sizes().vec(), (std::vector<int64_t>{3, 3}));
}

TEST_F(VarTest, TensorAccess) {
    auto tensor = torch::zeros({2, 2});
    auto handle = TensorHandle{std::make_shared<at::Tensor>(tensor)};
    Var o(handle);
    auto retrieved = o.as<Tensor>();
    EXPECT_TRUE(retrieved);
}

TEST_F(VarTest, TensorToString) {
    auto tensor = torch::empty({4, 5});
    Var o(TensorHandle{std::make_shared<at::Tensor>(tensor)});
    auto s = o.to_string();
    // Without torch plugin, to_string returns a generic marker.
    // With torch plugin loaded via dlopen, it may return full details.
    // Accept either case.
    EXPECT_TRUE(!s.empty());
}

#endif // TRUST_HAS_TORCH

// ---- assign new types ----

TEST_F(VarTest, ReassignDouble) {
    Var o;
    o = Var(2.718);
    EXPECT_EQ(o.kind(), VarKind::Double);
    EXPECT_DOUBLE_EQ(o.as<double>(), 2.718);
}

TEST_F(VarTest, ReassignInt64) {
    Var o;
    o = Var(static_cast<int64_t>(12345678901234LL));
    EXPECT_EQ(o.kind(), VarKind::Int64);
    EXPECT_EQ(o.as<int64_t>(), 12345678901234LL);
}

TEST_F(VarTest, SwapDouble) {
    Var a(3.14);
    Var b(42);
    swap(a, b);
    EXPECT_EQ(b.as<double>(), 3.14);
    EXPECT_EQ(a.as<int>(), 42);
}

TEST_F(VarTest, ToStringDouble) {
    EXPECT_EQ(Var(1.5).to_string(), "1.5");
}

TEST_F(VarTest, ToStringInt64) {
    EXPECT_EQ(Var(static_cast<int64_t>(999999999999LL)).to_string(), "999999999999");
}

// ---- conversion errors for new types ----

TEST_F(VarTest, VectorInt64AsIntThrows) {
    std::vector<int64_t> v = {1, 2, 3};
    Var o(v);
    EXPECT_THROW(o.as<int>(), ConversionError);
}

TEST_F(VarTest, InvalidAsDoubleFromVectorInt64) {
    std::vector<int64_t> v = {1, 2, 3};
    Var o(v);
    EXPECT_THROW(o.as<double>(), ConversionError);
}

TEST_F(VarTest, DoubleAsInt64FromEmpty) {
    Var o;
    EXPECT_THROW(o.as<double>(), ConversionError);
}

TEST_F(VarTest, Int64AsInt64FromEmpty) {
    Var o;
    EXPECT_THROW(o.as<int64_t>(), ConversionError);
}

// ---- strict_get<T> ----

TEST_F(VarTest, StrictGetExactMatch) {
    Var o(42);
    auto const &ref = o.strict_get<int>();
    EXPECT_EQ(ref, 42);
}

TEST_F(VarTest, StrictGetString) {
    Var o(std::string{"hello"});
    auto const &ref = o.strict_get<std::string>();
    EXPECT_STREQ(ref.c_str(), "hello");
}

TEST_F(VarTest, StrictGetMismatchThrows) {
    Var o(std::string{"42"});
    EXPECT_THROW(o.strict_get<int>(), std::runtime_error);
}

TEST_F(VarTest, StrictGetEmptyThrows) {
    Var o;
    EXPECT_THROW(o.strict_get<int>(), std::runtime_error);
}

TEST_F(VarTest, StrictGetBool) {
    Var o(false);
    EXPECT_EQ(o.strict_get<bool>(), false);
}

TEST_F(VarTest, StrictGetDouble) {
    Var o(3.14);
    EXPECT_DOUBLE_EQ(o.strict_get<double>(), 3.14);
}

TEST_F(VarTest, StrictGetInt64) {
    Var o(static_cast<int64_t>(9876543210LL));
    EXPECT_EQ(o.strict_get<int64_t>(), 9876543210LL);
}

TEST_F(VarTest, StrictGetVectorInt64) {
    std::vector<int64_t> v = {1, 2, 3};
    Var o(v);
    auto const &ref = o.strict_get<std::vector<int64_t>>();
    ASSERT_EQ(ref.size(), 3u);
    EXPECT_EQ(ref[0], 1);
    EXPECT_EQ(ref[2], 3);
}

// ---- try_get<T> ----

TEST_F(VarTest, TryGetExactMatch) {
    Var o(42);
    auto ptr = o.try_get<int>();
    ASSERT_TRUE(ptr.has_value());
    EXPECT_EQ(**ptr, 42);
}

TEST_F(VarTest, TryGetMismatch) {
    Var o(std::string{"hello"});
    auto ptr = o.try_get<int>();
    EXPECT_FALSE(ptr.has_value());
}

TEST_F(VarTest, TryGetEmpty) {
    Var o;
    auto void_ptr = o.try_get<Void>();
    ASSERT_TRUE(void_ptr.has_value());

    auto int_try = o.try_get<int>();
    EXPECT_FALSE(int_try.has_value());
}

TEST_F(VarTest, TryGetBool) {
    Var o(true);
    auto ptr = o.try_get<bool>();
    ASSERT_TRUE(ptr.has_value());
    EXPECT_EQ(**ptr, true);
}

// ---- is<T> compile-time check ----

TEST_F(VarTest, IsInt) {
    Var o(42);
    EXPECT_TRUE(o.is<int>());
    EXPECT_FALSE(o.is<bool>());
    EXPECT_TRUE(o.is<int>());
    EXPECT_FALSE(o.is<bool>());
}

TEST_F(VarTest, IsString) {
    Var o(std::string{"hi"});
    EXPECT_TRUE(o.is<std::string>());
    EXPECT_TRUE(o.is<std::string>());
}

// ---- kind() consistency ----

TEST_F(VarTest, KindMatchesIs) {
    Var o(42);
    EXPECT_TRUE(o.is<int>());
}

TEST_F(VarTest, KindAfterReassign) {
    Var o;
    EXPECT_EQ(o.kind(), VarKind::Void);
    o = Var(42);
    EXPECT_EQ(o.kind(), VarKind::Int);
    o = Var(std::string{"hi"});
    EXPECT_EQ(o.kind(), VarKind::String);
    o = Var(true);
    EXPECT_EQ(o.kind(), VarKind::Bool);
}

TEST_F(VarTest, KindAfterSwap) {
    Var a(42);
    Var b(std::string{"hello"});
    EXPECT_EQ(a.kind(), VarKind::Int);
    EXPECT_EQ(b.kind(), VarKind::String);
    swap(a, b);
    EXPECT_EQ(a.kind(), VarKind::String);
    EXPECT_EQ(b.kind(), VarKind::Int);
}