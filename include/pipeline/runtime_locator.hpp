// include/pipeline/runtime_locator.hpp
// Поиск и извлечение trust-runtime библиотеки (.so/.a) и её заголовков.
// Выделено из pipeline.cpp: RuntimeLocator-логика (имя файла, поиск по
// exe/LD_LIBRARY_PATH/cwd, извлечение заголовка из ELF-секции).
#pragma once

#include "pipeline/pipeline.hpp"

#include <filesystem>
#include <string>

namespace trust {

/// Имя файла рантайм-библиотеки: trust-runtime.a (static) или trust-runtime.so (shared).
std::filesystem::path runtimeLibraryFileName(RuntimeLink link);

/// Поиск рантайм-библиотеки: каталог реального exe → LD_LIBRARY_PATH → cwd.
/// Пустой путь = не найдена.
std::filesystem::path locateRuntimeLibrary(RuntimeLink link);

/// Извлекает рантайм-заголовок (headerPath) из библиотеки в <buildDir>/<headerPath>,
/// чтобы Makefile нашёл его по `#include "<path>"`. false при ошибке.
bool extractRuntimeHeader(const std::string& headerPath, const std::filesystem::path& buildDir, RuntimeLink link);

} // namespace trust
