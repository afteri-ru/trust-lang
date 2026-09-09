#pragma once

// include/assets/asset_provider.hpp
// Провайдер содержимого ВСТРОЕННЫХ в компилятор ассетов (AssetSource::kCompilerEmbedded).
// Ассеты рантайма (AssetSource::kRuntimeElf) в бинарнике компилятора НЕ хранятся: их
// извлекает pipeline из ELF-секций trust-runtime (см. pipeline/runtime_locator.cpp),
// используя единый каталог assets/asset_catalog.hpp для диспетчеризации по источнику.

#include "assets/asset_catalog.hpp"

#include <cstdint>
#include <string_view>

namespace trust {

/// Содержимое встроенного ассета (без завершающего NUL). Для ассетов рантайма
/// (AssetSource::kRuntimeElf) — FAULT: их содержимое в компиляторе отсутствует.
/// Пустое содержимое встроенного ассета — FAULT (валидируется static_assert).
[[nodiscard]] std::string_view embeddedAssetContent(AssetId id);

} // namespace trust
