#include "symbols/decl/module_context.hpp"

#include "gtest/gtest.h"

#include <stdexcept>

namespace trust {
namespace {

// -- ModuleContext: узел-корень модуля и private-область -------------------------------------

TEST(ModuleContext, DefaultsWithoutModuleNode) {
    ModuleContext ctx;
    EXPECT_FALSE(ctx.hasModuleRoot());
    EXPECT_EQ(ctx.moduleRootNode(), 0u);
    // По умолчанию текущий модуль — индекс 0 (валидный: главный модуль), пока не задан иной.
    EXPECT_EQ(ctx.currentModule().value, 0u);
    EXPECT_FALSE(ctx.inFilePrivateScope(/*atModuleLevel=*/true));
    EXPECT_FALSE(ctx.inFilePrivateScope(/*atModuleLevel=*/false));
}

TEST(ModuleContext, ModuleRootNodeIsFirstModuleScope) {
    ModuleContext ctx;
    ctx.enterModuleScope(1);
    EXPECT_TRUE(ctx.hasModuleRoot());
    EXPECT_EQ(ctx.moduleRootNode(), 1u);
    // Повторный вход не перезаписывает корень (вложенные ModuleNode не двигают уровень).
    ctx.enterModuleScope(3);
    EXPECT_EQ(ctx.moduleRootNode(), 1u);

    // Выход с узлом, который не является зафиксированным корнем, — внутренняя ошибка.
    EXPECT_THROW(ctx.leaveModuleScope(3), std::runtime_error);
    EXPECT_EQ(ctx.moduleRootNode(), 1u);
    ctx.leaveModuleScope(1);
    EXPECT_FALSE(ctx.hasModuleRoot());
    EXPECT_EQ(ctx.moduleRootNode(), 0u);
}

TEST(ModuleContext, FilePrivateRequiresModuleLevel) {
    ModuleContext ctx;
    ctx.enterFilePrivate();
    EXPECT_TRUE(ctx.inFilePrivateScope(/*atModuleLevel=*/true));
    EXPECT_FALSE(ctx.inFilePrivateScope(/*atModuleLevel=*/false));
    ctx.leaveFilePrivate();
    EXPECT_FALSE(ctx.inFilePrivateScope(/*atModuleLevel=*/true));
}

TEST(ModuleContext, CurrentModuleIsSingleWritePoint) {
    ModuleContext ctx;
    ctx.setCurrentModule(ModuleIndex{7});
    ASSERT_TRUE(ctx.currentModule().isValid());
    EXPECT_EQ(ctx.currentModule().value, 7u);
}

} // namespace
} // namespace trust
