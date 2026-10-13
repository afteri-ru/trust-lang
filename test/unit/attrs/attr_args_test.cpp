// test/unit/attrs/attr_args_test.cpp - тесты единого чтения аргументов атрибутов (attr_args.hpp).
#include "attrs/attr_args.hpp"
#include "attrs/attr_builtin.hpp"
#include "ast/ast_nodes.hpp"
#include "attrs/attr_pool.hpp"
#include <gtest/gtest.h>
#include <stdexcept>
#include <string>

namespace trust {

namespace {

/// Узел с атрибутом `Include` и заданными аргументами (атрибут присутствует всегда).
FuncDecl makeNodeWithInclude(AttrPool& pool, std::vector<std::string> args) {
    FuncDecl node("f");
    const auto id = pool.lookup(attr::Include);
    EXPECT_TRUE(id.has_value());
    node.add_attr(*id);
    node.set_attr_args(*id, std::move(args));
    return node;
}

} // namespace

TEST(AttrArgsFirstArg, AbsentAttributeReturnsNullptr) {
    AttrPool pool;
    const FuncDecl node("f");
    EXPECT_EQ(firstAttrArg(pool, node, attr::Include), nullptr);
}

TEST(AttrArgsFirstArg, PresentAttributeWithoutArgsReturnsNullptr) {
    AttrPool pool;
    FuncDecl node("f");
    const auto id = pool.lookup(attr::Include);
    ASSERT_TRUE(id.has_value());
    node.add_attr(*id);
    EXPECT_EQ(firstAttrArg(pool, node, attr::Include), nullptr);
}

TEST(AttrArgsFirstArg, EmptyFirstArgReturnsNullptr) {
    AttrPool pool;
    const FuncDecl node = makeNodeWithInclude(pool, {"", "utility"});
    EXPECT_EQ(firstAttrArg(pool, node, attr::Include), nullptr);
}

TEST(AttrArgsFirstArg, ReturnsFirstArg) {
    AttrPool pool;
    const FuncDecl node = makeNodeWithInclude(pool, {"vector"});
    const std::string* arg = firstAttrArg(pool, node, attr::Include);
    ASSERT_NE(arg, nullptr);
    EXPECT_EQ(*arg, "vector");
}

TEST(AttrArgsFirstArg, ReturnsFirstOfSeveralArgs) {
    AttrPool pool;
    const FuncDecl node = makeNodeWithInclude(pool, {"a.hpp", "b.hpp"});
    const std::string* arg = firstAttrArg(pool, node, attr::Include);
    ASSERT_NE(arg, nullptr);
    EXPECT_EQ(*arg, "a.hpp");
}

TEST(AttrArgsIncludeDirective, AngleBracketsAreFullDirective) {
    EXPECT_EQ(includeDirective("<vector>"), "#include <vector>");
}

TEST(AttrArgsIncludeDirective, QuotesAreQuotedInclude) {
    EXPECT_EQ(includeDirective("\"local.hpp\""), "#include \"local.hpp\"");
}

TEST(AttrArgsIncludeDirective, AtPrefixIsPackageHeader) {
    EXPECT_EQ(includeDirective("@stdlib/async/thread_pool.hpp"), "@stdlib/async/thread_pool.hpp");
}

TEST(AttrArgsIncludeDirective, BareNameIsAngleInclude) {
    EXPECT_EQ(includeDirective("vector"), "#include <vector>");
}

TEST(AttrArgsIncludeDirective, EmptyHeaderIsInternalError) {
    EXPECT_THROW(includeDirective(""), std::runtime_error);
}

} // namespace trust
