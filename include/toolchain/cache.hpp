#pragma once

// Валидация записи кеша сборки `--run`: читает ELF-секцию `.debug_trust_hash` и сверяет
// версию компилятора, строку codegen-опций и md5 файлов. Формат записи — `utils/build_record.hpp`
// (единый владелец; пишет кодоген в шапке `.cppt`).

#include <filesystem>
#include <string_view>

namespace trust {
namespace toolchain {

/// Проверить запись из ELF-секции `utils::kRunCacheSection`: версия компилятора, строка опций,
/// замыкание зависимостей и md5 файлов. `expectedMain` — главный файл программы (путь в записи
/// относителен CWD и обязан совпасть: кеш чужой программы не применяется).
/// false — кеш невалиден (пересборка), в т.ч. при отсутствии секции.
[[nodiscard]] bool runCacheValid(const std::filesystem::path& executable, const std::filesystem::path& expectedMain, std::string_view optionsRecord);

} // namespace toolchain
} // namespace trust

