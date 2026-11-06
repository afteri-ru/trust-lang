// Генерация build-каталога и компиляция/линковка сгенерированного .cppt через
// Makefile. Выделено из pipeline.cpp (модуль BuildManager).
#pragma once

#include "toolchain/build/build_request.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace trust {
namespace toolchain {

/// Генерирует C++-код главной функции `int main` (инклюды + extern + тело), идущий ПОСЛЕ
/// тела модуля. Используется: (1) многофайловым путём - как содержимое отдельного
/// `_main.cppt` (writeBuildFiles); (2) однофайловым режимом `-fsingle-file` - встраивается
/// в тот же `.cppt` модуля (main-обёртка в одной TU, отдельный `_main.cppt` не нужен).
/// usesStackCheck/usesSync - из рантайм-заголовков, реально использованных кодом.
std::string buildEntryMainSource(const PipelineOpts& opts, bool usesStackCheck, bool usesSync, const std::string& entry_func_name,
                                 const std::string& entry_params);

/// Однократно вычисляет входы сборки: все заголовки (включая синтетические) и LinkPlan.
/// Пути build_dir/stdlib выводятся из `spec.cpptPath`. false + out_error при ошибке резолва.
bool prepareBuildInputs(const BuildRequest& spec, BuildInputs& out, std::string& out_error);

/// Чистая сборка текста build.conf по уже вычисленным входам.
std::string composeBuildConf(const BuildRequest& spec, const BuildInputs& inputs, const std::filesystem::path& main_cppt_path);

/// Генерирует build-файлы (Makefile, build.conf, _main.cppt, LICENSE) и распаковывает
/// пакет стандартной библиотеки в `<build_dir>/stdlib`. БЕЗ компиляции/линковки.
/// `inputs` заполняется вычисленными входами для последующей сборки.
bool writeBuildFiles(const BuildRequest& spec, BuildInputs& inputs);

/// writeBuildFiles + запуск make для цели compile_mode и копирование артефакта
/// в opts.output_file (если задан). false при ошибке компиляции/линковки.
bool compileAndLink(const BuildRequest& spec);

} // namespace toolchain
} // namespace trust
