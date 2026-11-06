#include "ast/ast_nodes.hpp"
#include "location/decl_id.hpp"
#include "target/decl_cpp_facts.hpp"

#include <gtest/gtest.h>
#include <memory>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace trust {
namespace {

TEST(DefinitionKindTest, DefaultIsForward) {
    FuncDecl fn(std::string("f"));
    EXPECT_EQ(fn.definitionKind(), DefinitionKind::None); // payload пуст
    EXPECT_TRUE(fn.isForward());
    EXPECT_FALSE(fn.hasBody());
    EXPECT_FALSE(fn.hasImplementation());
    EXPECT_TRUE(fn.isForwardDeclaration());
}

TEST(DefinitionKindTest, BodyHasImplementationEvenWhenEmpty) {
    // Пустое тело `f() := {};` — ОПРЕДЕЛЕНИЕ (не forward): вид тела не зависит от наполненности вектора.
    FuncDecl fn(std::string("f"));
    fn.m_definition = Definition::fromBody({});
    EXPECT_TRUE(fn.hasBody());
    EXPECT_EQ(fn.definitionKind(), DefinitionKind::Body);
    EXPECT_TRUE(fn.hasImplementation());
    EXPECT_FALSE(fn.isForwardDeclaration());
    EXPECT_EQ(fn.body()->size(), 0u);
}

TEST(DefinitionKindTest, BodyVectorAccess) {
    FuncDecl fn(std::string("f"));
    auto stmt = std::make_shared<IdentName>(std::string("x"));
    std::vector<AstNodePtr> body;
    body.push_back(stmt);
    fn.m_definition = Definition::fromBody(std::move(body));
    EXPECT_TRUE(fn.hasBody());
    EXPECT_EQ(fn.body()->size(), 1u);
    EXPECT_EQ((*fn.body())[0].get(), stmt.get());
}

TEST(DefinitionKindTest, ForwardedToKind) {
    FuncDecl fn(std::string("g"));
    fn.m_definition = Definition::forwardTo("size");
    EXPECT_TRUE(fn.isDelegate());
    EXPECT_FALSE(fn.hasBody());
    EXPECT_TRUE(fn.hasImplementation());
    ASSERT_NE(fn.forwardingTarget(), nullptr);
    EXPECT_EQ(*fn.forwardingTarget(), "size");
    EXPECT_FALSE(fn.isNativeImport());
}

TEST(DefinitionKindTest, NativeCppNameIsSeparateFact) {
    FuncDecl fn(std::string("g"));
    fn.m_bindsNativeCpp = true;
    EXPECT_TRUE(fn.isNativeImport());
    EXPECT_TRUE(fn.bindsNativeCpp());
    EXPECT_FALSE(fn.isDelegate()); // нативная привязка — НЕ вид определения
    EXPECT_TRUE(fn.hasImplementation()); // импорт = реализация предоставлена внешним символом
    // C++-имя — ЦЕЛЕВОЙ факт хранилища `DeclCppFacts` (вне языковой модели, R1), не поле AST.
    DeclCppFacts facts;
    const DeclId id = DeclId::fromParts(1u, 1u);
    facts.setCppName(id, "std::sqrt");
    EXPECT_EQ(facts.cppNameOf(id), "std::sqrt");
}

TEST(DefinitionKindTest, NativeExportIsSeparateFact) {
    FuncDecl fn(std::string("fib"));
    fn.m_definition = Definition::fromBody({});
    fn.m_bindsNativeCpp = true;
    EXPECT_TRUE(fn.isNativeExport());
    EXPECT_FALSE(fn.isNativeImport());
    EXPECT_TRUE(fn.hasBody());
    EXPECT_TRUE(fn.hasImplementation());
}

TEST(DefinitionKindTest, ResetReturnsToForward) {
    FuncDecl fn(std::string("f"));
    fn.m_definition = Definition::fromBody({});
    fn.m_definition = Definition{};               // сброс к forward
    EXPECT_TRUE(fn.isForward());
    EXPECT_FALSE(fn.hasImplementation());
}

TEST(DefinitionKindTest, FuncDeclPredicatesAreDerived) {
    FuncDecl fn(std::string("g"));
    EXPECT_FALSE(fn.hasImplementation());
    EXPECT_TRUE(fn.isForwardDeclaration());

    fn.m_definition = Definition::forwardTo("h"); // переадресация — реализация
    EXPECT_TRUE(fn.hasImplementation());
    EXPECT_FALSE(fn.isForwardDeclaration());
}

// -- VarDecl: состояние инициализации — ПРОИЗВОДНЫЙ факт (S4.1′) ---------------------------

namespace {
AstNodePtr noneMarker() {
    return std::make_shared<IdentName>(std::string("_"));
}
AstNodePtr intType() {
    return std::make_shared<IdentType>(std::string("Int32"));
}
AstNodePtr intLiteral() {
    return std::make_shared<Literal>(ParserToken::Kind::IntLiteral, std::string("1"));
}
} // namespace

TEST(VarInitKindTest, FourStatesAreDerivedFromPayload) {
    VarDecl forward(std::string("fwd"), intType());
    EXPECT_EQ(forward.initKind(), VarInitKind::Forward);
    EXPECT_TRUE(forward.isForwardDeclaration());
    EXPECT_FALSE(forward.isDefinition());

    VarDecl uninit(std::string("u"), intType(), noneMarker());
    EXPECT_EQ(uninit.initKind(), VarInitKind::Uninitialized);
    EXPECT_FALSE(uninit.isForwardDeclaration());
    EXPECT_TRUE(uninit.isDefinition());
    EXPECT_EQ(uninit.initKind(), VarInitKind::Uninitialized); // экспорт требует реального значения

    VarDecl deferred(std::string("d"), nullptr, noneMarker());
    EXPECT_EQ(deferred.initKind(), VarInitKind::DeferredInit);
    EXPECT_TRUE(deferred.isDefinition());

    VarDecl initialized(std::string("i"), nullptr, intLiteral());
    EXPECT_EQ(initialized.initKind(), VarInitKind::Initialized);
}

TEST(VarInitKindTest, CatchBindingIsBoundNotForward) {
    VarDecl binding(std::string("e"), intType());
    binding.m_isCatchBinding = true;
    EXPECT_EQ(binding.initKind(), VarInitKind::Bound);
    EXPECT_FALSE(binding.isForwardDeclaration());
    EXPECT_TRUE(binding.isDefinition());
}

TEST(VarInitKindTest, NativeRoleFollowsInitKind) {
    // import: нативное имя без значения (любой «пустой» вид, кроме Initialized).
    VarDecl imported(std::string("ext"), intType());
    imported.m_bindsNativeCpp = true;
    EXPECT_EQ(imported.nativeRole(), Decl::NativeRole::Import);
    EXPECT_TRUE(imported.isNativeImport());
    EXPECT_FALSE(imported.isForwardDeclaration()); // импорт — не forward

    VarDecl importedNoInit(std::string("ext2"), intType(), noneMarker());
    importedNoInit.m_bindsNativeCpp = true;
    EXPECT_EQ(importedNoInit.nativeRole(), Decl::NativeRole::Import);

    // export: нативное имя + реальное значение.
    VarDecl exported(std::string("v"), nullptr, intLiteral());
    exported.m_bindsNativeCpp = true;
    EXPECT_EQ(exported.nativeRole(), Decl::NativeRole::Export);
    EXPECT_TRUE(exported.isNativeExport());
}

TEST(DefinitionKindTest, ForwardFactoryRejectsNativeTargetAndEmpty) {
    EXPECT_THROW(Definition::forwardTo(""), std::runtime_error);
    EXPECT_THROW(Definition::forwardTo("%size"), std::runtime_error);
}

TEST(DefinitionKindTest, NativeRoleOnFunctionDeclaration) {
    FuncDecl fn(std::string("g"));
    fn.m_bindsNativeCpp = true;
    EXPECT_EQ(fn.nativeRole(), Decl::NativeRole::Import);
    fn.m_definition = Definition::fromBody({});
    EXPECT_EQ(fn.nativeRole(), Decl::NativeRole::Export);
}

} // namespace
} // namespace trust