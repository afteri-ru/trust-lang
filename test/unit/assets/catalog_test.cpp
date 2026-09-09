// test/unit/assets/catalog_test.cpp - тесты единого каталога ассетов (префиксы/источники).
// Инвариант слоёв: ЕДИНЫЙ каталог ассетов (assets/asset_catalog.hpp) не смешивает префиксы
// "stdlib/…" (ассеты стандартной библиотеки, вшиты в компилятор) и "trust/…" (заголовки
// встроенных типов, ELF-секции trust-runtime).
#include <gtest/gtest.h>

#include "assets/asset_catalog.hpp"
#include "assets/asset_provider.hpp"

#include <cstddef>
#include <string_view>

namespace {

using trust::AssetId;
using trust::AssetSource;

std::string_view nameOf(std::size_t i) {
    return trust::assetName(static_cast<AssetId>(i));
}

TEST(AssetsTest, EveryAssetIsPublishable) {
    for (std::size_t i = 0; i < static_cast<std::size_t>(AssetId::kCount); ++i) {
        const std::string_view name = nameOf(i);
        EXPECT_FALSE(name.empty()) << "asset " << i << " has an empty published name";
        EXPECT_TRUE(name.starts_with("stdlib/") || name.starts_with("trust/"))
            << "asset '" << name << "' must use a known prefix";
    }
}

TEST(AssetsTest, LookupByNameIsRoundTrip) {
    for (std::size_t i = 0; i < static_cast<std::size_t>(AssetId::kCount); ++i) {
        const auto id = static_cast<AssetId>(i);
        const auto found = trust::findAsset(nameOf(i));
        ASSERT_TRUE(found.has_value()) << "asset '" << nameOf(i) << "' is not found by its own name";
        EXPECT_EQ(*found, id);
    }
}

// Префиксы не смешиваются: stdlib-ассеты вшиты в компилятор, trust-ассеты — ELF-секции рантайма.
TEST(AssetsTest, PrefixesMapToSources) {
    for (std::size_t i = 0; i < static_cast<std::size_t>(AssetId::kCount); ++i) {
        const auto id = static_cast<AssetId>(i);
        const std::string_view name = nameOf(i);
        if (name.starts_with("stdlib/")) {
            EXPECT_TRUE(trust::isStdlibAsset(id)) << name;
            EXPECT_EQ(trust::assetSource(id), AssetSource::kCompilerEmbedded) << name;
            EXPECT_FALSE(trust::embeddedAssetContent(id).empty()) << name;
        } else {
            EXPECT_FALSE(trust::isStdlibAsset(id)) << name;
            EXPECT_EQ(trust::assetSource(id), AssetSource::kRuntimeElf) << name;
        }
    }
    EXPECT_FALSE(trust::findAsset("stdlib/unknown.hpp").has_value());
    EXPECT_FALSE(trust::findAsset("trust/unknown.hpp").has_value());
    EXPECT_FALSE(trust::findAsset("").has_value());
}

// Ассеты prelude на месте и ссылаются на stdlib-заголовок (не на @trust/).
TEST(AssetsTest, PreludeAssetsReferenceStdlibHeader) {
    const std::string_view dsl = trust::embeddedAssetContent(AssetId::kStdlibDslSrc);
    EXPECT_NE(dsl.find("assert"), std::string_view::npos) << "dsl.src must define the `assert` mnemonic";

    const std::string_view iter = trust::embeddedAssetContent(AssetId::kStdlibIteratorSrc);
    EXPECT_NE(iter.find("Iterator"), std::string_view::npos) << "iterator.src must declare Iterator<T>";
    EXPECT_NE(iter.find("@stdlib/any_iterator.hpp"), std::string_view::npos)
        << "iterator.src must include the stdlib header under the @stdlib/ prefix";
    EXPECT_EQ(iter.find("@trust/any_iterator.hpp"), std::string_view::npos)
        << "iterator.src must NOT reference the built-in-type prefix";
}

} // namespace
