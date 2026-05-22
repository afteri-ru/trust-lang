// attr_test.cpp — unit tests for the new AST attribute system
// Tests AttrPool, Attr, AttrSet, StringPool, and the parse_attr function.

#include "ast/attr.hpp"
#include "ast/attr_pool.hpp"
#include "ast/attr_builtin.hpp"
#include "ast/attr_parser.hpp"
#include "ast/token_info.hpp"
#include "diag/diag.hpp"
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
    AttrId id1 = pool.register_attr("param_attr", std::vector<AttrParamType>{AttrParamType::kInt});
    AttrId id2 = pool.register_attr("param_attr", std::vector<AttrParamType>{AttrParamType::kString});
    // Same name → same ID (attributes are unique by name; param types are advisory)
    EXPECT_EQ(id1, id2);
}

TEST_F(AttrPoolTest, AttrWithConcreteParams) {
    std::vector<AttrParam> params;
    params.emplace_back(int64_t(42));
    params.emplace_back(std::string_view("hello"));

    AttrId id = pool.register_attr("concrete", std::move(params));
    EXPECT_NE(id, 0u);

    const Attr& attr = pool.get(id);
    EXPECT_EQ(attr.m_name, "concrete");
    ASSERT_TRUE(attr.m_params.has_value());
    EXPECT_EQ(attr.m_params->size(), 2u);
    EXPECT_TRUE(attr.m_params->at(0).is_int());
    EXPECT_EQ(attr.m_params->at(0).as_int(), 42);
    EXPECT_TRUE(attr.m_params->at(1).is_string());
    EXPECT_EQ(attr.m_params->at(1).as_string(), "hello");
}

TEST_F(AttrPoolTest, RegisterWithConcreteParamsReturnsExisting) {
    std::vector<AttrParam> params;
    params.emplace_back(int64_t(99));

    AttrId id1 = pool.register_attr("concrete_dup", params);
    AttrId id2 = pool.register_attr("concrete_dup", params);
    EXPECT_EQ(id1, id2);
}

// ── Built-in attribute tests ──

TEST_F(AttrPoolTest, BuiltinAttributesRegistered) {
    auto id = pool.lookup(attr_names::kConst);
    ASSERT_TRUE(id.has_value());

    const Attr& attr = pool.get(id.value());
    EXPECT_EQ(attr.m_name, "const");
    EXPECT_TRUE(attr.m_required_param_types.empty());
}

TEST_F(AttrPoolTest, BuiltinTrustHasStringParamType) {
    auto id = pool.lookup(attr_names::kTrust);
    ASSERT_TRUE(id.has_value());

    const Attr& attr = pool.get(id.value());
    ASSERT_EQ(attr.m_required_param_types.size(), 1u);
    EXPECT_EQ(attr.m_required_param_types[0], AttrParamType::kString);
}

TEST_F(AttrPoolTest, BuiltinRequireHasRangeParamType) {
    auto id = pool.lookup(attr_names::kRequire);
    ASSERT_TRUE(id.has_value());

    const Attr& attr = pool.get(id.value());
    ASSERT_EQ(attr.m_required_param_types.size(), 1u);
    EXPECT_EQ(attr.m_required_param_types[0], AttrParamType::kRange);
}

TEST_F(AttrPoolTest, BuiltinEnsureHasRangeParamType) {
    auto id = pool.lookup(attr_names::kEnsure);
    ASSERT_TRUE(id.has_value());

    const Attr& attr = pool.get(id.value());
    ASSERT_EQ(attr.m_required_param_types.size(), 1u);
    EXPECT_EQ(attr.m_required_param_types[0], AttrParamType::kRange);
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
    AttrId id = pool.register_attr("my_custom_attr", std::vector<AttrParamType>{AttrParamType::kInt, AttrParamType::kString});
    EXPECT_NE(id, 0u);

    const Attr& attr = pool.get(id);
    EXPECT_EQ(attr.m_name, "my_custom_attr");
    ASSERT_EQ(attr.m_required_param_types.size(), 2u);
    EXPECT_EQ(attr.m_required_param_types[0], AttrParamType::kInt);
    EXPECT_EQ(attr.m_required_param_types[1], AttrParamType::kString);
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
    // built-in attrs (11 attrs) + 1 reserved placeholder
    std::size_t initial = pool.attr_count();
    EXPECT_EQ(initial, 12u);

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
    EXPECT_EQ(AttrParam(int64_t(1)).type(), AttrParamType::kInt);
    EXPECT_EQ(AttrParam(std::string_view("str")).type(), AttrParamType::kString);
    EXPECT_EQ(AttrParam(MapperRange{}).type(), AttrParamType::kRange);
}

// ── Attr::matches_params test ──

