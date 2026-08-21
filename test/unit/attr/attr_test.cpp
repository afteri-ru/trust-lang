// attr_test.cpp - tests for attribute parsing and AttrId bitmask
#include "ast/attr_parser.hpp"
#include "ast/attr_builtin.hpp"
#include "ast/token.hpp"
#include "ast/token_base.hpp"
#include "diag/context.hpp"
#include <gtest/gtest.h>
#include <memory>
#include <string>

namespace trust {

class AttrParserTest : public ::testing::Test {
  protected:
    void SetUp() override {
        // Register a source file so we can build valid MapperRanges.
        m_file = m_ctx.source().add_source("attr_test.src", "test source");
        ASSERT_FALSE(m_file.isInvalid());
        m_range = MapperRange(m_file, 1, 4);
    }

    Context m_ctx;
    MapperFile m_file{};
    MapperRange m_range{};
};

TEST_F(AttrParserTest, ParseSimpleNameAttr) {
    auto result = parse_attr(m_ctx, m_range, "readonly");
    ASSERT_TRUE(result.has_value());

    // Should match built-in readonly (единый атрибут иммутабельности)
    const Attr& attr = m_ctx.attrs().get(*result);
    EXPECT_EQ(attr.m_name, "readonly");
}

TEST_F(AttrParserTest, ParseAttrWithoutParamsDefaultNullopt) {
    // parse_attr(ctx, range, name) - params defaults to nullopt
    auto result = parse_attr(m_ctx, m_range, "readonly");
    ASSERT_TRUE(result.has_value());

    const Attr& attr = m_ctx.attrs().get(*result);
    EXPECT_EQ(attr.m_name, "readonly");
}

TEST_F(AttrParserTest, ParseAttrWithStringParam) {
    // @[my_attr(42)] - the attribute must be registered first.
    AttrId reg = m_ctx.attrs().register_attr("my_attr", {"42"}, m_range);
    ASSERT_FALSE(detail::is_builtin(reg));

    std::vector<std::string_view> params = {"42"};
    auto result = parse_attr(m_ctx, m_range, "my_attr", params);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, reg);

    const Attr& attr = m_ctx.attrs().get(*result);
    EXPECT_EQ(attr.m_name, "my_attr");
    ASSERT_TRUE(attr.has_params());
    EXPECT_EQ(attr.m_default_params.size(), 1u);
    EXPECT_EQ(attr.m_default_params[0], "42");
}

TEST_F(AttrParserTest, ParseAttrWithStringParamFromString) {
    // @[labeled("warning")] - the attribute must be registered first.
    AttrId reg = m_ctx.attrs().register_attr("labeled", {"warning"}, m_range);
    ASSERT_FALSE(detail::is_builtin(reg));

    std::vector<std::string_view> params = {"warning"};
    auto result = parse_attr(m_ctx, m_range, "labeled", params);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, reg);
}

TEST_F(AttrParserTest, ParseBuiltinTrustWithStringParam) {
    // @[trust("x > 0")] - встроенные строковые атрибуты (trust/link и др.) зарегистрированы
    // с одним пустым (wildcard) дефолт-параметром: он принимает ЛЮБОЕ строковое значение.
    // Раньше произвольные строковые параметры отклонялись ("mismatched parameters");
    // теперь пустой дефолт = wildcard (см. Attr::matches_params).
    std::vector<std::string_view> params = {"x > 0"};
    auto result = parse_attr(m_ctx, m_range, "trust", params);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(m_ctx.attrs().get(*result).m_name, "trust");

    // Без параметров строковый атрибут тоже резолвится.
    auto noParams = parse_attr(m_ctx, m_range, "trust");
    ASSERT_TRUE(noParams.has_value());
}

TEST_F(AttrParserTest, ParseLinkAttrWithArbitraryValue) {
    // @[link("m")] - имя нативной библиотеки; произвольное строковое значение.
    auto result = parse_attr(m_ctx, m_range, "link", std::vector<std::string_view>{"m"});
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(m_ctx.attrs().get(*result).m_name, attr::Link);

    auto z = parse_attr(m_ctx, m_range, "link", std::vector<std::string_view>{"z"});
    ASSERT_TRUE(z.has_value());

    // Без параметров (голый @[link]) атрибут резолвится (валидация выполняется только
    // при предоставлении параметров), но значения не несёт.
    auto none = parse_attr(m_ctx, m_range, "link");
    ASSERT_TRUE(none.has_value());
    // Неверная арность (2 параметра при зарегистрированной 1) - не совпадает → ошибка.
    auto two = parse_attr(m_ctx, m_range, "link", std::vector<std::string_view>{"a", "b"});
    EXPECT_FALSE(two.has_value());
}

