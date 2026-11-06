// Расчёт путей build-каталога и генерируемого .cppt. Без зависимости от оркестратора.
#pragma once

#include "driver/options.hpp"

#include <cstddef>
#include <filesystem>
#include <string_view>

namespace trust {

/// build_dir = temp_dir (если задан) или <каталог исходника>/.trust/<stem>
/// (для --run — <cwd>/.trust/<stem>).
std::filesystem::path computeBuildDir(const PipelineOpts& opts);

/// cppt_path = build_dir / <input_stem>.cppt
std::filesystem::path computeCpptPath(const PipelineOpts& opts);

/// Путь модульного `.cppt`: `<stem>-<md5(канонический путь)[0..12]>.cppt`. Хеш уникален для
/// пути модуля и детерминирован, поэтому модули с одинаковым именем файла (`\a\util` и
/// `\b\util`) не перезаписывают артефакты, а имена не зависят от порядка загрузки.
std::filesystem::path moduleCppPath(const std::filesystem::path& buildDir, std::string_view modulePath);

/// Путь заголовка модуля: `build_dir / <enc>.hppt`, где `<enc>` — `moduleEncodedName(modulePath)`
/// (база имени заголовка, не namespace символов): одна формула с именем entry. Уникальность
/// `<enc>` гарантирует загрузчик (коллизия кодированных имён — ошибка компиляции).
/// Требует импортируемый модуль (логический путь с префиксом `\` или `\\`/`\\\`).
std::filesystem::path moduleHeaderPath(const std::filesystem::path& buildDir, std::string_view modulePath);

} // namespace trust
