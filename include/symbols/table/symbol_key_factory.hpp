#pragma once

// include/symbols/table/symbol_key_factory.hpp
// Единственная точка ПОСТРОЕНИЯ StorageKey для таблицы символов: модульные/плоские/скрытые
// варианты одного spelling. Само правило видимости остаётся за `types/site_visibility.hpp`
// (фабрика получает готовый SiteVisibilityInput); интернирование ключей — `internStorageKey`.

#include "location/module_index.hpp"
#include "types/names.hpp"

#include <array>
#include <string_view>
#include <vector>

namespace trust {

class StorageKeyFactory {
  public:
    /// Ключи объявления по входу правила видимости: идентичность модуля + привязки сайта.
    /// Результат ИНТЕРНИРОВАН (кладётся в скоуп, обязан пережить аргументы).
    [[nodiscard]] std::vector<StorageKey> siteKeys(const SiteVisibilityInput& input) const;

    /// Объявление ВНЕ тела импортируемого модуля: единственный ключ (private либо идентичность).
    /// `module` — модуль-владелец объявления (может отличаться от текущего).
    [[nodiscard]] StorageKey ownerKey(ModuleIndex module, std::string_view qualifiedName, SiteDeclVisibility visibility) const;

    /// Локальный/блочный скоуп: плоская привязка, видимость не фильтруется.
    [[nodiscard]] StorageKey localKey(std::string_view qualifiedName) const;

    /// Ключи поиска в модульном скоупе @p module: private, идентичность, hidden, плоский.
    [[nodiscard]] std::array<StorageKey, 4> variants(ModuleIndex module, std::string_view name) const;
    /// Ключи «своего» модуля (private, identity, import-hidden) — конфликт внутри модуля.
    [[nodiscard]] std::array<StorageKey, 3> ownKeys(ModuleIndex module, std::string_view name) const;

    [[nodiscard]] StorageKey privateKey(ModuleIndex module, std::string_view name) const;
    [[nodiscard]] StorageKey hiddenKey(ModuleIndex module, std::string_view name) const;
    [[nodiscard]] StorageKey identityKey(ModuleIndex module, std::string_view name) const;
};

} // namespace trust
