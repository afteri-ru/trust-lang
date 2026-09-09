#pragma once

// include/assets/asset_catalog.hpp
// ЕДИНЫЙ источник описания ВСЕХ встраиваемых артефактов компилятора:
//   - стандартная библиотека (`stdlib/…`, вшита в бинарник КОМПИЛЯТОРА через #embed);
//   - заголовки ВСТРОЕННЫХ типов (`trust/…`, ELF-секции trust-runtime).
// Префиксы НЕ смешиваются; принадлежность фиксируется полем SourceKind в таблице.
// Единый X-macro-список порождает enum AssetId, имя, источник, поиск по имени и
// компайлтайм-проверки покрытия. Опечатка в публикуемом имени — ошибка компиляции.

#include <cstdint>
#include <optional>
#include <string_view>

namespace trust {

/// Источник содержимого ассета.
enum class AssetSource : uint8_t {
    kCompilerEmbedded, ///< Вшит в бинарник компилятора (#embed); публикуется как `@stdlib/…`.
    kRuntimeElf,       ///< ELF-секция trust-runtime; публикуется как `@trust/…`.
};

// -- ЕДИНЫЙ список ассетов -------------------------------------------------
//   _(EnumId, "публикуемое имя", источник)
#define TRUST_ASSET_LIST(_)                                                \
    /* Стандартная библиотека (компилятор, #embed) */                      \
    _(kStdlibDslSrc, "stdlib/dsl.src", AssetSource::kCompilerEmbedded)     \
    _(kStdlibIteratorSrc, "stdlib/iterator.src", AssetSource::kCompilerEmbedded) \
    _(kStdlibAnyIteratorHpp, "stdlib/any_iterator.hpp", AssetSource::kCompilerEmbedded) \
    _(kStdlibIteratorHpp, "stdlib/iterator.hpp", AssetSource::kCompilerEmbedded) \
    _(kStdlibGeneratorHpp, "stdlib/generator.hpp", AssetSource::kCompilerEmbedded) \
    _(kStdlibTensorHpp, "stdlib/tensor.hpp", AssetSource::kCompilerEmbedded) \
    /* Заголовки встроенных типов (ELF-секции trust-runtime) */            \
    _(kTrustRationalHpp, "trust/rational.hpp", AssetSource::kRuntimeElf)   \
    _(kTrustBigIntegerHpp, "trust/big_integer.hpp", AssetSource::kRuntimeElf) \
    _(kTrustAssertHpp, "trust/assert.hpp", AssetSource::kRuntimeElf)       \
    _(kTrustCheckedCastHpp, "trust/checked_cast.hpp", AssetSource::kRuntimeElf) \
    _(kTrustIoHpp, "trust/io.hpp", AssetSource::kRuntimeElf)               \
    _(kTrustDictHpp, "trust/dict.hpp", AssetSource::kRuntimeElf)           \
    _(kTrustArgsHpp, "trust/args.hpp", AssetSource::kRuntimeElf)           \
    _(kTrustRangeHpp, "trust/range.hpp", AssetSource::kRuntimeElf)         \
    _(kTrustEnumHpp, "trust/enum.hpp", AssetSource::kRuntimeElf)           \
    _(kTrustAnyConvertHpp, "trust/any_convert.hpp", AssetSource::kRuntimeElf) \
    _(kTrustStackCheckHpp, "trust/stack_check.hpp", AssetSource::kRuntimeElf) \
    _(kTrustInterruptHpp, "trust/interrupt.hpp", AssetSource::kRuntimeElf) \
    _(kTrustTrustedCppHpp, "trust/trusted-cpp.hpp", AssetSource::kRuntimeElf) \
    _(kTrustTrustedCppSyncHpp, "trust/trusted-cpp-sync.hpp", AssetSource::kRuntimeElf) \
    _(kTrustResourceHpp, "trust/resource.hpp", AssetSource::kRuntimeElf)

/// Типизированные идентификаторы ассетов (генерируются из списка).
enum class AssetId : uint8_t {
#define ASSET_ENUM(name, path, src) name,
    TRUST_ASSET_LIST(ASSET_ENUM)
#undef ASSET_ENUM
        kCount,
};

/// Публикуемое имя (путь извлечения) ассета, например "stdlib/dsl.src".
constexpr std::string_view assetName(AssetId id) {
    switch (id) {
#define ASSET_NAME(name, path, src) \
    case AssetId::name:             \
        return path;
        TRUST_ASSET_LIST(ASSET_NAME)
#undef ASSET_NAME
    case AssetId::kCount:
        break;
    }
    return {};
}

/// Источник содержимого ассета.
constexpr AssetSource assetSource(AssetId id) {
    switch (id) {
#define ASSET_SOURCE(name, path, src) \
    case AssetId::name:               \
        return src;
        TRUST_ASSET_LIST(ASSET_SOURCE)
#undef ASSET_SOURCE
    case AssetId::kCount:
        break;
    }
    return AssetSource::kRuntimeElf;
}

/// Находит id ассета по публикуемому имени (любой источник). nullopt - имя не ассет.
constexpr std::optional<AssetId> findAsset(std::string_view name) {
    for (std::size_t i = 0; i < static_cast<std::size_t>(AssetId::kCount); ++i) {
        const auto id = static_cast<AssetId>(i);
        if (assetName(id) == name) {
            return id;
        }
    }
    return std::nullopt;
}

/// Ассет стандартной библиотеки (вшит в компилятор) - true.
constexpr bool isStdlibAsset(AssetId id) {
    return assetSource(id) == AssetSource::kCompilerEmbedded;
}

} // namespace trust
