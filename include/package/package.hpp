// include/package/package.hpp
// Пакет trust (`.trust`) — архив исходников tar.gz с манифестом `trust.toml`:
// определение/извлечение/проверка целостности, создание и распаковка (режимы CLI --package/--extract),
// упаковка каталога в tar.gz (createTarGz) и доступ к пакетам ресурсов toolchain.
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace trust {

/// Поиск пакета ресурсов инструментария `<share/trust>/<name>.trust`.
/// Корень ресурсов — каталог `share/trust` относительно реального исполняемого файла
/// (env `TRUST_RESOURCE_DIR` перекрывает). Невалидное имя пакета или отсутствующий архив — nullopt.
[[nodiscard]] std::optional<std::filesystem::path> locateResourcePackage(std::string_view name);

/// Корень ресурсов toolchain (`share/trust`, env `TRUST_RESOURCE_DIR` перекрывает).
/// nullopt — каталог не найден.
[[nodiscard]] std::optional<std::filesystem::path> resourceDirectory();

/// Чтение одного файла из пакета `.trust` без распаковки на диск.
/// `relPath` — путь внутри пакета (ведущий `./` допускается). Запись должна существовать
/// и быть безопасной (без `..`/абсолютного пути); иначе false + out_error.
bool readPackageEntry(const std::filesystem::path& archive, std::string_view relPath, std::string& out_content, std::string& out_error);

/// Чтение файла из пакета ресурсов `<share/trust>/<packageName>.trust`.
/// nullopt — пакет не найден или запись отсутствует/недоступна.
[[nodiscard]] std::optional<std::string> readResourcePackageEntry(std::string_view packageName, std::string_view relPath);

/// Создание пакета `.trust` из каталога с исходниками.
/// Каталог обязан содержать `trust.toml` с полем `name`.
/// `version` — semver ПАКЕТА: сохраняется авторское значение, а при отсутствии подставляется
/// версия компилятора (`TRUST_VERSION_SHORT`) как provenance; версия toolchain — `[build].toolchain`.
/// В архив кладётся КОПИЯ манифеста с добавленными таблицами `[build]`
/// (formatVersion/toolchain/created) и `[files]` (MD5 каждого файла — контроль целостности);
/// исходный `trust.toml` НЕ изменяется. Собираются все файлы каталога (включая `*.src`,
/// C++-заголовки пакета и вложенные `*.trust`); исключаются build-артефакты
/// (`.trust/`, `*.cppt`, `*.src_map`, `*.o`, `*.so`, `app`).
/// @param package_dir  каталог-пакет (содержит trust.toml).
/// @param output_file  путь архива; пусто -> `<name>.trust` в текущем каталоге.
/// @return 0 при успехе; 1 при ошибке (диагностика - в stderr).
int createTrustPackage(const std::filesystem::path& package_dir, const std::filesystem::path& output_file);

/// Распаковка пакета `.trust` в каталог назначения.
/// Архив обязан содержать `trust.toml` с обязательными полями `name`/`version`.
/// Записи архива проверяются на выход за пределы каталога назначения (path traversal).
/// После распаковки выполняется сверка целостности по таблице `[files]` (MD5); её отсутствие -
/// ошибка (нет данных о целостности).
/// @param archive   путь к архиву `.trust`.
/// @param dest_dir  каталог назначения (создаётся при отсутствии); пусто -> подкаталог
///                  `<name>/` в текущем каталоге (name из манифеста архива).
/// @return 0 при успехе; 1 при ошибке (диагностика - в stderr).
int extractTrustPackage(const std::filesystem::path& archive, const std::filesystem::path& dest_dir);

/// Пакет ли это: gzip-подпись (1f 8b) в начале файла.
bool isTrustPackage(const std::filesystem::path& path);

/// Извлечение пакета с проверкой целостности (extract + verifyPackage).
/// false + out_error при ошибке (без silent fallback).
bool extractAndVerifyPackage(const std::filesystem::path& archive, const std::filesystem::path& dest, std::string& out_error);

/// Кэш-запись распакованного stdlib.trust: ключ = MD5 архива, корень XDG_CACHE_HOME
/// (fallback ~/.cache), LRU-чистка по last-used (записи старше 30 суток удаляются).
/// Гарантирует наличие валидной записи: при промахе распаковывает и проверяет архив
/// (extractAndVerifyPackage) с атомарным rename. Используется сборкой (копия дерева
/// в build-каталог) и чтением stdlib-модулей prelude (readResourcePackageEntry).
/// nullopt БЕЗ out_error — кэш недоступен (нет XDG/HOME/не создался): вызывающий
/// продолжает прямой распаковкой; nullopt С out_error — ошибка распаковки архива.
[[nodiscard]] std::optional<std::filesystem::path> ensureStdlibCacheEntry(std::string& out_error);

/// Проверка целостности распакованного пакета по таблице `[files]` манифеста `trust.toml`
/// (MD5 каждого файла; лишние файлы - ошибка). Отсутствие `trust.toml` или таблицы `[files]` -
/// ошибка (нет данных о целостности). false + out_error при любом несоответствии.
bool verifyPackage(const std::filesystem::path& dir, std::string& out_error);

/// Упаковка каталога dir в tar.gz-архив (через системный tar, архив должен лежать ВНЕ dir).
/// Исключаются *.o, *.so, app. Возвращает false при ошибке (причина в out_error).
bool createTarGz(const std::filesystem::path& dir, const std::filesystem::path& archive, std::string& out_error);

} // namespace trust
