// Драйвер сборки/запуска: оркестрация поверх артефактов `Pipeline`.
//
// Компонент НИЖЕ `pipeline` и `toolchain`: конвейер компиляции (`pipeline`) производит артефакты
// (`.cppt`/`.hpp`), этот слой вызывает `toolchain` (build/compile/link) и запускает результат.
// Тем самым «фронтенд → бэкенд» не зависит от хостового компилятора (E20), а `pipeline` не
// зависит от `toolchain` (R34).
#pragma once

#include "driver/options.hpp"
#include "location/location.hpp"

#include <filesystem>
#include <string>

namespace trust {

class Pipeline;

/// Компиляция/сборка главного файла (--run, -c/-a/--shared-lib, --emit-build-dir).
/// CLI-диспетчеризация и выбор режима — вне (см. `driver/commands`, `src/trust.cpp`).
int compileProgram(Pipeline& pipeline, const PipelineOpts& opts, MapperFile inputFile);

/// Транспилирует trust_code и собирает tar.gz-архив build-каталога по пути
/// <emit_dir>/trust-lang-<версия>-generated.tar.gz (без компиляции). Временные файлы
/// удаляются (RAII). Возвращает путь к архиву; пусто при ошибке (причина в out_error).
std::filesystem::path emitBuildDirArchive(const std::string& trust_code, const std::filesystem::path& emit_dir, std::string& out_error);

} // namespace trust
