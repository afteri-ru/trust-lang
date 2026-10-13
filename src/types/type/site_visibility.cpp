// src/types/site_visibility.cpp
#include "types/names/site_visibility.hpp"

namespace trust {

SiteVisibility siteVisibilityOf(const SiteVisibilityInput& input) {
    if (input.declVisibility == SiteDeclVisibility::Private) {
        return SiteVisibility::NotVisible; // виден только своему модулю (module-local ключ)
    }
    if (input.declVisibility == SiteDeclVisibility::Protected && !input.protectedAllowed) {
        return SiteVisibility::NotVisible; // `_::` доступно только файлам той же группы/пакета
    }
    if (input.access == SiteAccess::Direct && input.masks.matches(input.namespacePath, input.name)) {
        return SiteVisibility::VisibleFlat;
    }
    return SiteVisibility::VisibleHidden;
}

std::vector<StorageKey> siteKeysFor(const SiteVisibilityInput& input) {
    if (input.declVisibility == SiteDeclVisibility::Private) {
        return {moduleKey(input.declarationModule, input.namespacePath, input.name, /*importHidden=*/false)};
    }
    std::vector<StorageKey> keys{moduleExportKey(input.declarationModule, input.namespacePath, input.name)};
    switch (siteVisibilityOf(input)) {
    case SiteVisibility::VisibleFlat:
        keys.push_back(exportedKey(input.namespacePath, input.name));
        break;
    case SiteVisibility::VisibleHidden:
        keys.push_back(moduleKey(input.declarationModule, input.namespacePath, input.name, /*importHidden=*/true));
        if (input.access == SiteAccess::Transitive) {
            // Непосредственный импортёр (owner) видит имя, но корневой импортёр — нет.
            const StorageKey ownerKey = moduleKey(input.siteImporter, input.namespacePath, input.name, /*importHidden=*/true);
            if (!(ownerKey == keys.back())) {
                keys.push_back(ownerKey);
            }
        }
        break;
    case SiteVisibility::NotVisible:
        break;
    }
    return keys;
}

} // namespace trust
