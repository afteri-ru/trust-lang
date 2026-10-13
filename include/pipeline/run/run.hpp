// include/pipeline/run/run.hpp
// Запуск собранного исполняемого файла (--run) и кеш по md5 исходников.
// Выделено из pipeline.cpp (модуль RunManager).
#pragma once

#include "driver/options.hpp"
#include "location/module_index.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace trust {

class Context;
class ModuleLoader;

/// Замыкание зависимостей программы, НЕ покрытых списком её модулей: пакеты ресурсов
/// (stdlib и provider-пакеты) и явный DSL-файл. ЕДИНЫЙ источник для ключа кеша `--run`
/// (`buildProgramRecord`) и для графа зависимостей `--emit-module-deps`.
/// Пути канонические, без дублей, в порядке первого использования.
[[nodiscard]] std::vector<std::filesystem::path> programDependencyArchives(const ModuleLoader& loader, const PipelineOpts& opts);

/// Запись кеша --run: версия компилятора + фингерпринт codegen-опций + "файл\tmd5" для главного файла и модулей.
/// Пути относительны CWD. Используется и buildProgramRecord (run.cpp), и записью C++-артефакта (emitCppArtifact).
std::string buildProgramRecord(const std::filesystem::path& mainFile, Context& ctx, const ModuleLoader& loader, ModuleIndex mainIdx, const PipelineOpts& opts);

/// Запускает собранный исполняемый файл --run (путь -o, иначе <build_dir>/<stem>).
int runBuiltExecutable(const PipelineOpts& opts, const std::filesystem::path& cpptPath);

/// --run: если кеш по md5 исходников валиден и exe существует — запустить без перекомпиляции.
/// Возвращает код возврата программы, если кеш применим; иначе nullopt → перекомпиляция.
std::optional<int> tryRunCached(const PipelineOpts& opts);

} // namespace trust