TEST_F(AttrParserTest, AttrValueStorageOnNode) {
    // set_attr_args/attr_args - хранение списка аргументов атрибута на узле.
    auto link_id = m_ctx.attrs().lookup(attr::Link);
    ASSERT_TRUE(link_id.has_value());

    auto node = std::make_shared<AstNodeAttr>(ParserToken::Kind::FuncDecl);
    node->add_attr(link_id.value());
    EXPECT_EQ(node->attr_args(link_id.value()), nullptr);

    node->set_attr_args(link_id.value(), {"m"});
    const std::vector<std::string>* v = node->attr_args(link_id.value());
    ASSERT_NE(v, nullptr);
    ASSERT_EQ(v->size(), 1u);
    EXPECT_EQ((*v)[0], "m");

    // Несколько аргументов.
    node->set_attr_args(link_id.value(), {"a", "b"});
    ASSERT_EQ(node->attr_args(link_id.value())->size(), 2u);
    EXPECT_EQ((*node->attr_args(link_id.value()))[1], "b");

    // Перезапись.
    node->set_attr_args(link_id.value(), {"z"});
    ASSERT_EQ(node->attr_args(link_id.value())->size(), 1u);
    EXPECT_EQ((*node->attr_args(link_id.value()))[0], "z");

    // Аргументы индексируются по индексу атрибута (биты маски игнорируются).
    AttrId bare = link_id.value() & detail::kAttrIndexMask;
    EXPECT_EQ(node->attr_args(bare), node->attr_args(link_id.value()));
}

TEST_F(AttrParserTest, ParseEmptyNameReturnsNullopt) {
    auto result = parse_attr(m_ctx, m_range, "");
    EXPECT_FALSE(result.has_value());
}

TEST_F(AttrParserTest, ParseUnknownAttrReturnsNulloptWithDiagnostic) {
    auto before = m_ctx.diag().errorCount();
    auto result = parse_attr(m_ctx, m_range, "definitely_unknown_attr");
    EXPECT_FALSE(result.has_value());
    EXPECT_GT(m_ctx.diag().errorCount(), before) << "unknown attribute must produce a diagnostic";
}

TEST_F(AttrParserTest, ParseTwiceWithSameNameReturnsSameId) {
    // Register dynamic(1) - the same name always maps to the same AttrId.
    AttrId reg = m_ctx.attrs().register_attr("dynamic", {"1"}, m_range);

    std::vector<std::string_view> p1 = {"1"};
    std::vector<std::string_view> pBad = {"2"};

    auto r1 = parse_attr(m_ctx, m_range, "dynamic", p1);
    auto rBad = parse_attr(m_ctx, m_range, "dynamic", pBad);
    ASSERT_TRUE(r1.has_value());
    EXPECT_EQ(*r1, reg);
    // Mismatched params must fail with a diagnostic.
    EXPECT_FALSE(rBad.has_value());
}

TEST_F(AttrParserTest, BuiltinAttrsHaveBuiltinBit) {
    auto readonly_id = m_ctx.attrs().lookup(attr::ReadOnly);
    ASSERT_TRUE(readonly_id.has_value());
    EXPECT_TRUE(detail::is_builtin(readonly_id.value())) << "built-in attribute must have the builtin bit set";
    EXPECT_FALSE(detail::is_manual(readonly_id.value())) << "registered attribute must not be manual by default";
}

TEST_F(AttrParserTest, UserDefinedAttrsDoNotHaveBuiltinBit) {
    AttrId reg = m_ctx.attrs().register_attr("my_user_attr", {}, m_range);
    EXPECT_FALSE(detail::is_builtin(reg)) << "user-defined attribute must not have the builtin bit set";

    auto result = parse_attr(m_ctx, m_range, "my_user_attr");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, reg);
}

TEST_F(AttrParserTest, RegisterUserAttrHasNoBuiltinBit) {
    AttrId id = m_ctx.attrs().register_attr("custom_attr", {}, m_range);
    EXPECT_FALSE(detail::is_builtin(id));
}

TEST_F(AttrParserTest, RegisterBuiltinAttrHasBuiltinBit) {
    AttrId id = m_ctx.attrs().register_builtin_attr("my_builtin_attr");
    EXPECT_TRUE(detail::is_builtin(id));
    EXPECT_FALSE(detail::is_manual(id));
}

