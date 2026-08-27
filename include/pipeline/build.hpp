// include/pipeline/build.hpp
// Генерация build-каталога и компиляция/линковка сгенерированного .cppt через
// Makefile. Выделено из pipeline.cpp (модуль BuildManager): writeBuildFiles,
// compileAndLink и расчёт путей build_dir/cppt.
#pragma once

#include "pipeline/pipeline.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace trust {

/// Генерирует build-файлы (Makefile, build.conf, _main.cppt, LICENSE, trust/)
/// в build_dir рядом с .cppt. БЕЗ компиляции/линковки. Используется и `trust build`
/// (compileAndLink), и trust-lsp --emit-build-dir.
bool writeBuildFiles(const PipelineOpts& opts, const std::filesystem::path& cppt_path, const std::vector<std::filesystem::path>& module_cppt_paths,
                     const std::vector<std::string>& runtime_headers, const std::vector<std::string>& link_libs, const std::string& entry_func_name);

/// writeBuildFiles + запуск make для цели compile_mode и копирование артефакта
/// в opts.output_file (если задан). false при ошибке компиляции/линковки.
bool compileAndLink(const PipelineOpts& opts, const std::filesystem::path& cppt_path, const std::vector<std::filesystem::path>& module_cppt_paths,
                    const std::vector<std::string>& runtime_headers, const std::vector<std::string>& link_libs, const std::string& entry_func_name);

/// build_dir = temp_dir (если задан) или каталог входного файла (или <cwd>/.trust/<stem> для --run).
std::filesystem::path computeBuildDir(const PipelineOpts& opts);

/// cppt_path = build_dir / <input_stem>.cppt
std::filesystem::path computeCpptPath(const PipelineOpts& opts);

} // namespace trust
