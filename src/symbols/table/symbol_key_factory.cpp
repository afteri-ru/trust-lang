#include "symbols/table/symbol_key_factory.hpp"

#include "utils/strings.hpp"

namespace trust {

std::vector<StorageKey> StorageKeyFactory::siteKeys(const SiteVisibilityInput& input) const {
    std::vector<StorageKey> keys;
    for (const StorageKey& key : siteKeysFor(input)) {
        keys.push_back(internStorageKey(key));
    }
    return keys;
}

StorageKey StorageKeyFactory::ownerKey(ModuleIndex module, std::string_view qualifiedName, SiteDeclVisibility visibility) const {
    const std::string_view nsPath = utils::namespacePathOf(qualifiedName);
    const std::string_view name = utils::shortNameOf(qualifiedName);
    if (visibility == SiteDeclVisibility::Private) {
        return internStorageKey(moduleKey(module, nsPath, name, /*importHidden=*/false));
    }
    return internStorageKey(moduleExportKey(module, nsPath, name));
}

StorageKey StorageKeyFactory::localKey(std::string_view qualifiedName) const { return internStorageKey(exportedKeyOf(qualifiedName)); }

std::array<StorageKey, 4> StorageKeyFactory::variants(ModuleIndex module, std::string_view name) const {
    return {privateKey(module, name), identityKey(module, name), hiddenKey(module, name), internStorageKey(exportedKeyOf(name))};
}

std::array<StorageKey, 3> StorageKeyFactory::ownKeys(ModuleIndex module, std::string_view name) const {
    return {privateKey(module, name), identityKey(module, name), hiddenKey(module, name)};
}

StorageKey StorageKeyFactory::privateKey(ModuleIndex module, std::string_view name) const {
    return internStorageKey(moduleKey(module, utils::namespacePathOf(name), utils::shortNameOf(name), /*importHidden=*/false));
}

StorageKey StorageKeyFactory::hiddenKey(ModuleIndex module, std::string_view name) const {
    return internStorageKey(moduleKey(module, utils::namespacePathOf(name), utils::shortNameOf(name), /*importHidden=*/true));
}

StorageKey StorageKeyFactory::identityKey(ModuleIndex module, std::string_view name) const {
    return internStorageKey(moduleExportKey(module, utils::namespacePathOf(name), utils::shortNameOf(name)));
}

} // namespace trust
