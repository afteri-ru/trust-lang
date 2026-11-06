// Хранилище пакетов: поиск корня пакета (архив `.trust` или каталог с `trust.toml`) и
// извлечение архива в кэш. Без `Context`: ошибки возвращаются строкой, фатальными их делает
// загрузчик (`reportFatal` — единственная точка).
// Идентичность пакета — логическое имя (`packageNameOf`); пути внутри сохраняют пакетный
// заголовок, один пакет = один `trust.toml`.
#pragma once

#include "location/module_path.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace trust {

class PackageStore {
  public:
    /// Каталог извлечения (задаёт владелец — `Pipeline`); без него импорт пакета — ошибка.
    void setCacheDir(std::filesystem::path dir) { m_cacheDir = std::move(dir); }

    /// Архивы, реально использованные при резолве (канонические пути, порядок использования):
    /// нужны записи кеша `--run` для инвалидации по содержимому пакета.
    [[nodiscard]] const std::vector<std::filesystem::path>& usedArchives() const noexcept { return m_usedArchives; }

    /// Извлечение архива в `<cacheDir>/<stem>-<md5(archive)[0..12]>/`; каталог = кэш
    /// (валидность по маркеру MD5, старые версии того же имени удаляются).
    [[nodiscard]] std::filesystem::path ensureExtracted(const std::filesystem::path& archive, std::string& out_error);

    /// Корень модулей пакета `path.package` по корням поиска: манифест проверяется,
    /// `module-root` обязан быть относительным и без `..`.
    [[nodiscard]] bool resolveRoot(const ModulePath& path, const std::vector<std::filesystem::path>& roots, std::filesystem::path& outRoot,
                                   std::string& outError);

  private:
    std::filesystem::path m_cacheDir;
    std::vector<std::filesystem::path> m_usedArchives;
};

} // namespace trust
