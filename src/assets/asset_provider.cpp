// src/assets/asset_provider.cpp - embeds the compiler-side assets (standard library) and
// provides their content. Runtime built-in headers are NOT here: they are embedded into
// trust-runtime (src/runtime/trust_headers.cpp) and extracted by pipeline/runtime_locator.cpp.
//
// All arrays are char with a trailing NUL so the content is easy to treat as text.
// The paths are relative to this source file (src/assets/ -> include/stdlib/): clang's
// `#embed` resolves quoted names against the including file's directory and does not
// search -I include paths.

#include "assets/asset_provider.hpp"

#include "utils/error.hpp"

namespace trust {

namespace {

static constexpr char kStdlibDslSrcData[] = {
#embed "../../include/stdlib/dsl.src"
    , 0};

static constexpr char kStdlibIteratorSrcData[] = {
#embed "../../include/stdlib/iterator.src"
    , 0};

static constexpr char kStdlibAnyIteratorHppData[] = {
#embed "../../include/stdlib/any_iterator.hpp"
    , 0};

static constexpr char kStdlibIteratorHppData[] = {
#embed "../../include/stdlib/iterator.hpp"
    , 0};

static constexpr char kStdlibGeneratorHppData[] = {
#embed "../../include/stdlib/generator.hpp"
    , 0};

static constexpr char kStdlibTensorHppData[] = {
#embed "../../include/stdlib/tensor.hpp"
    , 0};

} // namespace

std::string_view embeddedAssetContent(AssetId id) {
    if (assetSource(id) != AssetSource::kCompilerEmbedded) {
        FAULT("embeddedAssetContent: asset '{}' is not compiler-embedded (runtime ELF asset)", assetName(id));
    }
    switch (id) {
    case AssetId::kStdlibDslSrc:
        return std::string_view(kStdlibDslSrcData, sizeof(kStdlibDslSrcData) - 1);
    case AssetId::kStdlibIteratorSrc:
        return std::string_view(kStdlibIteratorSrcData, sizeof(kStdlibIteratorSrcData) - 1);
    case AssetId::kStdlibAnyIteratorHpp:
        return std::string_view(kStdlibAnyIteratorHppData, sizeof(kStdlibAnyIteratorHppData) - 1);
    case AssetId::kStdlibIteratorHpp:
        return std::string_view(kStdlibIteratorHppData, sizeof(kStdlibIteratorHppData) - 1);
    case AssetId::kStdlibGeneratorHpp:
        return std::string_view(kStdlibGeneratorHppData, sizeof(kStdlibGeneratorHppData) - 1);
    case AssetId::kStdlibTensorHpp:
        return std::string_view(kStdlibTensorHppData, sizeof(kStdlibTensorHppData) - 1);
    default:
        FAULT("embeddedAssetContent: asset '{}' has no embedded content", assetName(id));
    }
    return {};
}

// Компайлтайм-инварианты каталога: у каждого ассета непустое публикуемое имя; каждый
// compiler-embedded ассет имеет непустой массив содержимого.
namespace {
#define ASSET_NAME_NONEMPTY(name, path, src) static_assert(assetName(AssetId::name) != "");
TRUST_ASSET_LIST(ASSET_NAME_NONEMPTY)
#undef ASSET_NAME_NONEMPTY

static_assert(sizeof(kStdlibDslSrcData) > 1);
static_assert(sizeof(kStdlibIteratorSrcData) > 1);
static_assert(sizeof(kStdlibAnyIteratorHppData) > 1);
static_assert(sizeof(kStdlibIteratorHppData) > 1);
static_assert(sizeof(kStdlibGeneratorHppData) > 1);
static_assert(sizeof(kStdlibTensorHppData) > 1);
} // namespace

} // namespace trust
