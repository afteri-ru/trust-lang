// attr_test.cpp — unit tests for the AST attribute system
// Tests AttrPool, Attr, AttrSet, StringPool, and the parse_attr function.

#include "ast/attr_pool.hpp"
#include "ast/attr_builtin.hpp"
#include "ast/attr_parser.hpp"
#include "ast/token_info.hpp"
#include "diag/diag.hpp"
#include "diag/mapper.hpp"
#include <gtest/gtest.h>
#include <string>
#include <vector>

namespace trust {

// ────────────────────────────────────────────────────────────────────────────
// Test fixture: provides an AttrPool with built-in attrs registered
// ────────────────────────────────────────────────────────────────────────────

class AttrPoolTest : public ::testing::Test {
  protected:
    AttrPool pool;

    void SetUp() override { register_builtin_attrs(pool); }
};

// ── Basic AttrPool tests ──

TEST_F(AttrPoolTest, RegisterAndLookup) {
    AttrId id = pool.register_attr("test_attr", std::vector<AttrParamType>{});
    EXPECT_NE(id, 0u);

    auto lookup = pool.lookup("test_attr");
    ASSERT_TRUE(lookup.has_value());
    EXPECT_EQ(lookup.value(), id);
}

TEST_F(AttrPoolTest, RegisterTwiceReturnsSameId) {
    AttrId id1 = pool.register_attr("dup", std::vector<AttrParamType>{});
    AttrId id2 = pool.register_attr("dup", std::vector<AttrParamType>{});
    EXPECT_EQ(id1, id2);
}

TEST_F(AttrPoolTest, RegisterWithDifferentParamTypesReturnsSameId) {
    AttrId id1 = pool.register_attr("param_attr", std::vector<AttrParamType>{AttrParamType::kString});
    AttrId id2 = pool.register_attr("param_attr", std::vector<AttrParamType>{AttrParamType::kRange});
    // Same name → same ID (attributes are unique by name; param types are advisory)
    EXPECT_EQ(id1, id2);
}

TEST_F(AttrPoolTest, AttrWithConcreteParams) {
    std::vector<AttrParam> params;
    params.emplace_back(std::string_view("42"));
    params.emplace_back(std::string_view("hello"));

    AttrId id = pool.register_attr("concrete", std::move(params));
    EXPECT_NE(id, 0u);

    const Attr& attr = pool.get(id);
    EXPECT_EQ(attr.m_name, "concrete");
    ASSERT_EQ(attr.m_default_params.size(), 2u);
    EXPECT_TRUE(attr.m_default_params[0].is_string());
    EXPECT_EQ(attr.m_default_params[0].as_string(), "42");
    EXPECT_TRUE(attr.m_default_params[1].is_string());
    EXPECT_EQ(attr.m_default_params[1].as_string(), "hello");
}

TEST_F(AttrPoolTest, RegisterWithConcreteParamsReturnsExisting) {
    std::vector<AttrParam> params;
    params.emplace_back(std::string_view("99"));

    AttrId id1 = pool.register_attr("concrete_dup", params);
    AttrId id2 = pool.register_attr("concrete_dup", params);
    EXPECT_EQ(id1, id2);
}

// ── param_types() tests ──

TEST_F(AttrPoolTest, ParamTypesDerivedCorrectly) {
    AttrId id = pool.register_attr("typed_attr", std::vector<AttrParamType>{AttrParamType::kString, AttrParamType::kRange});
    const Attr& attr = pool.get(id);

    auto types = attr.param_types();
    ASSERT_EQ(types.size(), 2u);
    EXPECT_EQ(types[0], AttrParamType::kString);
    EXPECT_EQ(types[1], AttrParamType::kRange);
}

TEST_F(AttrPoolTest, ParamTypesEmptyForNoParamAttr) {
    AttrId id = pool.register_attr("empty_attr", std::vector<AttrParamType>{});
    const Attr& attr = pool.get(id);

    auto types = attr.param_types();
    EXPECT_TRUE(types.empty());
}

// ── Built-in attribute tests ──

TEST_F(AttrPoolTest, BuiltinAttributesRegistered) {
    auto id = pool.lookup(attr_names::kConst);
    ASSERT_TRUE(id.has_value());

    const Attr& attr = pool.get(id.value());
    EXPECT_EQ(attr.m_name, "const");
    EXPECT_TRUE(attr.m_default_params.empty());
}

TEST_F(AttrPoolTest, BuiltinTrustHasStringParamType) {
    auto id = pool.lookup(attr_names::kTrust);
    ASSERT_TRUE(id.has_value());

    const Attr& attr = pool.get(id.value());
    ASSERT_EQ(attr.m_default_params.size(), 1u);
    EXPECT_EQ(attr.m_default_params[0].type(), AttrParamType::kString);
}

TEST_F(AttrPoolTest, BuiltinRequireHasRangeParamType) {
    auto id = pool.lookup(attr_names::kRequire);
    ASSERT_TRUE(id.has_value());

    const Attr& attr = pool.get(id.value());
    ASSERT_EQ(attr.m_default_params.size(), 1u);
    EXPECT_EQ(attr.m_default_params[0].type(), AttrParamType::kRange);
}

TEST_F(AttrPoolTest, BuiltinEnsureHasRangeParamType) {
    auto id = pool.lookup(attr_names::kEnsure);
    ASSERT_TRUE(id.has_value());

    const Attr& attr = pool.get(id.value());
    ASSERT_EQ(attr.m_default_params.size(), 1u);
    EXPECT_EQ(attr.m_default_params[0].type(), AttrParamType::kRange);
}

TEST_F(AttrPoolTest, RegisterBuiltinTwiceIsSafe) {
    // Calling register_builtin_attrs again should not corrupt anything
    register_builtin_attrs(pool);

    auto id = pool.lookup(attr_names::kReadOnly);
    ASSERT_TRUE(id.has_value());
    EXPECT_EQ(pool.get(id.value()).m_name, "readonly");
}

// ── User-defined attribute tests ──

TEST_F(AttrPoolTest, UserAttrRegistration) {
    AttrId id = pool.register_attr("my_custom_attr", std::vector<AttrParamType>{AttrParamType::kString, AttrParamType::kRange});
    EXPECT_NE(id, 0u);

    const Attr& attr = pool.get(id);
    EXPECT_EQ(attr.m_name, "my_custom_attr");
    ASSERT_EQ(attr.m_default_params.size(), 2u);
    EXPECT_EQ(attr.m_default_params[0].type(), AttrParamType::kString);
    EXPECT_EQ(attr.m_default_params[1].type(), AttrParamType::kRange);
}

// ── AttrSet tests (via add_multi and resolve) ──

TEST_F(AttrPoolTest, AddMultiSingleSet) {
    AttrId a1 = pool.register_attr("a1", std::vector<AttrParamType>{});
    AttrId a2 = pool.register_attr("a2", std::vector<AttrParamType>{});

    auto result = pool.add_multi({a1, a2}, true);
    ASSERT_EQ(result.size(), 1u);
    // result[0] should be a set ID (has set flag)
    EXPECT_TRUE(result[0] & detail::kAttrSetFlag);

    // resolve should yield both members
    auto resolved = pool.resolve(result[0]);
    EXPECT_EQ(resolved.size(), 2u);
    EXPECT_TRUE(std::find(resolved.begin(), resolved.end(), a1) != resolved.end());
    EXPECT_TRUE(std::find(resolved.begin(), resolved.end(), a2) != resolved.end());
}

TEST_F(AttrPoolTest, IdenticalSetsReturnSameId) {
    AttrId a1 = pool.register_attr("x", std::vector<AttrParamType>{});
    AttrId a2 = pool.register_attr("y", std::vector<AttrParamType>{});

    auto set1 = pool.add_multi({a1, a2}, true);
    auto set2 = pool.add_multi({a2, a1}, true); // reverse order
    ASSERT_EQ(set1.size(), 1u);
    ASSERT_EQ(set2.size(), 1u);
    EXPECT_EQ(set1[0], set2[0]);
}

TEST_F(AttrPoolTest, EmptyAddMulti) {
    auto result = pool.add_multi({}, true);
    EXPECT_TRUE(result.empty());
}

TEST_F(AttrPoolTest, AddMultiWithDuplicatesDeduplicated) {
    AttrId a1 = pool.register_attr("dup_a", std::vector<AttrParamType>{});

    auto result = pool.add_multi({a1, a1, a1}, true);
    ASSERT_EQ(result.size(), 1u);

    auto resolved = pool.resolve(result[0]);
    ASSERT_EQ(resolved.size(), 1u);
    EXPECT_EQ(resolved[0], a1);
}

// ── add_multi without set creation ──

TEST_F(AttrPoolTest, AddMultiWithoutSet) {
    AttrId a1 = pool.register_attr("solo1", std::vector<AttrParamType>{});
    AttrId a2 = pool.register_attr("solo2", std::vector<AttrParamType>{});

    auto result = pool.add_multi({a1, a2}, false);
    // No existing sets, so result should contain individual IDs
    ASSERT_EQ(result.size(), 2u);
    EXPECT_TRUE(std::find(result.begin(), result.end(), a1) != result.end());
    EXPECT_TRUE(std::find(result.begin(), result.end(), a2) != result.end());
}

TEST_F(AttrPoolTest, AddMultiWithoutSetExisting) {
    AttrId a1 = pool.register_attr("base1", std::vector<AttrParamType>{});
    AttrId a2 = pool.register_attr("base2", std::vector<AttrParamType>{});
    AttrId a3 = pool.register_attr("base3", std::vector<AttrParamType>{});

    // Create a set first with a1, a2
    auto set_result = pool.add_multi({a1, a2}, true);
    ASSERT_EQ(set_result.size(), 1u);
    AttrId set_id = set_result[0];

    // Now add a1, a2, a3 without creating a set
    auto result = pool.add_multi({a1, a2, a3}, false);
    // Should return best_match set + missing a3
    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(result[0], set_id);
    EXPECT_EQ(result[1], a3);
}

// ── resolve() tests ──

TEST_F(AttrPoolTest, ResolveSingleton) {
    AttrId a = pool.register_attr("single", std::vector<AttrParamType>{});
    auto resolved = pool.resolve(a);
    ASSERT_EQ(resolved.size(), 1u);
    EXPECT_EQ(resolved[0], a);
}

TEST_F(AttrPoolTest, ResolveSet) {
    AttrId a1 = pool.register_attr("rs1", std::vector<AttrParamType>{});
    AttrId a2 = pool.register_attr("rs2", std::vector<AttrParamType>{});

    auto set_result = pool.add_multi({a1, a2}, true);
    ASSERT_EQ(set_result.size(), 1u);

    auto resolved = pool.resolve(set_result[0]);
    EXPECT_EQ(resolved.size(), 2u);
    EXPECT_TRUE(std::find(resolved.begin(), resolved.end(), a1) != resolved.end());
    EXPECT_TRUE(std::find(resolved.begin(), resolved.end(), a2) != resolved.end());
}

// ── Counters ──

TEST_F(AttrPoolTest, AttrCount) {
    // built-in attrs (12 attrs) + 1 reserved placeholder
    std::size_t initial = pool.attr_count();
    EXPECT_EQ(initial, 13u);

    pool.register_attr("extra", std::vector<AttrParamType>{});
    EXPECT_EQ(pool.attr_count(), initial + 1);
}

TEST_F(AttrPoolTest, SetCount) {
    EXPECT_EQ(pool.set_count(), 0u);
    AttrId a = pool.register_attr("set_test_a", std::vector<AttrParamType>{});
    pool.add_multi({a}, true);
    EXPECT_EQ(pool.set_count(), 1u);
}

// ── TokenInfo with AttrId tests ──

TEST(AttrTokenInfoTest, AddAttrToToken) {
    AttrPool pool;
    AttrId id = pool.register_attr("tok_attr", std::vector<AttrParamType>{});

    TokenInfo tok;
    EXPECT_TRUE(tok.m_attrs.empty());

    tok.add_attr(id);
    ASSERT_EQ(tok.m_attrs.size(), 1u);
    EXPECT_EQ(tok.m_attrs[0], id);
}

TEST(AttrTokenInfoTest, HasAttrOnToken) {
    AttrPool pool;
    AttrId id = pool.register_attr("check_attr", std::vector<AttrParamType>{});

    TokenInfo tok;
    tok.add_attr(id);

    EXPECT_TRUE(tok.has_attr(id));

    AttrId other = pool.register_attr("other_attr", std::vector<AttrParamType>{});
    EXPECT_FALSE(tok.has_attr(other));
}

TEST(AttrTokenInfoTest, MultipleAttrsOnToken) {
    AttrPool pool;
    AttrId a1 = pool.register_attr("multi1", std::vector<AttrParamType>{});
    AttrId a2 = pool.register_attr("multi2", std::vector<AttrParamType>{});

    TokenInfo tok;
    tok.add_attr(a1);
    tok.add_attr(a2);

    ASSERT_EQ(tok.m_attrs.size(), 2u);
    EXPECT_TRUE(tok.has_attr(a1));
    EXPECT_TRUE(tok.has_attr(a2));
}

// ── AttrParam::type() test ──

TEST(AttrParamTest, TypeDetection) {
    EXPECT_EQ(AttrParam(std::string_view("str")).type(), AttrParamType::kString);
    EXPECT_EQ(AttrParam(MapperRange{}).type(), AttrParamType::kRange);
}

// ── AttrParam::operator== test ──

TEST(AttrParamTest, EqualityOperator) {
    EXPECT_TRUE(AttrParam(std::string_view("hello")) == AttrParam(std::string_view("hello")));
    EXPECT_FALSE(AttrParam(std::string_view("hello")) == AttrParam(std::string_view("world")));
    // Different types: string vs range — always false
    EXPECT_FALSE(AttrParam(std::string_view("42")) == AttrParam(MapperRange{}));
}

// ── Attr::matches_params test ──

TEST(AttrTest, MatchesParams) {
    Attr attr;
    attr.m_default_params = std::vector<AttrParam>{AttrParam(std::string_view("42"))};

    std::vector<AttrParam> matching = {AttrParam(std::string_view("42"))};
    EXPECT_TRUE(attr.matches_params(matching));

    std::vector<AttrParam> non_matching = {AttrParam(std::string_view("99"))};
    EXPECT_FALSE(attr.matches_params(non_matching));

    std::vector<AttrParam> empty;
    EXPECT_FALSE(attr.matches_params(empty));
}

// ── Move semantics ──

TEST_F(AttrPoolTest, MovePoolPreservesAttributes) {
    AttrId original_id = pool.register_attr("movable", std::vector<AttrParamType>{});

    AttrPool moved = std::move(pool);

    const auto& attr = moved.get(original_id);
    EXPECT_EQ(attr.m_name, "movable");
}

TEST_F(AttrPoolTest, MovePoolPreservesSets) {
    AttrId a1 = pool.register_attr("ma", std::vector<AttrParamType>{});
    AttrId a2 = pool.register_attr("mb", std::vector<AttrParamType>{});
    auto set_result = pool.add_multi({a1, a2}, true);
    ASSERT_EQ(set_result.size(), 1u);
    AttrId set_id = set_result[0];

    AttrPool moved = std::move(pool);

    // After move, resolve the set to check members
    auto resolved = moved.resolve(set_id);
    EXPECT_EQ(resolved.size(), 2u);
    EXPECT_TRUE(std::find(resolved.begin(), resolved.end(), a1) != resolved.end());
    EXPECT_TRUE(std::find(resolved.begin(), resolved.end(), a2) != resolved.end());
}

// ── AttrPool intern tests ──

TEST_F(AttrPoolTest, InternAndDedup) {
    std::string_view s1 = pool.intern("hello");
    std::string_view s2 = pool.intern("hello");
    EXPECT_EQ(s1.data(), s2.data());
    EXPECT_EQ(s1, "hello");
}

TEST_F(AttrPoolTest, InternEmptyReturnsEmpty) {
    std::string_view s = pool.intern("");
    EXPECT_TRUE(s.empty());
}

TEST_F(AttrPoolTest, InternDifferentStrings) {
    std::string_view s1 = pool.intern("abc");
    std::string_view s2 = pool.intern("xyz");
    EXPECT_NE(s1.data(), s2.data());
}

TEST_F(AttrPoolTest, InternLongString) {
    std::string long_str(5000, 'A');
    std::string_view s = pool.intern(long_str);
    EXPECT_EQ(s.size(), 5000u);
}

// ── parse_attr tests (requires DiagnosticEngine) ──

class AttrParserTest : public ::testing::Test {
  protected:
    AttrPool pool;

