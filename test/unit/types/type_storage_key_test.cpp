// test/unit/types/type_storage_key_test.cpp
// TypeSystem: канонический ключ регистрации для ВСЕХ видов типов (алиас/enum/variant/
// record/native class): хранение под storage-ключом, display-имя отдельно, проекции
// namespacePath/isFilePrivate, отсутствие затирания одноимённых типов из разных namespace.
#include "support/cpp_facts_test_util.hpp"
#include "types/type_system.hpp"
#include "types/names.hpp"
#include "types/type_id.hpp"
#include "session/context.hpp"
#include "transpiler/type/cpp_type_renderer.hpp"
#include "gtest/gtest.h"

#include <memory>

namespace trust {
namespace {

class TypeStorageKeyFixture : public ::testing::Test {
  protected:
    Context m_ctx;
    std::unique_ptr<TypeSystem> m_types;
    TargetContract m_target;
    void SetUp() override {
        m_types = std::make_unique<TypeSystem>(m_ctx.diag(), m_ctx.opts());
        m_ctx.setTypes(m_types.get());
        test::installTargetFacts(m_ctx, m_target);
    }
    TypeId int32Id() { return m_ctx.readTypes().getType("Int32"); }
};

TEST_F(TypeStorageKeyFixture, NamespacedAliasIsKeyedByStorageKey) {
    TypeSystem& reg = *m_types;
    const TypeId id = reg.registrar().registerType("MyInt", int32Id(), {}, {}, false, exportedKey("ns", "MyInt"));
    ASSERT_NE(id, INVALID_TYPE_ID);
    // Ключ реестра - qualified, display-имя - короткое.
    EXPECT_EQ(reg.read().findType(exportedKey("ns", "MyInt")), id);
    EXPECT_FALSE(reg.read().findType("MyInt").has_value());
    ASSERT_TRUE(reg.read().store().hasDescriptor(id));
    EXPECT_EQ(reg.read().store().nameOf(id), "MyInt");
    EXPECT_EQ(reg.read().store().namespacePath(id), "ns");
    EXPECT_FALSE(reg.read().store().isFilePrivate(id));
}

TEST_F(TypeStorageKeyFixture, SameSpellingInDifferentNamespacesCoexist) {
    TypeSystem& reg = *m_types;
    const TypeId a = reg.registrar().registerType("MyInt", int32Id(), {}, {}, false, exportedKey("ns", "MyInt"));
    const TypeId b = reg.registrar().registerType("MyInt", int32Id(), {}, {}, false, exportedKey("ns2", "MyInt"));
    ASSERT_NE(a, INVALID_TYPE_ID);
    ASSERT_NE(b, INVALID_TYPE_ID);
    EXPECT_NE(a, b);
    EXPECT_EQ(reg.read().findType(exportedKey("ns", "MyInt")), a);
    EXPECT_EQ(reg.read().findType(exportedKey("ns2", "MyInt")), b);
}

TEST_F(TypeStorageKeyFixture, DuplicateInsideSameNamespaceDiagnosed) {
    TypeSystem& reg = *m_types;
    ASSERT_NE(reg.registrar().registerType("MyInt", int32Id(), {}, {}, false, exportedKey("ns", "MyInt")), INVALID_TYPE_ID);
    EXPECT_EQ(reg.registrar().registerType("MyInt", int32Id(), {}, {}, false, exportedKey("ns", "MyInt")), INVALID_TYPE_ID);
}

TEST_F(TypeStorageKeyFixture, NamespacedEnumKeepsQualifiedKeyAndShortDisplay) {
    TypeSystem& reg = *m_types;
    const TypeId id = reg.registrar().registerEnumType("Color", int32Id(), {}, {}, false, exportedKey("ns", "Color"));
    ASSERT_NE(id, INVALID_TYPE_ID);
    EXPECT_EQ(reg.read().findType(exportedKey("ns", "Color")), id);
    EXPECT_FALSE(reg.read().findType("Color").has_value());
    EXPECT_EQ(reg.read().store().nameOf(id), "Color");
    EXPECT_EQ(reg.read().store().namespacePath(id), "ns");
}

TEST_F(TypeStorageKeyFixture, NamespacedVariantKeepsQualifiedKey) {
    TypeSystem& reg = *m_types;
    std::vector<VariantMemberData> members;
    members.push_back(VariantMemberData{std::string("RED"), int32Id()});
    const TypeId id = reg.registrar().registerVariantType("Value", std::move(members), {}, false, exportedKey("ns", "Value"));
    ASSERT_NE(id, INVALID_TYPE_ID);
    EXPECT_EQ(reg.read().findType(exportedKey("ns", "Value")), id);
    EXPECT_FALSE(reg.read().findType("Value").has_value());
}

TEST_F(TypeStorageKeyFixture, NamespacedNativeClassKeepsQualifiedKey) {
    TypeSystem& reg = *m_types;
    const TypeId id = test::registerNativeClassFacts(reg, m_target, "MyStr", "std::string", {}, {}, {}, exportedKey("ns", "MyStr"));
    ASSERT_NE(id, INVALID_TYPE_ID);
    EXPECT_EQ(reg.read().findType(exportedKey("ns", "MyStr")), id);
    EXPECT_FALSE(reg.read().findType("MyStr").has_value());
    EXPECT_EQ(reg.read().store().namespacePath(id), "ns");
}

TEST_F(TypeStorageKeyFixture, FilePrivateKeyExposed) {
    TypeSystem& reg = *m_types;
    const StorageKey key = moduleKey(ModuleIndex{3}, {}, "Point", /*importHidden=*/false);
    const TypeId id = reg.registrar().declareRecord("Point", Group::kStructs, {}, {}, false, key);
    ASSERT_NE(id, INVALID_TYPE_ID);
    EXPECT_TRUE(reg.read().store().isFilePrivate(id));
    EXPECT_TRUE(reg.read().store().namespacePath(id).empty());
    EXPECT_EQ(reg.read().findType(key), id);
}

TEST_F(TypeStorageKeyFixture, ImportHiddenKeyKeepsQualifiedCppName) {
    TypeSystem& reg = *m_types;
    const StorageKey key = moduleKey(ModuleIndex{4}, "ns", "Point", /*importHidden=*/true);
    const TypeId id = reg.registrar().declareRecord("Point", Group::kStructs, {}, {}, false, key);
    ASSERT_NE(id, INVALID_TYPE_ID);
    EXPECT_FALSE(reg.read().store().isFilePrivate(id)) << "import-hidden export is not file-private";
    EXPECT_EQ(reg.read().store().namespacePath(id), "ns");
    // «Неимпортируемый» модуль (stub) не даёт C++-префикса — имя остаётся qualified.
    auto cpp = CppTypeRenderer(reg.read(), test::cppFactsViewOf(m_ctx)).render(id);
    ASSERT_TRUE(cpp.has_value()) << cpp.error();
    EXPECT_EQ(*cpp, "ns::Point");
}

TEST_F(TypeStorageKeyFixture, ModuleExportKeyKeepsQualifiedName) {
    /// Модуль в C++-имя типа НЕ входит: имя = namespace-путь программиста.
    TypeSystem& reg = *m_types;
    const StorageKey key = moduleExportKey(ModuleIndex{4}, "ns", "Point");
    const TypeId id = reg.registrar().declareRecord("Point", Group::kStructs, {}, {}, false, key);
    ASSERT_NE(id, INVALID_TYPE_ID);
    EXPECT_EQ(reg.read().store().moduleOf(id), ModuleIndex{4});
    auto cpp = CppTypeRenderer(reg.read(), test::cppFactsViewOf(m_ctx)).render(id);
    ASSERT_TRUE(cpp.has_value()) << cpp.error();
    EXPECT_EQ(*cpp, "ns::Point");
}

TEST_F(TypeStorageKeyFixture, ForEachTypeYieldsStorageKeys) {
    TypeSystem& reg = *m_types;
    ASSERT_NE(reg.registrar().registerType("MyInt", int32Id(), {}, {}, false, exportedKey("ns", "MyInt")), INVALID_TYPE_ID);
    bool sawQualifiedKey = false;
    bool sawShortDisplay = false;
    reg.read().forEachType([&](const StorageKey& key, std::string_view display, bool userDefined) {
        if (!userDefined || display != "MyInt") {
            return;
        }
        sawQualifiedKey = sawQualifiedKey || (key.namespacePath == "ns" && key.name == "MyInt" && !key.isModuleQualified());
        sawShortDisplay = sawShortDisplay || key.namespacePath.empty();
    });
    EXPECT_TRUE(sawQualifiedKey) << "qualified storage key must be the iteration key";
    EXPECT_FALSE(sawShortDisplay);
}

} // namespace
} // namespace trust
