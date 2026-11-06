// test/unit/transpiler/cpp_template_params_test.cpp
// ЕДИНЫЙ helper типовых параметров объявления (cppTemplateParams): критерий «объявление —
// шаблон» — непустой `names`. nullopt и пустой список дают пустой результат; nullptr-узел
// пропускается (не даёт имени, но и не ломает шаблонность).

#include "transpiler/emit/decl_emit_common.hpp"
#include "ast/ast_nodes.hpp"
#include "attrs/attr_pool.hpp"

#include "gtest/gtest.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace trust {
namespace {

std::vector<AstNodePtr> params(std::initializer_list<AstNodePtr> items) {
    return std::vector<AstNodePtr>(items);
}

TEST(CppTemplateParams, AbsentParamsAreNotATemplate) {
    const CppTemplateParams tpl = cppTemplateParams(std::optional<std::vector<AstNodePtr>>{});
    EXPECT_TRUE(tpl.header.empty());
    EXPECT_TRUE(tpl.names.empty());
    EXPECT_TRUE(tpl.packs.empty());
}

TEST(CppTemplateParams, EmptyParamsAreNotATemplate) {
    const std::vector<AstNodePtr> empty;
    const CppTemplateParams tpl = cppTemplateParams(empty);
    EXPECT_TRUE(tpl.header.empty());
    EXPECT_TRUE(tpl.names.empty());
}

TEST(CppTemplateParams, TypedParamsProduceHeader) {
    std::optional<std::vector<AstNodePtr>> p = params({std::make_shared<ArgNode>(std::string("T")), std::make_shared<ArgNode>(std::string("U"))});
    const CppTemplateParams tpl = cppTemplateParams(p);
    EXPECT_EQ(tpl.header, "template <typename T, typename U>");
    ASSERT_EQ(tpl.names.size(), 2u);
    EXPECT_EQ(tpl.names[0], "T");
    EXPECT_EQ(tpl.names[1], "U");
    EXPECT_EQ(tpl.packs, (std::vector<bool>{false, false}));
}

TEST(CppTemplateParams, TypePackIsTrailing) {
    auto pack = std::make_shared<ArgNode>(std::string("T"));
    pack->isTypePack = true;
    std::optional<std::vector<AstNodePtr>> p = params({pack});
    const CppTemplateParams tpl = cppTemplateParams(p);
    EXPECT_EQ(tpl.header, "template <typename... T>");
    EXPECT_EQ(tpl.packs, (std::vector<bool>{true}));
}

TEST(CppTemplateParams, NullNodeIsSkippedAndDoesNotBreakTemplate) {
    std::optional<std::vector<AstNodePtr>> p = params({nullptr, std::make_shared<ArgNode>(std::string("T"))});
    const CppTemplateParams tpl = cppTemplateParams(p);
    EXPECT_EQ(tpl.header, "template <typename T>");
    ASSERT_EQ(tpl.names.size(), 1u);
    EXPECT_EQ(tpl.names[0], "T");
}

// R5: «шаблонность» метода — тот же критерий, что у cppTemplateParams (непустые имена), а не
// «непустой список узлов». Вырожденный вход {nullptr} методом-шаблоном НЕ является.
TEST(CppTemplateParams, MethodBodyMustBeInHeaderUsesSingleTemplateCriterion) {
    FuncDecl func(std::string("f"));
    const AttrPool attrs;
    EXPECT_FALSE(methodBodyMustBeInHeader(func, /*recordIsTemplate=*/false, attrs));
    func.m_templateParams = std::vector<AstNodePtr>{nullptr};
    EXPECT_FALSE(methodBodyMustBeInHeader(func, /*recordIsTemplate=*/false, attrs));
    func.m_templateParams = std::vector<AstNodePtr>{std::make_shared<ArgNode>(std::string("T"))};
    EXPECT_TRUE(methodBodyMustBeInHeader(func, /*recordIsTemplate=*/false, attrs));
    EXPECT_TRUE(methodBodyMustBeInHeader(func, /*recordIsTemplate=*/true, attrs));
}

} // namespace
} // namespace trust
