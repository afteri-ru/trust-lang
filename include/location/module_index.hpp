// include/location/module_index.hpp
// Идентичность модуля в пределах ОДНОЙ компиляции - индекс записи в ModuleRegistry
// (in-process, не ABI-идентичность и не межпрогонный ключ).
//
// Strong type: модульный индекс нельзя перепутать с любым другим std::size_t
// (размер, позиция, registry_index типа). Размещён в нижнем слое `location`, потому что
// его используют и `syntax`/`ast` (Term/ModuleNode), и `types` (storage-ключ), а прямых
// зависимостей между этими слоями нет.
#pragma once

#include <compare>
#include <cstddef>
#include <functional>

namespace trust {

struct ModuleIndex {
    /// Значение «модуля нет»: 0 — ВАЛИДНЫЙ индекс (главный модуль), поэтому нулём
    /// отсутствие модуля выразить нельзя.
    static constexpr std::size_t kInvalidValue = static_cast<std::size_t>(-1);

    /// Индекс отсутствующего модуля (прелюдия/DSL, native-сущности, root без заголовка).
    [[nodiscard]] static constexpr ModuleIndex invalid() noexcept { return ModuleIndex{kInvalidValue}; }

    std::size_t value{0};

    [[nodiscard]] constexpr bool isValid() const noexcept { return value != kInvalidValue; }
    [[nodiscard]] constexpr bool operator==(const ModuleIndex&) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const ModuleIndex&) const noexcept = default;
};

} // namespace trust

template <>
struct std::hash<trust::ModuleIndex> {
    [[nodiscard]] std::size_t operator()(const trust::ModuleIndex& index) const noexcept { return std::hash<std::size_t>{}(index.value); }
};
