// test/unit/types/storage_key_test.cpp
// Структурный ключ имени (types/storage_key.hpp): видимость/модуль - поля, namespace-путь и
// имя - отдельные части, текст ключа - только проекция для вывода.
#include "types/names.hpp"
#include "utils/strings.hpp"
#include "gtest/gtest.h"

#include <string>

namespace trust {
namespace {

TEST(StorageKeyTest, ExportedKeySplitsQualifiedName) {
    const StorageKey key = exportedKeyOf("ns::sub::Point");
    EXPECT_EQ(key.visibility, VisibilityClass::Exported);
    EXPECT_FALSE(key.isModuleQualified());
    EXPECT_EQ(key.namespacePath, "ns::sub");
    EXPECT_EQ(key.name, "Point");
}

TEST(StorageKeyTest, GlobalNameHasEmptyPath) {
    const StorageKey key = exportedKeyOf("Point");
    EXPECT_FALSE(key.isModuleQualified());
    EXPECT_TRUE(key.namespacePath.empty());
    EXPECT_EQ(key.name, "Point");
}

TEST(StorageKeyTest, FilePrivateAndImportHiddenAreDistinct) {
    const StorageKey priv = moduleKey(ModuleIndex{7}, {}, "Point", /*importHidden=*/false);
    const StorageKey hidden = moduleKey(ModuleIndex{7}, {}, "Point", /*importHidden=*/true);
    const StorageKey otherModule = moduleKey(ModuleIndex{8}, {}, "Point", /*importHidden=*/false);
    EXPECT_EQ(priv.visibility, VisibilityClass::FilePrivate);
    EXPECT_EQ(hidden.visibility, VisibilityClass::ImportHidden);
    EXPECT_NE(priv, hidden);
    EXPECT_NE(priv, otherModule);
    EXPECT_NE(priv, exportedKeyOf("Point"));
    EXPECT_TRUE(priv.isModuleQualified());
    EXPECT_EQ(priv.module, ModuleIndex{7});
}

TEST(StorageKeyTest, StructurallyEqualKeysCompareEqualAcrossTextSources) {
    // Ключи сравниваются по содержимому частей, а не по адресам views.
    const std::string path = "ns";
    const std::string name = "f";
    EXPECT_EQ(exportedKey(path, name), exportedKeyOf("ns::f"));
    EXPECT_NE(exportedKey("ns", "f"), exportedKey("", "ns::f"));
}

TEST(StorageKeyTest, TextProjectionIsForOutputOnly) {
    EXPECT_EQ(storageKeyText(exportedKeyOf("ns::Point")), "ns::Point");
    EXPECT_EQ(storageKeyText(moduleKey(ModuleIndex{7}, {}, "Point", /*importHidden=*/false)), std::string(1, '\x1f') + "7" + '\x1f' + "Point");
    EXPECT_EQ(storageKeyText(moduleKey(ModuleIndex{12}, "ns", "f", /*importHidden=*/true)), std::string(1, '\x1f') + "12" + '\x1f' + "~ns::f");
}

TEST(StorageKeyTest, InternStorageKeyKeepsViewsAlive) {
    StorageKey key;
    {
        const std::string localPath = "ns::sub";
        const std::string localName = "Point";
        key = internStorageKey(exportedKey(localPath, localName));
    }
    // Строки-источники уничтожены: интернированные views остаются валидными.
    EXPECT_EQ(key.namespacePath, "ns::sub");
    EXPECT_EQ(key.name, "Point");
    EXPECT_EQ(storageKeyText(key), "ns::sub::Point");
}

TEST(StorageKeyTest, QualifyAndSplit) {
    EXPECT_EQ(utils::qualifyTrustName("ns::sub", "Point"), "ns::sub::Point");
    EXPECT_EQ(utils::qualifyTrustName("", "Point"), "Point");
    EXPECT_EQ(utils::shortNameOf("ns::sub::Point"), "Point");
    EXPECT_EQ(utils::namespacePathOf("ns::sub::Point"), "ns::sub");
    EXPECT_EQ(utils::shortNameOf("Point"), "Point");
    EXPECT_TRUE(utils::namespacePathOf("Point").empty());
}

} // namespace
} // namespace trust
