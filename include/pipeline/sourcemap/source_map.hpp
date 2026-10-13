// include/pipeline/sourcemap/source_map.hpp
// Сохранение сгенерированного .cppt, копия LICENSE и встраивание source-map + записи
// кеша --run в ELF-секции. Выделено из pipeline.cpp.
#pragma once

#include "location/location.hpp"

#include <filesystem>
#include <string>

namespace trust {

class Context;

/// Сохранение .cppt + .src_map рядом + #embed.
/// embed_source_map=false — не встраивать `#embed "<name>.src_map"` в C++-файл
/// (сам файл `.src_map` записывается ВСЕГДА). Управляется флагом `-fsourcemap`/`-fno-sourcemap`.
/// program_record — запись кеша --run (первая строка ВСЕГДА версия компилятора). Эмитится в
/// ELF-секцию `.debug_trust_hash` ТОЛЬКО для главного файла (непустая запись); модульные
/// единицы передают пустую строку.
/// trust_options — строка фактически заданных codegen-опций (пишется в шапку `.cppt` как
/// `// trust-options: ...`, если непустая; для трассируемости, кеш не затрагивает).
bool saveCppAndEmbedSourceMap(Context& ctx, MapperFile cpp_idx, const std::filesystem::path& cppt_path, bool verbose, const std::string& program_record = {},
                              bool embed_source_map = true, const std::string& trust_options = {});

} // namespace trust
