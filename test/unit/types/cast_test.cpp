#include <gtest/gtest.h>
#include "types/forward.hpp"
#include "types/types.hpp"

// ── Compile-time conversion validation tests ──

// // Same type conversion is always valid
// static_assert(trust::KindOps::is_valid_conversion_v<trust::TypeKind::Int64, trust::TypeKind::Int64>, "Int64→Int64 must be valid");
// static_assert(trust::KindOps::is_valid_conversion_v<trust::TypeKind::Float64, trust::TypeKind::Float64>, "Float64→Float64 must be valid");
// static_assert(trust::KindOps::is_valid_conversion_v<trust::TypeKind::Complex64, trust::TypeKind::Complex64>, "Complex64→Complex64 must be valid");

// // Integer → Integer: smaller to larger index is valid
// static_assert(trust::KindOps::is_valid_conversion_v<trust::TypeKind::Int8, trust::TypeKind::Int32>, "Int8→Int32 must be valid");
// static_assert(trust::KindOps::is_valid_conversion_v<trust::TypeKind::Int8, trust::TypeKind::Int64>, "Int8→Int64 must be valid");
// static_assert(trust::KindOps::is_valid_conversion_v<trust::TypeKind::Int32, trust::TypeKind::Int64>, "Int32→Int64 must be valid");

// // Integer → Integer: smaller from larger index is invalid
// static_assert(!trust::KindOps::is_valid_conversion_v<trust::TypeKind::Int64, trust::TypeKind::Int8>, "Int64→Int8 must be invalid");
// static_assert(!trust::KindOps::is_valid_conversion_v<trust::TypeKind::Int32, trust::TypeKind::Int8>, "Int32→Int8 must be invalid");

// // Float → Float: smaller to larger index is valid
// static_assert(trust::KindOps::is_valid_conversion_v<trust::TypeKind::Float16, trust::TypeKind::Float64>, "Float16→Float64 must be valid");
// static_assert(trust::KindOps::is_valid_conversion_v<trust::TypeKind::Float32, trust::TypeKind::Float64>, "Float32→Float64 must be valid");

// // Float → Float: smaller from larger index is invalid
// static_assert(!trust::KindOps::is_valid_conversion_v<trust::TypeKind::Float64, trust::TypeKind::Float16>, "Float64→Float16 must be invalid");
// static_assert(!trust::KindOps::is_valid_conversion_v<trust::TypeKind::Float64, trust::TypeKind::Float16>, "Float64→Float16 must be invalid");

// // Complex → Complex: smaller to larger index is valid
// static_assert(trust::KindOps::is_valid_conversion_v<trust::TypeKind::Complex32, trust::TypeKind::Complex128>, "Complex32→Complex128 must be valid");
// static_assert(trust::KindOps::is_valid_conversion_v<trust::TypeKind::Complex64, trust::TypeKind::Complex128>, "Complex64→Complex128 must be valid");

// // Complex → Complex: smaller from larger index is invalid
// static_assert(!trust::KindOps::is_valid_conversion_v<trust::TypeKind::Complex128, trust::TypeKind::Complex32>, "Complex128→Complex32 must be invalid");

// // Integer → Float: always valid at compile time (runtime checks value)
// static_assert(trust::KindOps::is_valid_conversion_v<trust::TypeKind::Int16, trust::TypeKind::Float16>, "Int16→Float16 valid");
// static_assert(trust::KindOps::is_valid_conversion_v<trust::TypeKind::Int64, trust::TypeKind::Float16>, "Int64→Float16 valid");

// // Integer → Complex: always valid at compile time
// static_assert(trust::KindOps::is_valid_conversion_v<trust::TypeKind::Int8, trust::TypeKind::Complex32>, "Int8→Complex32 valid");
// static_assert(trust::KindOps::is_valid_conversion_v<trust::TypeKind::Int64, trust::TypeKind::Complex32>, "Int64→Complex32 valid");

// // Float → Complex: always valid at compile time
// static_assert(trust::KindOps::is_valid_conversion_v<trust::TypeKind::Float32, trust::TypeKind::Complex64>, "Float32→Complex64 valid");
// static_assert(trust::KindOps::is_valid_conversion_v<trust::TypeKind::Float64, trust::TypeKind::Complex32>, "Float64→Complex32 valid");