    void SetUp() override { register_builtin_attrs(pool); }
};

TEST_F(AttrParserTest, ParseSimpleNameAttr) {
    DiagnosticEngine diag;

    // Create a token sequence: [NAME("const")]
    TokenSequence tokens;
    tokens.push_back(TokenInfo::make(ParserToken::Kind::NAME, "const", MapperRange{}));

    auto result = parse_attr(pool, tokens, diag);
    ASSERT_TRUE(result.has_value());

    // Should match built-in const
    const Attr& attr = pool.get(result->m_id);
    EXPECT_EQ(attr.m_name, "const");
}

TEST_F(AttrParserTest, ParseAttrWithStringParam) {
    DiagnosticEngine diag;

    // Create: NAME("my_attr") LPAREN INTEGER("42") RPAREN
    // Integer defaults to string param now (no int64_t conversion)
    TokenSequence tokens;
    tokens.push_back(TokenInfo::make(ParserToken::Kind::NAME, "my_attr", MapperRange{}));
    tokens.push_back(TokenInfo::make(ParserToken::Kind::LPAREN, "(", MapperRange{}));
    tokens.push_back(TokenInfo::make(ParserToken::Kind::INTEGER, "42", MapperRange{}));
    tokens.push_back(TokenInfo::make(ParserToken::Kind::RPAREN, ")", MapperRange{}));

    auto result = parse_attr(pool, tokens, diag);
    ASSERT_TRUE(result.has_value());

    const Attr& attr = pool.get(result->m_id);
    EXPECT_EQ(attr.m_name, "my_attr");
    ASSERT_TRUE(attr.has_params());
    EXPECT_EQ(attr.m_default_params.size(), 1u);
    EXPECT_TRUE(attr.m_default_params[0].is_string());
    EXPECT_EQ(attr.m_default_params[0].as_string(), "42");
}

TEST_F(AttrParserTest, ParseAttrWithStringParamFromString) {
    DiagnosticEngine diag;

    // Create: NAME("labeled") LPAREN STRWIDE("warning") RPAREN
    TokenSequence tokens;
    tokens.push_back(TokenInfo::make(ParserToken::Kind::NAME, "labeled", MapperRange{}));
    tokens.push_back(TokenInfo::make(ParserToken::Kind::LPAREN, "(", MapperRange{}));
    tokens.push_back(TokenInfo::make(ParserToken::Kind::STRWIDE, "warning", MapperRange{}));
    tokens.push_back(TokenInfo::make(ParserToken::Kind::RPAREN, ")", MapperRange{}));

    auto result = parse_attr(pool, tokens, diag);
    ASSERT_TRUE(result.has_value());

    const Attr& attr = pool.get(result->m_id);
    EXPECT_EQ(attr.m_name, "labeled");
    ASSERT_TRUE(attr.has_params());
    EXPECT_TRUE(attr.m_default_params[0].is_string());
    EXPECT_EQ(attr.m_default_params[0].as_string(), "warning");
}

TEST_F(AttrParserTest, ParseBuiltinTrustWithStringParam) {
    DiagnosticEngine diag;

    // @[trust("some assertion")]
    TokenSequence tokens;
    tokens.push_back(TokenInfo::make(ParserToken::Kind::NAME, "trust", MapperRange{}));
    tokens.push_back(TokenInfo::make(ParserToken::Kind::LPAREN, "(", MapperRange{}));
    tokens.push_back(TokenInfo::make(ParserToken::Kind::STRWIDE, "x > 0", MapperRange{}));
    tokens.push_back(TokenInfo::make(ParserToken::Kind::RPAREN, ")", MapperRange{}));

    auto result = parse_attr(pool, tokens, diag);
    ASSERT_TRUE(result.has_value());
}

TEST_F(AttrParserTest, ParseEmptyTokensReturnsNullopt) {
    DiagnosticEngine diag;
    TokenSequence tokens;

    auto result = parse_attr(pool, tokens, diag);
    EXPECT_FALSE(result.has_value());
}

TEST_F(AttrParserTest, ParseWithoutNameReturnsNullopt) {
    DiagnosticEngine diag;
    TokenSequence tokens;
    tokens.push_back(TokenInfo::make(ParserToken::Kind::LPAREN, "(", MapperRange{}));

    auto result = parse_attr(pool, tokens, diag);
    EXPECT_FALSE(result.has_value());
}

TEST_F(AttrParserTest, ParseTwiceWithSameNameReturnsSameId) {
    DiagnosticEngine diag;

    // First: NAME("dynamic") LPAREN INTEGER("1") RPAREN
    TokenSequence t1;
    t1.push_back(TokenInfo::make(ParserToken::Kind::NAME, "dynamic", MapperRange{}));
    t1.push_back(TokenInfo::make(ParserToken::Kind::LPAREN, "(", MapperRange{}));
    t1.push_back(TokenInfo::make(ParserToken::Kind::INTEGER, "1", MapperRange{}));
    t1.push_back(TokenInfo::make(ParserToken::Kind::RPAREN, ")", MapperRange{}));

    // Second: NAME("dynamic") LPAREN INTEGER("2") RPAREN — same param type (String) but different value
    TokenSequence t2;
    t2.push_back(TokenInfo::make(ParserToken::Kind::NAME, "dynamic", MapperRange{}));
    t2.push_back(TokenInfo::make(ParserToken::Kind::LPAREN, "(", MapperRange{}));
    t2.push_back(TokenInfo::make(ParserToken::Kind::INTEGER, "2", MapperRange{}));
    t2.push_back(TokenInfo::make(ParserToken::Kind::RPAREN, ")", MapperRange{}));

    auto r1 = parse_attr(pool, t1, diag);
    auto r2 = parse_attr(pool, t2, diag);
    ASSERT_TRUE(r1.has_value());
    ASSERT_TRUE(r2.has_value());

    // Same name + same param types → same ID (values are not part of identity)
    EXPECT_EQ(r1->m_id, r2->m_id);
}

// ── BuiltinAttrKind tests ──

TEST_F(AttrPoolTest, BuiltinKindIsSetForBuiltinAttrs) {
    // Verify that all built-in attributes have correct BuiltinAttrKind
    auto check_kind = [&](BuiltinAttrKind kind, std::string_view name) {
        auto id = pool.lookup(name);
        ASSERT_TRUE(id.has_value());
        const Attr& attr = pool.get(id.value());
        EXPECT_EQ(attr.m_builtin_kind, kind);
    };

    check_kind(BuiltinAttrKind::kConst, attr_names::kConst);
    check_kind(BuiltinAttrKind::kPure, attr_names::kPure);
    check_kind(BuiltinAttrKind::kSend, attr_names::kSend);
    check_kind(BuiltinAttrKind::kSync, attr_names::kSync);
    check_kind(BuiltinAttrKind::kThread, attr_names::kThread);
    check_kind(BuiltinAttrKind::kReadOnly, attr_names::kReadOnly);
    check_kind(BuiltinAttrKind::kNoExcept, attr_names::kNoExcept);
    check_kind(BuiltinAttrKind::kStackGuard, attr_names::kStackGuard);
    check_kind(BuiltinAttrKind::kTrust, attr_names::kTrust);
    check_kind(BuiltinAttrKind::kRequire, attr_names::kRequire);
    check_kind(BuiltinAttrKind::kEnsure, attr_names::kEnsure);
    check_kind(BuiltinAttrKind::kDependMacro, attr_names::kDependMacro);
}

TEST_F(AttrPoolTest, BuiltinKindIsNoneForUserAttrs) {
    AttrId id = pool.register_attr("user_defined", std::vector<AttrParamType>{});
    const Attr& attr = pool.get(id);
    EXPECT_EQ(attr.m_builtin_kind, BuiltinAttrKind::kNone);
}

TEST_F(AttrPoolTest, BuiltinIdReturnsCorrectId) {
    AttrId expected = pool.lookup(attr_names::kConst).value();
    AttrId got = pool.builtin_id(BuiltinAttrKind::kConst);
    EXPECT_EQ(got, expected);
}

TEST_F(AttrPoolTest, IsBuiltinReturnsTrueForMatchingBuiltin) {
    AttrId id = pool.lookup(attr_names::kConst).value();
    EXPECT_TRUE(pool.is_builtin(id, BuiltinAttrKind::kConst));
}

TEST_F(AttrPoolTest, IsBuiltinReturnsFalseForDifferentKind) {
    AttrId id = pool.lookup(attr_names::kConst).value();
    EXPECT_FALSE(pool.is_builtin(id, BuiltinAttrKind::kPure));
}

TEST_F(AttrPoolTest, IsBuiltinReturnsFalseForUserAttr) {
    AttrId id = pool.register_attr("my_attr", std::vector<AttrParamType>{});
    EXPECT_FALSE(pool.is_builtin(id, BuiltinAttrKind::kConst));
}

// ── has_attr tests ──

TEST_F(AttrPoolTest, HasAttrByName) {
    EXPECT_TRUE(pool.has_attr(attr_names::kConst));
    EXPECT_TRUE(pool.has_attr(attr_names::kTrust));
    EXPECT_FALSE(pool.has_attr("nonexistent_attr"));
}

TEST_F(AttrPoolTest, HasAttrByKind) {
    EXPECT_TRUE(pool.has_attr(BuiltinAttrKind::kConst));
    EXPECT_TRUE(pool.has_attr(BuiltinAttrKind::kTrust));
    EXPECT_FALSE(pool.has_attr(static_cast<BuiltinAttrKind>(99)));
}

TEST_F(AttrPoolTest, HasAttrOnToken) {
    register_builtin_attrs(pool);
    AttrId const_id = pool.builtin_id(BuiltinAttrKind::kConst);

    TokenInfo tok;
    tok.add_attr(const_id);

    EXPECT_TRUE(tok.has_attr(pool, pool.builtin_id(BuiltinAttrKind::kConst)));
    EXPECT_FALSE(tok.has_attr(pool, pool.builtin_id(BuiltinAttrKind::kPure)));
}

TEST_F(AttrPoolTest, HasAttrOnTokenByResolvedId) {
    register_builtin_attrs(pool);
    AttrId const_id = pool.builtin_id(BuiltinAttrKind::kConst);

    TokenInfo tok;
    tok.add_attr(const_id);

    EXPECT_TRUE(tok.has_attr(pool, const_id));

    AttrId pure_id = pool.builtin_id(BuiltinAttrKind::kPure);
    EXPECT_FALSE(tok.has_attr(pool, pure_id));
}

TEST_F(AttrPoolTest, HasAttrOnTokenByName) {
    register_builtin_attrs(pool);

    TokenInfo tok;
    tok.add_attr(pool.builtin_id(BuiltinAttrKind::kConst));

    EXPECT_TRUE(tok.has_attr(pool, attr_names::kConst));
    EXPECT_FALSE(tok.has_attr(pool, attr_names::kPure));
}

// ── add_attrs on TokenInfo ──

TEST_F(AttrPoolTest, TokenAddAttrs) {
    register_builtin_attrs(pool);
    AttrId const_id = pool.builtin_id(BuiltinAttrKind::kConst);
    AttrId pure_id = pool.builtin_id(BuiltinAttrKind::kPure);

    TokenInfo tok;
    tok.add_attrs(pool, {const_id, pure_id}, true);

    EXPECT_TRUE(tok.has_attr(pool, pool.builtin_id(BuiltinAttrKind::kConst)));
    EXPECT_TRUE(tok.has_attr(pool, pool.builtin_id(BuiltinAttrKind::kPure)));
}

// ── Attr::to_string tests ──

TEST(AttrToStringTest, NoParams) {
    Attr attr;
    attr.m_name = "const";
    EXPECT_EQ(attr.to_string(), "const");
}

TEST(AttrToStringTest, SingleStringParam) {
    Attr attr;
    attr.m_name = "trust";
    attr.m_default_params = std::vector<AttrParam>{AttrParam(std::string_view("x > 0"))};
    EXPECT_EQ(attr.to_string(), "trust(\"x > 0\")");
}

TEST(AttrToStringTest, MultipleStringParams) {
    Attr attr;
    attr.m_name = "multi";
    attr.m_default_params = std::vector<AttrParam>{AttrParam(std::string_view("1")), AttrParam(std::string_view("hello")), AttrParam(std::string_view("42"))};
    EXPECT_EQ(attr.to_string(), "multi(\"1\", \"hello\", \"42\")");
}

TEST(AttrToStringTest, EmptyDefaultParams) {
    Attr attr;
    attr.m_name = "empty_parens";
    attr.m_default_params = {}; // empty vector
    EXPECT_EQ(attr.to_string(), "empty_parens");
}

// ── AttrPool::export_attrs tests ──

class ExportTest : public ::testing::Test {
  protected:
    AttrPool pool;

