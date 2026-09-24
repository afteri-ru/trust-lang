// module_sabi.hpp - семантический ABI модуля: ELF-секция .trust.sabi.
//
// Секция .trust.sabi = версионируемый заголовок + массив Trust-деклараций экспортов
// (по одной на экспорт; разделитель '\0', опционально сжатие zstd по флагу). Декларация
// самодостаточна: имя + сигнатура + привязка к символу. Для двоичного модуля (C++ ABI)
// правая часть - `:= %<symbol>;`; для текстового - `:= ...;`. Вид сущности выводится из
// текста декларации (отдельного тега нет). Читается без dlopen через utils::readElfSection.
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace trust {

/// Разобранное содержимое секции .trust.sabi.
struct SabiData {
    std::string compilerVersion;    ///< версия компилятора, записавшего секцию
    std::string srcRecord;          ///< запись источника (version + md5 по файлам) из __trust_exports.srcHash
    std::vector<std::string> decls; ///< Trust-декларации экспортов (по одной на экспорт)
};

/// Сериализовать секцию .trust.sabi: заголовок + метаданные (compilerVersion, srcRecord) +
/// блок NUL-разделённых деклараций. compress=true — блок деклараций сжат zstd (флаг в заголовке).
[[nodiscard]] std::vector<unsigned char> buildSabiSection(const std::vector<std::string>& decls, std::string_view compilerVersion, std::string_view srcRecord,
                                                          bool compress);

/// Разобрать байты секции .trust.sabi. При ошибке возвращает nullopt и заполняет error
/// (явная диагностика: магия/версия формата/размеры). Проверка совместимости версии
/// компилятора здесь НЕ выполняется (см. readModuleSabi).
[[nodiscard]] std::optional<SabiData> parseSabiSection(std::span<const unsigned char> bytes, std::string& error);

/// Прочитать и разобрать секцию .trust.sabi из файла (.so/.trust) без dlopen.
/// Явные ошибки: файл/секция отсутствуют, битая/несовместимая секция (версия компилятора).
[[nodiscard]] std::optional<SabiData> readModuleSabi(const std::string& path, std::string& error);

/// Имя сущности из текста Trust-декларации: до '(' / ':' / ' ' / '=' (для оператора - целиком
/// `` `sym` ``). Используется для фильтрации масками.
[[nodiscard]] std::string_view declName(std::string_view declText);

/// Отфильтровать декларации по comma-маскам имён (module_loader::matchesAnyMask).
/// Пустая/пробельная строка масок = без фильтра (все). Пустой результат - не ошибка.
[[nodiscard]] std::vector<std::string> filterModuleDecls(const std::vector<std::string>& decls, std::string_view commaMasks);

} // namespace trust
