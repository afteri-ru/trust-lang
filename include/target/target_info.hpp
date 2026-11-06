// Целевой контракт прогона: форма значений целевой платформы (R37/E20).
// Владелец сохранённого значения — `TargetContract` (компонент `target`); стадии бэкенда читают
// контракт, а не `PipelineOpts`. Layout/ABI/mangling здесь НЕ моделируются (R4): они принадлежат
// инструментарию целевого бэкенда.
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace trust {

/// Стандарт целевого C++ по умолчанию — единственный источник значения в компиляторе: оно входит в
/// контракт и попадает в `build.conf` через `toolchain::BuildRequest`.
inline constexpr std::string_view kDefaultCppStandard = "c++23";

/// Целевая платформа и её контракт для бэкендов. Значение считается один раз при создании контракта.
struct TargetInfo {
    std::string triple;                                          ///< целевая платформа; пусто — хост
    std::string cpp_standard = std::string(kDefaultCppStandard); ///< стандарт целевого C++
    std::vector<std::string> features;                           ///< включённые целевые возможности
    std::string provider;                                        ///< идентичность провайдера сборки (компилятор)
};

} // namespace trust