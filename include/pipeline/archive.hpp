// include/pipeline/archive.hpp
// Сборка tar.gz-архива build-каталога (--emit-build-dir для trust-lsp).
// Выделено из pipeline.cpp (модуль ArchiveBuilder).
#pragma once

#include "pipeline/pipeline.hpp"

#include <filesystem>
#include <string>

namespace trust {

/// Транспилирует trust_code и собирает tar.gz-архив build-каталога по пути
/// <emit_dir>/trust-lang-<версия>-generated.tar.gz (без компиляции). Временные
/// файлы удаляются (RAII). Возвращает путь к архиву; пусто при ошибке (причина в out_error).
std::filesystem::path emitBuildDirArchive(const std::string& trust_code, const std::filesystem::path& emit_dir, std::string& out_error);

} // namespace trust
