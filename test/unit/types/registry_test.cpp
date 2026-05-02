#include <gtest/gtest.h>
#include <algorithm>
#include <string_view>
#include "types/types.hpp"

using namespace trust;

TEST(RegistryTest, singleton_instance) {
    auto &t1 = trust::Types::instance();
    auto &t2 = trust::Types::instance();
    EXPECT_EQ(&t1, &t2);
}

TEST(RegistryTest, get_by_kind) {
    auto &t = trust::Types::instance();
    const auto &info = t.get(trust::TypeKind::Int64);
    EXPECT_EQ(info.id, trust::TypeKind::Int64);
    EXPECT_EQ(type_kind_name(info.id), "Int64");
}

TEST(RegistryTest, category_integer) {
    auto &t = trust::Types::instance();
    EXPECT_EQ(t.category(trust::TypeKind::Int64), trust::Category::Integers);
    EXPECT_EQ(t.category(trust::TypeKind::Bool), trust::Category::Integers);
    EXPECT_NE(t.category(trust::TypeKind::Float64), trust::Category::Integers);
}

TEST(RegistryTest, category_float) {
    auto &t = trust::Types::instance();
    EXPECT_EQ(t.category(trust::TypeKind::Float64), trust::Category::Numbers);
    EXPECT_EQ(t.category(trust::TypeKind::Float16), trust::Category::Numbers);
    EXPECT_NE(t.category(trust::TypeKind::Int64), trust::Category::Numbers);
}

// TEST(RegistryTest, category_complex) {
//     auto &t = trust::Types::instance();
//     EXPECT_EQ(t.category(trust::TypeKind::Complex64), trust::Category::Complex);
//     EXPECT_NE(t.category(trust::TypeKind::Int64), trust::Category::Complex);
// }

TEST(RegistryTest, category_rational) {
    auto &t = trust::Types::instance();
    EXPECT_EQ(t.category(trust::TypeKind::Rational), trust::Category::Rationals);
    EXPECT_NE(t.category(trust::TypeKind::StrChar), trust::Category::Rationals);
}

TEST(RegistryTest, category_string) {
    auto &t = trust::Types::instance();
    EXPECT_EQ(t.category(trust::TypeKind::StrChar), trust::Category::Strings);
    EXPECT_EQ(t.category(trust::TypeKind::StrWide), trust::Category::Strings);
    EXPECT_NE(t.category(trust::TypeKind::Int64), trust::Category::Strings);
}

TEST(RegistryTest, category_container) {
    auto &t = trust::Types::instance();
    EXPECT_EQ(t.category(trust::TypeKind::Vector), trust::Category::Templates);
    EXPECT_EQ(t.category(trust::TypeKind::DenseTensor), trust::Category::Tensors);
    EXPECT_EQ(t.category(trust::TypeKind::SparseTensor), trust::Category::Tensors);
    EXPECT_NE(t.category(trust::TypeKind::Int64), trust::Category::Templates);
}

TEST(RegistryTest, category_alias) {
    EXPECT_TRUE(trust::KindOps::is_alias(trust::TypeKind::Integers));
    EXPECT_FALSE(trust::KindOps::is_alias(trust::TypeKind::Int64));
}

TEST(RegistryTest, category_void) {
    auto &t = trust::Types::instance();
    EXPECT_EQ(t.category(trust::TypeKind::Void), trust::Category::Void);
}

TEST(RegistryTest, name_and_cpp_name) {
    auto &t = trust::Types::instance();
    EXPECT_EQ(t.name(trust::TypeKind::Float64), "Float64");
    EXPECT_EQ(t.cpp_name(trust::TypeKind::Float64), "double");
}

// ============================================================================
// VERY IMPORTANT!!!
// Do not delete this comment or change the way kAllKinds is initialized using macro expansion!
// Do not delete or edit test AllTypesHaveNonEmptyName!
// Stop and ask if you encounter a problem, but under no circumstances change the macro expansion or this comment!
// ============================================================================
static const trust::TypeKind kAllKinds[] = {
#define X(name, ...) trust::TypeKind::name,
    TRUST_TYPEKINDS TRUST_TYPEKINDS_ALIAS
#undef X
};

