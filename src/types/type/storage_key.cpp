// src/types/storage_key.cpp
// Реализация структурного ключа имени (см. types/storage_key.hpp).
#include "types/names/storage_key.hpp"

#include "location/module_index.hpp"
#include "types/type_data.hpp"
#include "utils/error.hpp"
#include "utils/string_interner.hpp"
#include "utils/strings.hpp"

#include <charconv>
#include <functional>

namespace trust {

std::size_t StorageKeyHash::operator()(const StorageKey& key) const noexcept {
    std::size_t h = std::hash<unsigned char>{}(static_cast<unsigned char>(key.visibility));
    const auto mix = [&h](std::size_t value) noexcept { h ^= value + 0x9e3779b9u + (h << 6) + (h >> 2); };
    mix(std::hash<std::size_t>{}(key.module.value));
    mix(std::hash<std::string_view>{}(key.namespacePath));
    mix(std::hash<std::string_view>{}(key.name));
    return h;
}

StorageKey exportedKey(std::string_view namespacePath, std::string_view name) noexcept {
    return StorageKey{VisibilityClass::Exported, ModuleIndex{}, namespacePath, name};
}

StorageKey exportedKeyOf(std::string_view qualifiedName) noexcept {
    return exportedKey(utils::namespacePathOf(qualifiedName), utils::shortNameOf(qualifiedName));
}

StorageKey moduleExportKey(ModuleIndex module, std::string_view namespacePath, std::string_view name) noexcept {
    return StorageKey{VisibilityClass::ModuleExport, module, namespacePath, name};
}

StorageKey moduleKey(ModuleIndex module, std::string_view namespacePath, std::string_view name, bool importHidden) noexcept {
    return StorageKey{importHidden ? VisibilityClass::ImportHidden : VisibilityClass::FilePrivate, module, namespacePath, name};
}

StorageKey internStorageKey(StorageKey key) {
    if (!key.namespacePath.empty()) {
        key.namespacePath = internText(key.namespacePath);
    }
    if (!key.name.empty()) {
        key.name = internText(key.name);
    }
    return key;
}

StorageKey internProcessStorageKey(StorageKey key) {
    SharedStringInterner& pool = manualStringInterner();
    if (!key.namespacePath.empty()) {
        key.namespacePath = pool.intern(key.namespacePath);
    }
    if (!key.name.empty()) {
        key.name = pool.intern(key.name);
    }
    return key;
}

std::string storageKeyText(const StorageKey& key) {
    std::string qualified = utils::qualifyTrustName(key.namespacePath, key.name);
    switch (key.visibility) {
    case VisibilityClass::Exported:
        return qualified;
    case VisibilityClass::ModuleExport:
    case VisibilityClass::ImportHidden:
    case VisibilityClass::FilePrivate:
        break;
    }
    // Формат проекции: \x1f<module>\x1f[~]<qualified>. Используется только для вывода.
    constexpr std::string_view sep = "\x1f";
    std::string text;
    text.reserve(qualified.size() + 28);
    text.append(sep);
    char digits[24];
    const auto res = std::to_chars(digits, digits + sizeof(digits), key.module.value);
    EXPECT(res.ec == std::errc{} && "storageKeyText: module index does not fit the buffer");
    text.append(digits, res.ptr);
    text.append(sep);
    if (isSiteHiddenVisibility(key.visibility)) {
        text.push_back('~');
    }
    text.append(qualified);
    return text;
}

void applyTypeStorageProjection(TypeDescriptor& desc, const StorageKey& key) {
    desc.namespacePath = std::string(key.namespacePath);
    // filePrivate (anonymous namespace) - только НАСТОЯЩИЙ private; import-hidden
    // (экспорт, скрытый сайтом импорта) остаётся экспортированным типом со своим namespace.
    desc.filePrivate = isFilePrivateVisibility(key.visibility);
    // Модуль объявления: несут только module-owned ключи (экспорт/приват/скрытый);
    // плоский ключ (site-binding/встроенный домен) модуля не несёт.
    desc.module = isModuleOwnedVisibility(key.visibility) ? key.module : ModuleIndex::invalid();
}

} // namespace trust