// // Float → Integer: always valid at compile time
// static_assert(trust::KindOps::is_valid_conversion_v<trust::TypeKind::Float16, trust::TypeKind::Int16>, "Float16→Int16 valid");
// static_assert(trust::KindOps::is_valid_conversion_v<trust::TypeKind::Float64, trust::TypeKind::Int64>, "Float64→Int64 valid");

// // Bool conversion
// static_assert(trust::KindOps::is_valid_conversion_v<trust::TypeKind::Bool, trust::TypeKind::Int8>, "Bool→Int8 must be valid (0<=0 index)");

// // Void → anything is valid (monostate semantics)
// static_assert(trust::KindOps::is_valid_conversion_v<trust::TypeKind::Void, trust::TypeKind::Int64>, "Void→Int64 must be valid");
// static_assert(trust::KindOps::is_valid_conversion_v<trust::TypeKind::Int64, trust::TypeKind::Void>, "Int64→Void must be valid");

// // Cross-category: valid paths only (Int↔Float, Int→Complex, Float→Complex)
// static_assert(trust::KindOps::is_valid_conversion_v<trust::TypeKind::Float64, trust::TypeKind::Int8>, "Float64→Int8 valid");
// static_assert(!trust::KindOps::is_valid_conversion_v<trust::TypeKind::Complex128, trust::TypeKind::Float32>,
//               "Complex128→Float32 invalid (no Complex→Float compile-time)");

// TEST(CastTest, compile_time_conversion_validation) {
//     EXPECT_TRUE((trust::KindOps::is_valid_conversion_v<trust::TypeKind::Int8, trust::TypeKind::Int32>));
//     EXPECT_FALSE((trust::KindOps::is_valid_conversion_v<trust::TypeKind::Int64, trust::TypeKind::Int8>));
//     EXPECT_TRUE((trust::KindOps::is_valid_conversion_v<trust::TypeKind::Int64, trust::TypeKind::Float16>));
//     EXPECT_TRUE((trust::KindOps::is_valid_conversion_v<trust::TypeKind::Int64, trust::TypeKind::Float16>));
//     EXPECT_FALSE((trust::KindOps::is_valid_conversion_v<trust::TypeKind::Complex128, trust::TypeKind::Float32>));
// }

TEST(CastTest, runtime_overflow_int) {
    auto& t = trust::Types::instance();
    trust::Integers v(INT64_MAX, trust::TypeKind::Int64);
    EXPECT_THROW(t.convert(trust::Any(v), trust::TypeKind::Int8), std::overflow_error);
}

TEST(CastTest, runtime_safe_int) {
    auto& t = trust::Types::instance();
    trust::Integers v(42, trust::TypeKind::Int64);
    auto result = t.convert(trust::Any(v), trust::TypeKind::Int32);
    auto* converted = std::get_if<trust::Integers>(&result);
    ASSERT_NE(converted, nullptr);
    EXPECT_EQ(converted->get(), 42);
}

TEST(CastTest, runtime_int_to_float) {
    auto& t = trust::Types::instance();
    trust::Integers v(42, trust::TypeKind::Int64);
    auto result = t.convert(trust::Any(v), trust::TypeKind::Float64);
    auto* converted = std::get_if<trust::Float>(&result);
    ASSERT_NE(converted, nullptr);
    EXPECT_DOUBLE_EQ(converted->get(), 42.0);
}

TEST(CastTest, runtime_float_to_int) {
    auto& t = trust::Types::instance();
    trust::Float v(3.14, trust::TypeKind::Float64);
    auto result = t.convert(trust::Any(v), trust::TypeKind::Int64);
    auto* converted = std::get_if<trust::Integers>(&result);
    ASSERT_NE(converted, nullptr);
    EXPECT_EQ(converted->get(), 3);
}

// TEST(CastTest, runtime_complex_convert) {
//     auto &t = trust::Types::instance();
//     trust::Complex v(std::complex<double>(1, 2), trust::TypeKind::Complex64);
//     auto result = t.convert(trust::Any(v), trust::TypeKind::Complex32);
//     auto *converted = std::get_if<trust::Complex>(&result);
//     ASSERT_NE(converted, nullptr);
//     EXPECT_DOUBLE_EQ(converted->get().real(), 1.0);
// }