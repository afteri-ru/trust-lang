// include/module_loader/path/module_path_policy.hpp
// Чистая политика путей модулей: резолв `\file`/`\\group`/`\\\pkg\...` в список исходников.
// Без `Context`, без реестра модулей и без диагностик: ошибка возвращается строкой, решение
// о её фатальности принимает загрузчик (`reportFatal` — единственная точка).
#pragma once

#include "location/module_path.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace trust::module_path {

namespace fs = std::filesystem;

/// Что допускает вызывающий: форма пути задаётся спросом, а не типом префикса.
enum class PathForm : uint8_t { File, Directory, Either };

/// Резолв цели в список исходников: каталог разворачивается в нерекурсивный список `.src`.
struct TargetFiles {
    std::vector<fs::path> files;
    fs::path scopeRoot;
    std::string error;
};

/// Канонизация пути: ошибка канонизации не делает уже найденный путь невалидным — остаётся он же.
[[nodiscard]] fs::path canonicalPath(const fs::path& path);

/// Канонизирует путь и добавляет в список, если такого ещё нет (единственная канонизация-дедуп).
void appendUniqueCanonical(std::vector<fs::path>& out, const fs::path& path);

/// Файлы группы: только `.src` НЕПОСРЕДСТВЕННО в каталоге; `packageRoot` = первый уровень
/// подкаталогов пакета (скрытые каталоги пропускаются), результат сортируется.
[[nodiscard]] std::vector<fs::path> collectGroupFiles(const fs::path& root, bool packageRoot, std::string& out_error);

/// Резолв цели в список исходников по корням поиска (0 кандидатов / >1 — ошибка).
[[nodiscard]] TargetFiles collectTargetFiles(const ModulePath& path, const std::vector<fs::path>& roots, PathForm form);

/// Группа, к которой принадлежит модуль (для `canAccessProtected`): пакет, первая компонента
/// пути или корневая группа проекта.
[[nodiscard]] std::string moduleGroupKey(const ModulePath& path);

/// Дополнительные корни из переменной окружения (`:`-разделённый список), порядок сохраняется.
void appendEnvList(std::vector<fs::path>& out, const char* env_name);

} // namespace trust::module_path
