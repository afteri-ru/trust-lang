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
/// Генерирует C++-код главной функции `int main` (инклюды + extern + тело), идущий ПОСЛЕ
/// тела модуля. Используется: (1) многофайловым путём - как содержимое отдельного
/// `_main.cppt` (writeBuildFiles); (2) однофайловым режимом `-fsingle-file` - встраивается
/// в тот же `.cppt` модуля (main-обёртка в одной TU, отдельный `_main.cppt` не нужен).
/// usesStackCheck/usesSync - из рантайм-заголовков, реально использованных кодом.
std::string buildEntryMainSource(const PipelineOpts& opts, bool usesStackCheck, bool usesSync,
                                 const std::string& entry_func_name, const std::string& entry_params);

bool writeBuildFiles(const PipelineOpts& opts, const std::filesystem::path& cppt_path, const std::vector<std::filesystem::path>& module_cppt_paths,
                     const std::vector<std::string>& runtime_headers, const std::vector<std::string>& link_libs, const std::string& entry_func_name,
                     const std::string& entry_params);

/// writeBuildFiles + запуск make для цели compile_mode и копирование артефакта
/// в opts.output_file (если задан). false при ошибке компиляции/линковки.
bool compileAndLink(const PipelineOpts& opts, const std::filesystem::path& cppt_path, const std::vector<std::filesystem::path>& module_cppt_paths,
                    const std::vector<std::string>& runtime_headers, const std::vector<std::string>& link_libs, const std::string& entry_func_name,
                    const std::string& entry_params);

/// build_dir = temp_dir (если задан) или <каталог исходника>/.trust/<stem> (для --run — <cwd>/.trust/<stem>).
std::filesystem::path computeBuildDir(const PipelineOpts& opts);

/// cppt_path = build_dir / <input_stem>.cppt
std::filesystem::path computeCpptPath(const PipelineOpts& opts);

} // namespace trust
