// Тесты ядра отладочного вывода компилятора (include/utils/trace.hpp):
//   - разбор __FILE__ в токены фильтрации (root/path/component/rel/stem/file), релативизация;
//   - glob-матчер и списки масок;
//   - match(Site, tags) и обязательная фильтрация (без фильтра - вывод пуст);
//   - макрос TRUST_DEBUG (dev-сборка), печать `path:line: msg`.

#include "utils/io.hpp"
#include "utils/trace.hpp"

#include <gtest/gtest.h>

#include <sstream>
#include <string>

namespace {

using trust::trace::globMatch;
using trust::trace::maskListMatches;
using trust::trace::Site;

// -- Site: разбор путей (constexpr) --

constexpr Site kAbsoluteSrc{"/home/user/trust-lang/src/semantic/name_resolution.cpp"};
static_assert(kAbsoluteSrc.root == "src");
static_assert(kAbsoluteSrc.path == "src/semantic/name_resolution.cpp");
static_assert(kAbsoluteSrc.component == "semantic");
static_assert(kAbsoluteSrc.rel == "semantic/name_resolution.cpp");
static_assert(kAbsoluteSrc.stem == "name_resolution");
static_assert(kAbsoluteSrc.file == "name_resolution.cpp");

constexpr Site kRelativeTest{"test/unit/utils/trace_test.cpp"};
static_assert(kRelativeTest.root == "test");
static_assert(kRelativeTest.path == "test/unit/utils/trace_test.cpp");
static_assert(kRelativeTest.component == "utils");
static_assert(kRelativeTest.rel == "utils/trace_test.cpp");
static_assert(kRelativeTest.file == "trace_test.cpp");

TEST(TraceSite, ParsesAbsoluteSrcPath) {
    EXPECT_EQ(kAbsoluteSrc.root, "src");
    EXPECT_EQ(kAbsoluteSrc.component, "semantic");
    EXPECT_EQ(kAbsoluteSrc.stem, "name_resolution");
}

TEST(TraceSite, ParsesIncludeZone) {
    const Site s{"include/semantic/debug_scope.hpp"};
    EXPECT_EQ(s.root, "include");
    EXPECT_EQ(s.component, "semantic");
    EXPECT_EQ(s.path, "include/semantic/debug_scope.hpp");
    EXPECT_EQ(s.file, "debug_scope.hpp");
}

TEST(TraceSite, RelativePathKeepsComponentAtProjectRoot) {
    // Односегментный относительный путь: компонент = корневой сегмент.
    const Site s{"top.cpp"};
    EXPECT_EQ(s.root, "");
    EXPECT_EQ(s.path, "top.cpp");
    EXPECT_EQ(s.file, "top.cpp");
    EXPECT_EQ(s.stem, "top");
    EXPECT_EQ(s.rel, "top.cpp");
}

TEST(TraceSite, FallbackForNonAnchoredPath) {
    // Нет корневой зоны проекта: берём последние два сегмента.
    const Site s{"/opt/build/gen/parser.yy.cpp"};
    EXPECT_EQ(s.root, "gen");
    EXPECT_EQ(s.path, "gen/parser.yy.cpp");
    EXPECT_EQ(s.component, "gen");
    EXPECT_EQ(s.file, "parser.yy.cpp");
}

TEST(TraceSite, AbsoluteAndRelativeAgreeOnTokens) {
    // Абсолютный префикс сборки не влияет на относительные токены (детерминизм).
    const Site abs{"/home/user/trust-lang/src/semantic/name_resolution.cpp"};
    const Site rel{"src/semantic/name_resolution.cpp"};
    EXPECT_EQ(abs.path, rel.path);
    EXPECT_EQ(abs.rel, rel.rel);
    EXPECT_EQ(abs.component, rel.component);
    EXPECT_EQ(abs.root, rel.root);
}

// -- glob / маски --

TEST(TraceGlob, Wildcards) {
    EXPECT_TRUE(globMatch("*", "anything"));
    EXPECT_TRUE(globMatch("src*", "src/semantic/x.cpp"));
    EXPECT_TRUE(globMatch("*/name_resolution.cpp", "src/semantic/name_resolution.cpp"));
    EXPECT_TRUE(globMatch("name???", "name123"));
    EXPECT_FALSE(globMatch("src*", "include/x.cpp"));
    EXPECT_FALSE(globMatch("name?4", "name123"));
}

TEST(TraceGlob, MaskListSplitsOnCommaAndTrims) {
    EXPECT_TRUE(maskListMatches("src*, include", "include"));
    EXPECT_TRUE(maskListMatches(" a , b ", "b"));
    EXPECT_FALSE(maskListMatches("a,b", "c"));
    EXPECT_FALSE(maskListMatches("", "c"));
}

// -- Фильтрация (обязательная) --

class TraceFilterTest : public ::testing::Test {
  protected:
    void SetUp() override { trust::trace::clearFilter(); }
    void TearDown() override { trust::trace::clearFilter(); }
};

TEST_F(TraceFilterTest, NoFilterMeansDisabled) {
    EXPECT_FALSE(trust::trace::enabled());
    EXPECT_FALSE(trust::trace::match(kAbsoluteSrc, "scope"));
}

TEST_F(TraceFilterTest, MatchByRoot) {
    trust::trace::setFilter("src");
    EXPECT_TRUE(trust::trace::enabled());
    EXPECT_TRUE(trust::trace::match(kAbsoluteSrc, ""));
    EXPECT_FALSE(trust::trace::match(kRelativeTest, ""));
}

TEST_F(TraceFilterTest, MatchByPathAndComponent) {
    trust::trace::setFilter("src/semantic/*");
    EXPECT_TRUE(trust::trace::match(kAbsoluteSrc, ""));

    trust::trace::setFilter("semantic");
    EXPECT_TRUE(trust::trace::match(kAbsoluteSrc, ""));
    EXPECT_FALSE(trust::trace::match(kRelativeTest, ""));
}

TEST_F(TraceFilterTest, MatchByStemAndFile) {
    trust::trace::setFilter("name_resolution");
    EXPECT_TRUE(trust::trace::match(kAbsoluteSrc, ""));

    trust::trace::setFilter("*.cpp");
    EXPECT_TRUE(trust::trace::match(kAbsoluteSrc, ""));
}

TEST_F(TraceFilterTest, MatchByExplicitTags) {
    trust::trace::setFilter("scope");
    // Авто-токены не совпадают, но явный тег - да.
    EXPECT_TRUE(trust::trace::match(kAbsoluteSrc, "ast,scope,memory"));
    EXPECT_FALSE(trust::trace::match(kAbsoluteSrc, "ast,memory"));
}

TEST_F(TraceFilterTest, EmptyFilterAfterNonEmptyDisables) {
    trust::trace::setFilter("src");
    EXPECT_TRUE(trust::trace::enabled());
    trust::trace::setFilter("");
    EXPECT_FALSE(trust::trace::enabled());
}

// -- Макрос TRUST_DEBUG (только в dev-сборке) --

#if TRUST_TRACE_ENABLED
TEST_F(TraceFilterTest, MacroPrintsToErrsWhenMatched) {
    trust::trace::setFilter("*");
    std::ostringstream oss;
    std::ostream* prev = trust::setErrs(&oss);
    TRUST_DEBUG("probe", "value={} name={}", 42, "x");
    trust::setErrs(prev);

    const std::string out = oss.str();
    EXPECT_NE(out.find("value=42 name=x"), std::string::npos);
    EXPECT_NE(out.find("trace_test.cpp:"), std::string::npos);
}

TEST_F(TraceFilterTest, MacroSilentWhenFilterDoesNotMatch) {
    trust::trace::setFilter("no-such-tag");
    std::ostringstream oss;
    std::ostream* prev = trust::setErrs(&oss);
    TRUST_DEBUG("probe", "should-not-print");
    trust::setErrs(prev);
    EXPECT_TRUE(oss.str().empty());
}

TEST_F(TraceFilterTest, MacroSilentWithoutFilter) {
    std::ostringstream oss;
    std::ostream* prev = trust::setErrs(&oss);
    TRUST_DEBUG("probe", "should-not-print");
    trust::setErrs(prev);
    EXPECT_TRUE(oss.str().empty());
}
#endif

} // namespace
