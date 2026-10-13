// include/toolchain/runtime/runtime_locator.hpp
// Поиск trust-runtime библиотеки (.so/.a) для линковки сгенерированной программы.
#pragma once

#include "driver/options.hpp"

#include <filesystem>
#include <string>

namespace trust {
namespace toolchain {

/// Имя файла рантайм-библиотеки: trust-runtime.a (static) или trust-runtime.so (shared).
std::filesystem::path runtimeLibraryFileName(RuntimeLink link);

/// Поиск рантайм-библиотеки: каталог реального exe → LD_LIBRARY_PATH → cwd.
/// Пустой путь = не найдена.
std::filesystem::path locateRuntimeLibrary(RuntimeLink link);

/// Поиск toolchain-библиотеки (artifact kind="toolchain") по логическому имени:
/// `lib<name>.a`, `<name>.a`, `lib<name>.so`, `<name>.so` в каталоге реального exe,
/// `<exe>/lib`, `<exe>/../lib`, каталогах LD_LIBRARY_PATH и cwd.
/// Пустой путь = не найдена.
std::filesystem::path locateToolchainLibrary(std::string_view name);

} // namespace toolchain
} // namespace trust