TEST(AttrTest, MatchesParams) {
    Attr attr;
    attr.m_params = std::vector<AttrParam>{AttrParam(int64_t(42))};

    std::vector<AttrParam> matching = {AttrParam(int64_t(42))};
    EXPECT_TRUE(attr.matches_params(matching));

    std::vector<AttrParam> non_matching = {AttrParam(int64_t(99))};
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

// ── StringPool tests ──

TEST(StringPoolTest, InternAndDedup) {
    detail::StringPool sp;
    std::string_view s1 = sp.intern("hello");
    std::string_view s2 = sp.intern("hello");
    EXPECT_EQ(s1.data(), s2.data());
}

TEST(StringPoolTest, EmptyString) {
    detail::StringPool sp;
    std::string_view s = sp.intern("");
    EXPECT_TRUE(s.empty());
}

TEST(StringPoolTest, DifferentStrings) {
    detail::StringPool sp;
    std::string_view s1 = sp.intern("abc");
    std::string_view s2 = sp.intern("xyz");
    EXPECT_NE(s1.data(), s2.data());
}

TEST(StringPoolTest, LongString) {
    detail::StringPool sp;
    std::string long_str(5000, 'A');
    std::string_view s = sp.intern(long_str);
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

TEST_F(AttrParserTest, ParseAttrWithIntParam) {
    DiagnosticEngine diag;

    // Create: NAME("my_attr") LPAREN INTEGER("42") RPAREN
    TokenSequence tokens;
    tokens.push_back(TokenInfo::make(ParserToken::Kind::NAME, "my_attr", MapperRange{}));
    tokens.push_back(TokenInfo::make(ParserToken::Kind::LPAREN, "(", MapperRange{}));
    tokens.push_back(TokenInfo::make(ParserToken::Kind::INTEGER, "42", MapperRange{}));
    tokens.push_back(TokenInfo::make(ParserToken::Kind::RPAREN, ")", MapperRange{}));

    auto result = parse_attr(pool, tokens, diag);
    ASSERT_TRUE(result.has_value());

    const Attr& attr = pool.get(result->m_id);
    EXPECT_EQ(attr.m_name, "my_attr");
    ASSERT_TRUE(attr.m_params.has_value());
    EXPECT_EQ(attr.m_params->size(), 1u);
    EXPECT_TRUE(attr.m_params->at(0).is_int());
    EXPECT_EQ(attr.m_params->at(0).as_int(), 42);
}

TEST_F(AttrParserTest, ParseAttrWithStringParam) {
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
    ASSERT_TRUE(attr.m_params.has_value());
    EXPECT_TRUE(attr.m_params->at(0).is_string());
    EXPECT_EQ(attr.m_params->at(0).as_string(), "warning");
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

    // Second: NAME("dynamic") LPAREN INTEGER("2") RPAREN — same param type (Int) but different value
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

    EXPECT_TRUE(tok.has_attr(pool, BuiltinAttrKind::kConst));
    EXPECT_FALSE(tok.has_attr(pool, BuiltinAttrKind::kPure));
}

// ── add_attrs on TokenInfo ──

TEST_F(AttrPoolTest, TokenAddAttrs) {
    register_builtin_attrs(pool);
    AttrId const_id = pool.builtin_id(BuiltinAttrKind::kConst);
    AttrId pure_id = pool.builtin_id(BuiltinAttrKind::kPure);

    TokenInfo tok;
    tok.add_attrs(pool, {const_id, pure_id}, true);

    EXPECT_TRUE(tok.has_attr(pool, BuiltinAttrKind::kConst));
    EXPECT_TRUE(tok.has_attr(pool, BuiltinAttrKind::kPure));
}

// ── Attr::to_string tests ──

TEST(AttrToStringTest, NoParams) {
    Attr attr;
    attr.m_name = "const";
    EXPECT_EQ(attr.to_string(), "const");
}

TEST(AttrToStringTest, SingleIntParam) {
    Attr attr;
    attr.m_name = "align";
    attr.m_params = std::vector<AttrParam>{AttrParam(int64_t(16))};
    EXPECT_EQ(attr.to_string(), "align(16)");
}

TEST(AttrToStringTest, SingleStringParam) {
    Attr attr;
    attr.m_name = "trust";
    attr.m_params = std::vector<AttrParam>{AttrParam(std::string_view("x > 0"))};
    EXPECT_EQ(attr.to_string(), "trust(\"x > 0\")");
}

TEST(AttrToStringTest, MultipleParams) {
    Attr attr;
    attr.m_name = "multi";
    attr.m_params = std::vector<AttrParam>{AttrParam(int64_t(1)), AttrParam(std::string_view("hello")), AttrParam(int64_t(42))};
    EXPECT_EQ(attr.to_string(), "multi(1, \"hello\", 42)");
}

TEST(AttrToStringTest, RangeParam) {
    Attr attr;
    attr.m_name = "require";
    MapperLocation begin = MapperLocation::makeLoc(MapperFile::make_input(0), 100);
    MapperLocation end = MapperLocation::makeLoc(MapperFile::make_input(0), 200);
    attr.m_params = std::vector<AttrParam>{AttrParam(MapperRange(begin, end))};
    EXPECT_EQ(attr.to_string(), "require([100..200])");
}

TEST(AttrToStringTest, EmptyParams) {
    Attr attr;
    attr.m_name = "empty_parens";
    attr.m_params = std::vector<AttrParam>{}; // empty vector
    EXPECT_EQ(attr.to_string(), "empty_parens");
}

} // namespace trust