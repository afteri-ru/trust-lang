// ЕДИНСТВЕННОЕ правило видимости объявления на сайте импорта: какое имя видно с сайта и
// под каким ключом хранения оно регистрируется/привязывается.
// Заголовок лежит НИЖЕ потребителей (types): `analysis::SymbolTable` (регистрация, привязка,
// резолв) и `module_loader` (проекция экспорт-набора сайта). Разбор/матчинг строки масок
// остаётся за `utils::ImportMasks`; правило принимает УЖЕ разобранные маски.
// Ось видимости объявления в его модуле (Public/Protected/Private) — свойство ИСХОДНИКА и не
// совпадает с `VisibilityClass` (ось ключа хранения): Private даёт FilePrivate-ключ,
// Protected влияет только на доступ ЧУЖОГО модуля.
#pragma once

#include "location/module_index.hpp"
#include "types/names/storage_key.hpp"
#include "utils/import_masks.hpp"

#include <string_view>
#include <vector>

namespace trust {

/// Видимость объявления в ЕГО модуле (текстовая ось, не ось ключа).
enum class SiteDeclVisibility { Public, Protected, Private };

/// Режим доступа сайта: `Direct` — внешний сайт (тело модуля анализируется впервые по нему),
/// `Transitive` — вложенный импорт внутри тела уже импортируемого модуля.
enum class SiteAccess { Direct, Transitive };

struct SiteVisibilityInput {
    ModuleIndex siteImporter;              ///< модуль, в чьём скоупе действует сайт
    ModuleIndex declarationModule;         ///< модуль-владелец объявления
    SiteDeclVisibility declVisibility = SiteDeclVisibility::Public;
    std::string_view namespacePath;        ///< путь объявления внутри его модуля (`""`/`ns`)
    std::string_view name;                 ///< короткое имя
    const utils::ImportMasks& masks;       ///< маски сайта (единственный матчер — ImportMasks)
    SiteAccess access = SiteAccess::Direct;
    bool protectedAllowed = true;          ///< canAccessProtected(siteImporter, declarationModule)
};

enum class SiteVisibility {
    NotVisible,    ///< имя не видно с сайта (private/`__::`, protected без доступа группы)
    VisibleFlat,   ///< видно плоским именем (плоская привязка сайта)
    VisibleHidden, ///< объявление зарегистрировано, но скрыто от импортёра (маска/транзитивность)
};

/// Видимость объявления с сайта. `Protected` без доступа группы — `NotVisible`: иначе
/// защищённое имя утекало бы через сайт, который первым запустил анализ тела модуля.
[[nodiscard]] SiteVisibility siteVisibilityOf(const SiteVisibilityInput& input);

/// Ключи хранения объявления, зарегистрированного/привязанного с сайта: идентичность
/// объявления в его модуле (`ModuleExport`) плюс привязка сайта (плоская либо скрытая).
/// Private (`__::`) — единственный module-local ключ, без привязок.
[[nodiscard]] std::vector<StorageKey> siteKeysFor(const SiteVisibilityInput& input);

} // namespace trust