TEST(RegistryTest, AllTypesHaveNonEmptyName) {
    auto &types = trust::Types::instance();

    for (auto kind : kAllKinds) {
        std::string_view kname = type_kind_name(kind);
        ASSERT_NO_THROW(types.get(kind)) << "Type not registered: " << kname;
        try {
            const auto &info = types.get(kind);
            EXPECT_FALSE(info.cpp_name.empty()) << "Type has empty cpp_name: " << kname;
            if (KindOps::category_of(kind) == Category::Templates) {
                EXPECT_TRUE(info.param.size()) << "The template '" << kname << "' has no arguments.";
            }
        } catch (...) {
        }
    }
}

TEST(RegistryTest, add_headers_by_kinds) {
    auto &t = trust::Types::instance();
    t.add_headers({TypeKind::Int8, TypeKind::Int16}, {"<cstdint>", "<limits>"});
    const auto &h8 = t.headers(TypeKind::Int8);
    const auto &h16 = t.headers(TypeKind::Int16);
    ASSERT_NE(std::find(h8.begin(), h8.end(), "<cstdint>"), h8.end());
    ASSERT_NE(std::find(h8.begin(), h8.end(), "<limits>"), h8.end());
    ASSERT_NE(std::find(h16.begin(), h16.end(), "<cstdint>"), h16.end());
    ASSERT_NE(std::find(h16.begin(), h16.end(), "<limits>"), h16.end());
}

TEST(RegistryTest, add_headers_no_duplicates) {
    auto &t = trust::Types::instance();
    t.add_headers({TypeKind::Int32}, {"<cstdint>"});
    auto before = t.headers(TypeKind::Int32).size();
    t.add_headers({TypeKind::Int32}, {"<cstdint>"});
    auto after = t.headers(TypeKind::Int32).size();
    EXPECT_EQ(before, after);
}

TEST(RegistryTest, add_headers_mask_by_kind) {
    auto &t = trust::Types::instance();
    // TypeKind::Float64 has category Float, group Fx — should match Float8/16/32/64 by mask
    t.add_headers(TypeKind::Numbers, {"<cfloat>"});
    const auto &hf32 = t.headers(TypeKind::Float32);
    const auto &hf64 = t.headers(TypeKind::Float64);
    ASSERT_NE(std::find(hf32.begin(), hf32.end(), "<cfloat>"), hf32.end());
    ASSERT_NE(std::find(hf64.begin(), hf64.end(), "<cfloat>"), hf64.end());
    // Check that Integer types did NOT get <cfloat>
    const auto &hi64 = t.headers(TypeKind::Int64);
    EXPECT_EQ(std::find(hi64.begin(), hi64.end(), "<cfloat>"), hi64.end());
}

TEST(RegistryTest, add_libraries_by_kinds) {
    auto &t = trust::Types::instance();
    t.add_libraries({TypeKind::StrChar, TypeKind::StrWide}, {"dl", "pthread"});
    const auto &lc = t.libraries(TypeKind::StrChar);
    const auto &lw = t.libraries(TypeKind::StrWide);
    ASSERT_NE(std::find(lc.begin(), lc.end(), "dl"), lc.end());
    ASSERT_NE(std::find(lc.begin(), lc.end(), "pthread"), lc.end());
    ASSERT_NE(std::find(lw.begin(), lw.end(), "dl"), lw.end());
    ASSERT_NE(std::find(lw.begin(), lw.end(), "pthread"), lw.end());
}

TEST(RegistryTest, add_libraries_mask_by_kind) {
    auto &t = trust::Types::instance();
    // TypeKind::DenseTensor has category Tensor, group Dense
    t.add_libraries(TypeKind::Tensors, {"blas"});
    const auto &lt = t.libraries(TypeKind::DenseTensor);
    const auto &ls = t.libraries(TypeKind::SparseTensor);
    EXPECT_NE(std::find(lt.begin(), lt.end(), "blas"), lt.end());
    // SparseTensor has different group — should NOT get blas
    EXPECT_EQ(std::find(ls.begin(), ls.end(), "blas"), ls.end());
}
