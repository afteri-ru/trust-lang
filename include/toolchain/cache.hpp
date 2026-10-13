#pragma once

// include/toolchain/cache.hpp
// Кеш сборки: нормализованная строка codegen-опций и запись `.debug_trust_hash`
// (формат + валидация). Набор файлов/зависимостей определяет оркестратор — здесь только
// формат и проверка по md5.

#include "driver/options.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace trust {
namespace toolchain {

/// Секция ELF, в которой лежит запись кеша `--run`.
inline constexpr std::string_view kRunCacheSection = ".debug_trust_hash";
/// Префикс первой строки записи (версия компилятора).
inline constexpr std::string_view kRunCacheVersionPrefix = "trust-lang\t";

/// Нормализованная строка фактически заданных codegen-опций (сортировка + санитайз).
/// Единый источник для строки `@opts` записи кеша `--run` и шапки `.cppt` (`trust-options`).
[[nodiscard]] std::string codegenArgsRecord(const PipelineOpts& opts);

/// Элемент ключа кеша: путь ОТНОСИТЕЛЬНО CWD + md5 содержимого.
struct CacheDependency {
    std::string path;
    std::string md5;
};

/// Собрать запись кеша: версия, `@opts`, `@deps` (замыкание) и файлы (первый — главный).
[[nodiscard]] std::string composeRunCacheRecord(std::string_view version, std::string_view optionsRecord, const std::vector<CacheDependency>& dependencies,
                                                const std::vector<CacheDependency>& files);

/// Проверить запись из ELF-секции `kRunCacheSection`: версия компилятора, строка опций,
/// замыкание зависимостей и md5 файлов. `expectedMain` — главный файл программы (путь в записи
/// относителен CWD и обязан совпасть: кеш чужой программы не применяется).
/// false — кеш невалиден (пересборка), в т.ч. при отсутствии секции.
[[nodiscard]] bool runCacheValid(const std::filesystem::path& executable, const std::filesystem::path& expectedMain, std::string_view optionsRecord);

} // namespace toolchain
} // namespace trust
