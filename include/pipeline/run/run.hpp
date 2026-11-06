// Замыкание зависимостей программы и запись кеша `--run` (шапка `.cppt`/ELF-секция).
// Запуск собранного файла и валидация кеша — в драйвере `builddriver`.
#pragma once

#include "driver/options.hpp"
#include "location/module_index.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace trust {

class ModuleLoader;

/// Замыкание зависимостей программы, НЕ покрытых списком её модулей: пакеты ресурсов
/// (stdlib и provider-пакеты) и явный DSL-файл. ЕДИНЫЙ источник для ключа кеша `--run`
/// (`buildProgramRecord`) и для графа зависимостей `--emit-module-deps`.
/// Пути канонические, без дублей, в порядке первого использования.
[[nodiscard]] std::vector<std::filesystem::path> programDependencyArchives(const ModuleLoader& loader, const PipelineOpts& opts);

/// Запись кеша --run: версия компилятора + фингерпринт codegen-опций + "файл\tmd5" для главного файла и модулей.
/// Пути относительны CWD. Используется и buildProgramRecord (run.cpp), и записью C++-артефакта (emitCppArtifact).
std::string buildProgramRecord(const std::filesystem::path& mainFile, const ModuleLoader& loader, ModuleIndex mainIdx, const PipelineOpts& opts);

/// Запуск собранного файла и кеш `--run` перенесены в драйвер (`builddriver`).

} // namespace trust
