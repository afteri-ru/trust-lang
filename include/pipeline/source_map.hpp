// include/pipeline/source_map.hpp
// Сохранение сгенерированного .cppt, копия LICENSE, экспорт-таблица и встраивание
// source-map + записи кеша --run в ELF-секции. Выделено из pipeline.cpp.
#pragma once

#include "pipeline/pipeline.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace trust {

/// Сохранение .cppt + .src_map рядом + #embed + export table.
/// embed_export_table=false — не встраивать экспорт-таблицу (модуль-исходник,
/// линкуемый в программу: таблица принадлежит главному файлу).
/// program_record — запись кеша --run (первая строка ВСЕГДА версия компилятора).
bool saveCppAndEmbedSourceMap(Context& ctx, MapperFile cpp_idx, const std::filesystem::path& cppt_path, bool verbose,
                              const std::vector<ExportEntry>& exports = {}, bool embed_export_table = true, const std::string& program_record = {});

} // namespace trust