TEST_F(AttrParserTest, UserDefinedAttrStoresRange) {
    AttrId id = m_ctx.attrs().register_attr("custom_attr", {}, m_range);
    const Attr& attr = m_ctx.attrs().get(id);
    EXPECT_FALSE(attr.m_def_range.isInvalid());
    EXPECT_EQ(attr.m_def_range, m_range);
    EXPECT_FALSE(detail::is_builtin(id));
}

TEST_F(AttrParserTest, ParseAttrReturnsSameRangeAsRegistered) {
    AttrId reg = m_ctx.attrs().register_attr("parsed_user_attr", {}, m_range);
    ASSERT_FALSE(detail::is_builtin(reg));
    const Attr& attr = m_ctx.attrs().get(reg);
    EXPECT_FALSE(attr.m_def_range.isInvalid());
    EXPECT_EQ(attr.m_def_range, m_range);
}

TEST_F(AttrParserTest, BuiltinAttrHasInvalidRange) {
    auto readonly_id = m_ctx.attrs().lookup(attr::ReadOnly);
    ASSERT_TRUE(readonly_id.has_value());
    const Attr& attr = m_ctx.attrs().get(*readonly_id);
    EXPECT_TRUE(attr.m_def_range.isInvalid());
    EXPECT_TRUE(detail::is_builtin(readonly_id.value()));
}

TEST_F(AttrParserTest, RegisterBuiltinAttrHasInvalidRange) {
    AttrId id = m_ctx.attrs().register_builtin_attr("my_builtin_attr");
    const Attr& attr = m_ctx.attrs().get(id);
    EXPECT_TRUE(attr.m_def_range.isInvalid());
    EXPECT_TRUE(detail::is_builtin(id));
}

TEST_F(AttrParserTest, AddAttrSetsManualBit) {
    auto readonly_id = m_ctx.attrs().lookup(attr::ReadOnly);
    ASSERT_TRUE(readonly_id.has_value());

    auto node = std::make_shared<AstNodeAttr>(ParserToken::Kind::SemicolonStmt);
    node->add_attr(readonly_id.value());

    ASSERT_EQ(node->attrs().size(), 1u);
    EXPECT_TRUE(detail::is_manual(node->attrs()[0])) << "add_attr must set the manual bit automatically";
    // The base index must match the registered id
    EXPECT_EQ(node->attrs()[0] & detail::kAttrIndexMask, readonly_id.value() & detail::kAttrIndexMask);
}

TEST_F(AttrParserTest, AddAttrAutoDoesNotSetManualBit) {
    auto readonly_id = m_ctx.attrs().lookup(attr::ReadOnly);
    ASSERT_TRUE(readonly_id.has_value());

    auto node = std::make_shared<AstNodeAttr>(ParserToken::Kind::SemicolonStmt);
    node->add_attr(readonly_id.value(), false);

    ASSERT_EQ(node->attrs().size(), 1u);
    EXPECT_FALSE(detail::is_manual(node->attrs()[0])) << "add_attr(id, false) must not set the manual bit";
}

TEST_F(AttrParserTest, HasAttrMatchesByIndexIgnoringBits) {
    auto readonly_id = m_ctx.attrs().lookup(attr::ReadOnly);
    ASSERT_TRUE(readonly_id.has_value());

    auto node = std::make_shared<AstNodeAttr>(ParserToken::Kind::SemicolonStmt);
    node->add_attr(readonly_id.value());

    // Query by the same index but with different flag bits must still match.
    AttrId bare = readonly_id.value() & detail::kAttrIndexMask;
    EXPECT_TRUE(node->has_attr(bare));
    EXPECT_TRUE(node->has_attr(detail::with_manual(bare)));
    EXPECT_TRUE(node->has_attr(detail::with_builtin(bare)));

    // Query by name resolves through the pool.
    EXPECT_TRUE(node->has_attr(m_ctx.attrs(), attr::ReadOnly));
    EXPECT_FALSE(node->has_attr(m_ctx.attrs(), "nonexistent_attr"));
}

TEST_F(AttrParserTest, AttrIdHelpers) {
    AttrId idx = 42;
    AttrId b = detail::with_builtin(idx);
    EXPECT_TRUE(detail::is_builtin(b));
    EXPECT_FALSE(detail::is_builtin(idx));

    AttrId m = detail::with_manual(idx);
    EXPECT_TRUE(detail::is_manual(m));
    EXPECT_FALSE(detail::is_manual(idx));

    // Clearing works too
    EXPECT_FALSE(detail::is_builtin(detail::with_builtin(b, false)));
    EXPECT_FALSE(detail::is_manual(detail::with_manual(m, false)));
}

} // namespace trust