    void SetUp() override { register_builtin_attrs(pool); }
};

TEST_F(ExportTest, EmptyTokensReturnsEmptyPool) {
    std::vector<TokenInfo*> empty;
    auto result = pool.export_attrs(empty);
    // Exported pool has only placeholder (1 entry)
    EXPECT_EQ(result->attr_count(), 1u);
}

TEST_F(ExportTest, SingleTokenSingleAttr) {
    AttrId const_id = pool.builtin_id(BuiltinAttrKind::kConst);
    TokenInfo tok;
    tok.add_attr(const_id);

    std::vector<TokenInfo*> tokens = {&tok};
    auto result = pool.export_attrs(tokens);

    // New pool has placeholder + "const"
    EXPECT_GE(result->attr_count(), 2u);
    EXPECT_TRUE(result->has_attr("const"));
    // Token must report having const attr via the new pool (handles sets internally)
    auto new_const = result->lookup("const");
    ASSERT_TRUE(new_const.has_value());
    EXPECT_TRUE(tok.has_attr(*result, new_const.value()));
}

TEST_F(ExportTest, MultipleTokensSharedAttr) {
    AttrId const_id = pool.builtin_id(BuiltinAttrKind::kConst);
    AttrId pure_id = pool.builtin_id(BuiltinAttrKind::kPure);

    TokenInfo tok1;
    tok1.add_attr(const_id);
    tok1.add_attr(pure_id);

    TokenInfo tok2;
    tok2.add_attr(const_id); // shared with tok1

    std::vector<TokenInfo*> tokens = {&tok1, &tok2};
    auto result = pool.export_attrs(tokens);

    EXPECT_TRUE(result->has_attr("const"));
    EXPECT_TRUE(result->has_attr("pure"));

    // Both tokens referencing const should have it in their m_attrs
    // (via the new AttrId from the exported pool)
    auto new_const = result->lookup("const");
    ASSERT_TRUE(new_const.has_value());
    EXPECT_TRUE(tok1.has_attr(*result, new_const.value()));
    EXPECT_TRUE(tok2.has_attr(*result, new_const.value()));
}

TEST_F(ExportTest, BuiltinKindsPreserved) {
    AttrId const_id = pool.builtin_id(BuiltinAttrKind::kConst);
    AttrId send_id = pool.builtin_id(BuiltinAttrKind::kSend);
    AttrId thread_id = pool.builtin_id(BuiltinAttrKind::kThread);

    TokenInfo tok;
    tok.add_attr(const_id);
    tok.add_attr(send_id);
    tok.add_attr(thread_id);

    std::vector<TokenInfo*> tokens = {&tok};
    auto result = pool.export_attrs(tokens);

    EXPECT_TRUE(result->has_attr("const"));
    EXPECT_TRUE(result->has_attr("send"));
    EXPECT_TRUE(result->has_attr("thread"));
}

TEST_F(ExportTest, SetsAreResolved) {
    // Create singleton attrs
    AttrId a1 = pool.register_attr("set_a", std::vector<AttrParamType>{});
    AttrId a2 = pool.register_attr("set_b", std::vector<AttrParamType>{});

    // Add them as a set to a token
    TokenInfo tok;
    tok.add_attrs(pool, {a1, a2}, true);

    // Verify initial state: should have 1 entry (a set)
    ASSERT_EQ(tok.m_attrs.size(), 1u);
    EXPECT_TRUE(tok.m_attrs[0] & detail::kAttrSetFlag);

    // Export
    std::vector<TokenInfo*> tokens = {&tok};
    auto result = pool.export_attrs(tokens);

    // After export, resolve the set from the new ModuleApi to get individual IDs for comparison
    ASSERT_FALSE(result->tokens().empty());
    ASSERT_NE(result->tokens()[0].m_attr, 0);
    auto resolved = result->resolve(result->tokens()[0].m_attr);
    auto new_a1 = result->lookup("set_a");
    auto new_a2 = result->lookup("set_b");
    ASSERT_TRUE(new_a1.has_value());
    ASSERT_TRUE(new_a2.has_value());
    EXPECT_TRUE(std::find(resolved.begin(), resolved.end(), new_a1.value()) != resolved.end());
    EXPECT_TRUE(std::find(resolved.begin(), resolved.end(), new_a2.value()) != resolved.end());
}

TEST_F(ExportTest, IndependenceFromSourcePool) {
    AttrId const_id = pool.builtin_id(BuiltinAttrKind::kConst);
    TokenInfo tok;
    tok.add_attr(const_id);

    std::vector<TokenInfo*> tokens = {&tok};
    auto result = pool.export_attrs(tokens);

    // Destroy the source pool by moving it away
    AttrPool moved = std::move(pool);

    // The result and tok should still work
    EXPECT_TRUE(result->has_attr("const"));
    auto new_const = result->lookup("const");
    ASSERT_TRUE(new_const.has_value());
    EXPECT_TRUE(tok.has_attr(*result, new_const.value()));
}

TEST_F(ExportTest, SameAttrCombinationProducesSameSet) {
    AttrId const_id = pool.builtin_id(BuiltinAttrKind::kConst);
    AttrId pure_id = pool.builtin_id(BuiltinAttrKind::kPure);
    AttrId send_id = pool.builtin_id(BuiltinAttrKind::kSend);

    // Two tokens with the same attributes
    TokenInfo tok1;
    tok1.add_attr(const_id);
    tok1.add_attr(pure_id);
    tok1.add_attr(send_id);

    TokenInfo tok2;
    tok2.add_attr(const_id);
    tok2.add_attr(pure_id);
    tok2.add_attr(send_id);

    std::vector<TokenInfo*> tokens = {&tok1, &tok2};
    auto result = pool.export_attrs(tokens);

    // Both tokens should have the same set ID
    // (add_multi deduplication should give same set for same attrs)
    EXPECT_GE(tok1.m_attrs.size(), 1u);
    EXPECT_EQ(tok1.m_attrs, tok2.m_attrs);
}

TEST_F(ExportTest, TokenWithNoAttrs) {
    TokenInfo tok;
    std::vector<TokenInfo*> tokens = {&tok};
    auto result = pool.export_attrs(tokens);

    // Token's attrs should remain empty
    EXPECT_TRUE(tok.m_attrs.empty());
    // Exported pool should have only the placeholder
    EXPECT_EQ(result->attr_count(), 1u);
}

TEST_F(ExportTest, TokenWithConcreteParams) {
    std::vector<AttrParam> params;
    params.emplace_back(std::string_view("42"));
    params.emplace_back(std::string_view("hello"));
    MapperLocation begin = MapperLocation::makeLoc(MapperFile::make_input(0), 100);
    MapperLocation end = MapperLocation::makeLoc(MapperFile::make_input(0), 200);
    params.emplace_back(MapperRange(begin, end));

    AttrId id = pool.register_attr("param_attr", params);

    TokenInfo tok;
    // Use "param_token" as the text identifier
    tok.text = "param_token";
    tok.add_attr(id);

    std::vector<TokenInfo*> tokens = {&tok};
    auto result = pool.export_attrs(tokens);

    // Verify via result->tokens() that the record exists with the correct attrs
    ASSERT_EQ(result->token_count(), 1u);
    EXPECT_EQ(result->get_token_name(0), "param_token");
    EXPECT_NE(result->tokens()[0].m_attr, 0);

    // Verify we can look up the attr in the new pool
    auto new_id = result->lookup("param_attr");
    ASSERT_TRUE(new_id.has_value());

    // Verify the attr name is preserved
    EXPECT_EQ(result->get_name(new_id.value()), "param_attr");

    // Verify via create_token that the token can be recreated with correct attrs
    // by registering into a fresh pool
    AttrPool recreated_pool;
    register_builtin_attrs(recreated_pool);
    auto recreated = result->create_token(0, recreated_pool);
    ASSERT_NE(recreated, nullptr);
    // The attribute should have been registered in recreated_pool
    auto recreated_id = recreated_pool.lookup("param_attr");
    ASSERT_TRUE(recreated_id.has_value());
    EXPECT_TRUE(recreated->has_attr(recreated_pool, recreated_id.value()));
}

TEST_F(ExportTest, SortingByAttrCount) {
    // Token with 3 attrs, token with 1 attr, token with 2 attrs
    AttrId a = pool.register_attr("alpha", std::vector<AttrParamType>{});
    AttrId b = pool.register_attr("beta", std::vector<AttrParamType>{});
    AttrId c = pool.register_attr("gamma", std::vector<AttrParamType>{});

    TokenInfo tok3;
    tok3.text = "tok3";
    tok3.add_attr(a);
    tok3.add_attr(b);
    tok3.add_attr(c);
    TokenInfo tok1;
    tok1.text = "tok1";
    tok1.add_attr(a);
    TokenInfo tok2;
    tok2.text = "tok2";
    tok2.add_attr(a);
    tok2.add_attr(b);

    std::vector<TokenInfo*> tokens = {&tok3, &tok1, &tok2};
    auto result = pool.export_attrs(tokens);

    // Look up new AttrIds in the exported pool
    auto new_a = result->lookup("alpha");
    auto new_b = result->lookup("beta");
    auto new_c = result->lookup("gamma");
    ASSERT_TRUE(new_a.has_value());
    ASSERT_TRUE(new_b.has_value());
    ASSERT_TRUE(new_c.has_value());

    // Verify via tokens() that all three records are present with their names
    ASSERT_EQ(result->token_count(), 3u);
    EXPECT_EQ(result->get_token_name(0), "tok3");
    EXPECT_EQ(result->get_token_name(1), "tok1");
    EXPECT_EQ(result->get_token_name(2), "tok2");

    // Verify via create_token that each recreated token has the expected attrs
    auto check_token = [&](std::size_t index, AttrId expected_id) -> bool {
        AttrPool check_pool;
        register_builtin_attrs(check_pool);
        auto token = result->create_token(index, check_pool);
        if (!token)
            return false;
        auto re_id = check_pool.lookup(result->get_name(expected_id));
        return re_id.has_value() && token->has_attr(check_pool, re_id.value());
    };

    EXPECT_TRUE(check_token(1, new_a.value())); // tok1 → alpha
    EXPECT_TRUE(check_token(2, new_a.value())); // tok2 → alpha
    EXPECT_TRUE(check_token(2, new_b.value())); // tok2 → beta
    EXPECT_TRUE(check_token(0, new_a.value())); // tok3 → alpha
    EXPECT_TRUE(check_token(0, new_b.value())); // tok3 → beta
    EXPECT_TRUE(check_token(0, new_c.value())); // tok3 → gamma
}

TEST_F(ExportTest, AttrWithoutParams) {
    AttrId id = pool.register_attr("no_params", std::vector<AttrParamType>{});
    TokenInfo tok;
    tok.text = "no_params_token";
    tok.add_attr(id);

    std::vector<TokenInfo*> tokens = {&tok};
    auto result = pool.export_attrs(tokens);

    auto new_id = result->lookup("no_params");
    ASSERT_TRUE(new_id.has_value());

    // Verify name
    EXPECT_EQ(result->get_name(new_id.value()), "no_params");

    // Verify token record
    ASSERT_EQ(result->token_count(), 1u);
    EXPECT_EQ(result->get_token_name(0), "no_params_token");

    // Create token via pool to verify it works
    AttrPool check_pool;
    register_builtin_attrs(check_pool);
    auto token = result->create_token(0, check_pool);
    EXPECT_NE(token, nullptr);
}

TEST_F(ExportTest, AttrWithStringParam) {
    AttrId id = pool.register_attr("str_attr", std::vector<AttrParam>{AttrParam(std::string_view("test_value"))});
    TokenInfo tok;
    tok.text = "str_token";
    tok.add_attr(id);

    std::vector<TokenInfo*> tokens = {&tok};
    auto result = pool.export_attrs(tokens);

    auto new_id = result->lookup("str_attr");
    ASSERT_TRUE(new_id.has_value());
    EXPECT_EQ(result->get_name(new_id.value()), "str_attr");

    // Verify via create_token
    AttrPool check_pool;
    register_builtin_attrs(check_pool);
    auto token = result->create_token(0, check_pool);
    EXPECT_NE(token, nullptr);
    auto re_id = check_pool.lookup("str_attr");
    ASSERT_TRUE(re_id.has_value());
    EXPECT_TRUE(token->has_attr(check_pool, re_id.value()));
}

TEST_F(ExportTest, AttrWithRangeParam) {
    MapperLocation begin = MapperLocation::makeLoc(MapperFile::make_input(0), 50);
    MapperLocation end = MapperLocation::makeLoc(MapperFile::make_input(0), 150);
    AttrId id = pool.register_attr("range_attr", std::vector<AttrParam>{AttrParam(MapperRange(begin, end))});
    TokenInfo tok;
    tok.text = "range_token";
    tok.add_attr(id);

    std::vector<TokenInfo*> tokens = {&tok};
    auto result = pool.export_attrs(tokens);

    auto new_id = result->lookup("range_attr");
    ASSERT_TRUE(new_id.has_value());
    EXPECT_EQ(result->get_name(new_id.value()), "range_attr");

    AttrPool check_pool;
    register_builtin_attrs(check_pool);
    auto token = result->create_token(0, check_pool);
    EXPECT_NE(token, nullptr);
    auto re_id = check_pool.lookup("range_attr");
    ASSERT_TRUE(re_id.has_value());
    EXPECT_TRUE(token->has_attr(check_pool, re_id.value()));
}

TEST_F(ExportTest, AttrWithMultipleStringParams) {
    std::vector<AttrParam> params;
    params.emplace_back(std::string_view("10"));
    params.emplace_back(std::string_view("20"));
    params.emplace_back(std::string_view("30"));
    AttrId id = pool.register_attr("multi_str_attr", params);
    TokenInfo tok;
    tok.text = "multi_str_token";
    tok.add_attr(id);

    std::vector<TokenInfo*> tokens = {&tok};
    auto result = pool.export_attrs(tokens);

    auto new_id = result->lookup("multi_str_attr");
    ASSERT_TRUE(new_id.has_value());
    EXPECT_EQ(result->get_name(new_id.value()), "multi_str_attr");

    AttrPool check_pool;
    register_builtin_attrs(check_pool);
    auto token = result->create_token(0, check_pool);
    EXPECT_NE(token, nullptr);
    auto re_id = check_pool.lookup("multi_str_attr");
    ASSERT_TRUE(re_id.has_value());
    EXPECT_TRUE(token->has_attr(check_pool, re_id.value()));
}

TEST_F(ExportTest, ParamsIndependenceFromSourcePool) {
    std::vector<AttrParam> params;
    params.emplace_back(std::string_view("42"));
    params.emplace_back(std::string_view("hello"));
    AttrId id = pool.register_attr("indep_attr", params);

    TokenInfo tok;
    tok.text = "indep_token";
    tok.add_attr(id);

    std::vector<TokenInfo*> tokens = {&tok};
    auto result = pool.export_attrs(tokens);

    // Destroy source pool
    AttrPool moved = std::move(pool);

    auto new_id = result->lookup("indep_attr");
    ASSERT_TRUE(new_id.has_value());
    EXPECT_EQ(result->get_name(new_id.value()), "indep_attr");

    // Create token after source pool is destroyed — should still work
    AttrPool check_pool;
    register_builtin_attrs(check_pool);
    auto token = result->create_token(0, check_pool);
    EXPECT_NE(token, nullptr);
    auto re_id = check_pool.lookup("indep_attr");
    ASSERT_TRUE(re_id.has_value());
    EXPECT_TRUE(token->has_attr(check_pool, re_id.value()));
}

} // namespace trust