// include/pipeline/source_map.hpp
// Сохранение сгенерированного .cppt, копия LICENSE, экспорт-таблица и встраивание
// source-map + записи кеша --run в ELF-секции. Выделено из pipeline.cpp.
#pragma once

#include "pipeline/pipeline.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace trust {

/// Сохранение .cppt + .src_map рядом + #embed source-map.
/// embed_source_map=false — не встраивать `#embed "<name>.src_map"` в C++-файл
/// (сам файл `.src_map` записывается ВСЕГДА). Управляется флагом `-fsourcemap`/`-fno-sourcemap`.
/// program_record — запись кеша --run (первая строка ВСЕГДА версия компилятора); пишется в
/// ELF-секцию `.debug_trust_hash`.
/// trust_options — строка фактически заданных codegen-опций (пишется в шапку `.cppt` как
/// `// trust-options: ...`, если непустая; для трассируемости).
///
/// Локальный кеш: в шапку `.cppt` пишется маркер `// trust-src-hash: <sha256(program_record)>`.
/// Если файл уже существует и его маркер совпадает с текущим — файл (и `.src_map`) НЕ
/// перезаписывается (mtime сохраняется), и `make` сам пропускает пересборку `.o`; через
/// `out_skipped` (если задан) возвращается true. `embed_elf_hash=false` — не создавать
/// ELF-секцию (модульные `.cppt`: запись читается только из исполняемого файла).
bool saveCppAndEmbedSourceMap(Context& ctx, MapperFile cpp_idx, const std::filesystem::path& cppt_path, bool verbose, const std::string& program_record = {},
                              bool embed_source_map = true, const std::string& trust_options = {}, bool embed_elf_hash = true, bool* out_skipped = nullptr);

/// Объявления Trust-интерфейса экспортов (`<decl> := %<native>;`), по одной строке на экспорт.
/// Источник интерфейсного текста модуля.
[[nodiscard]] std::vector<std::string> exportInterfaceDecls(const std::vector<ExportEntry>& exports);

/// Тот же Trust-интерфейс одной строкой (объявления через `\n`) — текст интерфейса модуля.
[[nodiscard]] std::string buildInterfaceText(const std::vector<ExportEntry>& exports);

} // namespace trust
