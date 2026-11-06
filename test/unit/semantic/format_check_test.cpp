// test/unit/semantic/format_check_test.cpp
// Юнит-тесты printf-формат проверки (semantic/format_check.hpp): парсер printf-спецификаторов
// и сверка ожидаемой категории аргумента с фактическим типом (атрибут @[format("printf", ...)]).

#include "semantic/verify/format_check.hpp"
#include "types/type_system.hpp"
#include "session/context.hpp"
#include "gtest/gtest.h"

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace trust {
namespace {

TEST(FormatCheck, ParseConversions) {
    std::vector<format_check::Conversion> convs;
    // %% - литерал (аргумент не потребляет); %d/%s/%f/%u/%x - конверсии в порядке аргументов.
    ASSERT_TRUE(format_check::parse_printf_format("x %d %s %% %f %u %x", convs));
    ASSERT_EQ(convs.size(), 5u);
    EXPECT_EQ(convs[0].conv, 'd');
    EXPECT_EQ(convs[0].expect, format_check::Expect::Integer);
    EXPECT_EQ(convs[1].conv, 's');
    EXPECT_EQ(convs[1].expect, format_check::Expect::StrChar);
    EXPECT_EQ(convs[2].conv, 'f');
    EXPECT_EQ(convs[2].expect, format_check::Expect::Float);
    EXPECT_EQ(convs[3].conv, 'u');
    EXPECT_EQ(convs[3].expect, format_check::Expect::Unsigned);
    EXPECT_EQ(convs[4].conv, 'x');
    EXPECT_EQ(convs[4].expect, format_check::Expect::Unsigned);
}

TEST(FormatCheck, ParseFlagsWidthPrecisionLength) {
    std::vector<format_check::Conversion> convs;
    // Флаги/ширина/точность/length-модификаторы не меняют категорию, но должны парситься.
    ASSERT_TRUE(format_check::parse_printf_format("%-5.2f %*d %ld %lld %s", convs));
    ASSERT_EQ(convs.size(), 5u);
    EXPECT_EQ(convs[0].expect, format_check::Expect::Float);
    EXPECT_EQ(convs[1].expect, format_check::Expect::Integer);
    EXPECT_EQ(convs[2].expect, format_check::Expect::Integer);
    EXPECT_EQ(convs[3].expect, format_check::Expect::Integer);
    EXPECT_EQ(convs[4].expect, format_check::Expect::StrChar);
}

TEST(FormatCheck, ParseInvalid) {
    std::vector<format_check::Conversion> convs;
    EXPECT_FALSE(format_check::parse_printf_format("abc%", convs)); // незакрытый '%'
    EXPECT_FALSE(format_check::parse_printf_format("%q", convs));   // неизвестный конверсионный символ
}

TEST(FormatCheck, ExpectNameAndInvalidFaults) {
    using format_check::Expect;
    EXPECT_STREQ(format_expect_name(Expect::Integer), "integer");
    EXPECT_STREQ(format_expect_name(Expect::Unsigned), "unsigned integer");
    EXPECT_STREQ(format_expect_name(Expect::Float), "floating point");
    EXPECT_STREQ(format_expect_name(Expect::StrChar), "string");
    EXPECT_STREQ(format_expect_name(Expect::Pointer), "pointer");
    EXPECT_THROW((void)format_expect_name(static_cast<Expect>(255)), std::runtime_error);
}

class FormatCheckTypeFixture : public ::testing::Test {
  protected:
    void SetUp() override {
        m_types = std::make_unique<TypeSystem>(m_ctx.diag(), m_ctx.opts());
        m_ctx.setTypes(m_types.get());
    }
    Context m_ctx;
    std::unique_ptr<TypeSystem> m_types;
};

TEST_F(FormatCheckTypeFixture, ArgMatchesExpect) {
    TypeSystem& reg = *m_types;
    const TypeId int32 = reg.read().getType("Int32");
    const TypeId str = reg.read().getType("StrChar");
    const TypeId cstr = reg.read().getType("CString");
    const TypeId flt = reg.read().getType("Float64");
    ASSERT_NE(int32, INVALID_TYPE_ID);
    ASSERT_NE(str, INVALID_TYPE_ID);
    ASSERT_NE(cstr, INVALID_TYPE_ID);
    ASSERT_NE(flt, INVALID_TYPE_ID);

    EXPECT_TRUE(format_check::arg_matches_expect(reg, int32, format_check::Expect::Integer));
    EXPECT_FALSE(format_check::arg_matches_expect(reg, int32, format_check::Expect::StrChar));
    EXPECT_FALSE(format_check::arg_matches_expect(reg, int32, format_check::Expect::Float));

    // %s ожидает C-строку (const char* = CString); StrChar (std::string) - только литерал.
    EXPECT_TRUE(format_check::arg_matches_expect(reg, cstr, format_check::Expect::StrChar));
    EXPECT_FALSE(format_check::arg_matches_expect(reg, str, format_check::Expect::StrChar));
    EXPECT_FALSE(format_check::arg_matches_expect(reg, str, format_check::Expect::Integer));

    EXPECT_TRUE(format_check::arg_matches_expect(reg, flt, format_check::Expect::Float));
    EXPECT_FALSE(format_check::arg_matches_expect(reg, flt, format_check::Expect::Integer));
}

} // namespace
} // namespace trust
