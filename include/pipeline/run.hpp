// include/pipeline/run.hpp
// Запуск собранного исполняемого файла (--run) и кеш по md5 исходников.
// Выделено из pipeline.cpp (модуль RunManager).
#pragma once

#include "pipeline/pipeline.hpp"

#include <filesystem>
#include <optional>
#include <string>

namespace trust {

/// Запись кеша --run: строка версии компилятора + "файл\tmd5" для главного файла и модулей.
/// Пути относительны CWD. Используется и buildProgramRecord (run.cpp), и runTranspileAndSave.
std::string buildProgramRecord(const std::filesystem::path& mainFile, Context& ctx, std::size_t mainIdx);

/// Запускает собранный исполняемый файл --run (путь -o, иначе <build_dir>/<stem>).
int runBuiltExecutable(const PipelineOpts& opts, const std::filesystem::path& cpptPath);

/// --run: если кеш по md5 исходников валиден и exe существует — запустить без перекомпиляции.
/// Возвращает код возврата программы, если кеш применим; иначе nullopt → перекомпиляция.
std::optional<int> tryRunCached(const PipelineOpts& opts);

} // namespace trust
