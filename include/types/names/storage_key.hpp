// ЕДИНЫЙ ключ хранения имени: SymbolTable (ключи скоупа), TypeSystem (ключи реестра
// типов), видимость импорта. Ключ СТРУКТУРНЫЙ: видимость и модуль - поля, а не текст.
// namespace-путь и короткое имя - стабильные views пула строк компиляции (см. internStorageKey).
// Текст ключа существует только как ПРОЕКЦИЯ для логов/тестов/диагностики
// (storageKeyText); обратного разбора нет - восстановить ключ из текста невозможно.

#pragma once

#include "location/module_index.hpp"

#include <compare>
#include <cstddef>
#include <string>
#include <string_view>

namespace trust {

struct TypeDescriptor; // types/type_data.hpp

/// Класс видимости объявления в модели модулей (ось видимости, а не текст ключа).
enum class VisibilityClass : unsigned char {
    Exported,     ///< ПЛОСКАЯ привязка (site-binding импорта, локальный скоуп, встроенный домен)
    ModuleExport, ///< идентичность экспортированного объявления в ЕГО модуле
    ImportHidden, ///< экспорт, скрытый на конкретном сайте импорта
    FilePrivate,  ///< `__::`/локальное: видно только своему модулю
};

// ЕДИНСТВЕННЫЕ предикаты класса видимости. `switch` без `default`: новый перечислитель обязан
// ломать сборку (`-Werror`) во всех точках, а не тихо менять поведение (LSP/проекции/вывод ключа).

/// Имя лежит в ЭКСПОРТИРУЕМОМ домене (плоская привязка сайта импорта или встроенный домен).
[[nodiscard]] constexpr bool isExportedVisibility(VisibilityClass visibility) noexcept {
    switch (visibility) {
    case VisibilityClass::Exported:
        return true;
    case VisibilityClass::ModuleExport:
    case VisibilityClass::ImportHidden:
    case VisibilityClass::FilePrivate:
        return false;
    }
    return false; // все перечислители покрыты выше (switch без default)
}

/// Объявление файл-приватно (`__::`): регистрируется под module-qualified ключом и эмитится
/// в C++ anonymous namespace.
[[nodiscard]] constexpr bool isFilePrivateVisibility(VisibilityClass visibility) noexcept {
    switch (visibility) {
    case VisibilityClass::FilePrivate:
        return true;
    case VisibilityClass::Exported:
    case VisibilityClass::ModuleExport:
    case VisibilityClass::ImportHidden:
        return false;
    }
    return false; // все перечислители покрыты выше (switch без default)
}

/// Экспорт модуля, скрытый на КОНКРЕТНОМ сайте импорта (маска/транзитивность).
[[nodiscard]] constexpr bool isSiteHiddenVisibility(VisibilityClass visibility) noexcept {
    switch (visibility) {
    case VisibilityClass::ImportHidden:
        return true;
    case VisibilityClass::Exported:
    case VisibilityClass::ModuleExport:
    case VisibilityClass::FilePrivate:
        return false;
    }
    return false; // все перечислители покрыты выше (switch без default)
}

/// Модульная идентичность имени: экспорт модуля либо его module-local видимость
/// (private/import-hidden).
[[nodiscard]] constexpr bool isModuleOwnedVisibility(VisibilityClass visibility) noexcept {
    switch (visibility) {
    case VisibilityClass::ModuleExport:
    case VisibilityClass::ImportHidden:
    case VisibilityClass::FilePrivate:
        return true;
    case VisibilityClass::Exported:
        return false;
    }
    return false; // все перечислители покрыты выше (switch без default)
}

/// Канонический ключ хранения имени.
/// Порядок (`<=>`) детерминирован по полям: обход скоупа/реестра стабилен между прогонами.
struct StorageKey {
    VisibilityClass visibility = VisibilityClass::Exported;
    ModuleIndex module{};           ///< индекс модуля (валиден при isModuleQualified())
    std::string_view namespacePath; ///< "ns" / "ns::sub" / "" (глобальная область)
    std::string_view name;          ///< короткое имя (без namespace-пути)

    [[nodiscard]] bool isModuleQualified() const noexcept { return isModuleOwnedVisibility(visibility); }

    bool operator==(const StorageKey&) const = default;
    auto operator<=>(const StorageKey&) const = default;
};

struct StorageKeyHash {
    [[nodiscard]] std::size_t operator()(const StorageKey& key) const noexcept;
};

/// Экспортируемое имя в плоском пространстве имён (`namespacePath::name`): ПРИВЯЗКА сайта импорта,
/// локального скоупа и встроенный домен.
[[nodiscard]] StorageKey exportedKey(std::string_view namespacePath, std::string_view name) noexcept;

/// Экспортируемое имя по готовому qualified-имени (`ns::name` либо `name`).
/// `qualifiedName` должен быть СТАБИЛЬНЫМ view (интернированный текст имени/узла).
[[nodiscard]] StorageKey exportedKeyOf(std::string_view qualifiedName) noexcept;

/// Идентичность экспортированного объявления в ЕГО модуле (в отличие от плоского ключа сайта).
[[nodiscard]] StorageKey moduleExportKey(ModuleIndex module, std::string_view namespacePath, std::string_view name) noexcept;

/// Module-local ключ: private (`importHidden=false`) либо скрытый на сайте импорта.
[[nodiscard]] StorageKey moduleKey(ModuleIndex module, std::string_view namespacePath, std::string_view name, bool importHidden) noexcept;

/// Ключ для СОХРАНЕНИЯ в контейнере: части интернируются в активный пул строк, чтобы views
/// ключа пережили аргументы, из которых он построен. Для поиска интернирование не нужно.
[[nodiscard]] StorageKey internStorageKey(StorageKey key);

/// Ключ для ПРОЦЕССНЫХ структур (живут дольше компиляции, напр. builtin-ядро): части
/// интернируются в процессный пул строк.
[[nodiscard]] StorageKey internProcessStorageKey(StorageKey key);

/// Текстовая проекция ключа (логи, тесты, диагностика). Обратного разбора НЕТ.
[[nodiscard]] std::string storageKeyText(const StorageKey& key);

/// Заполнить проекции дескриптора типа из ключа регистрации (`namespacePath`/`filePrivate`).
/// Ключ остаётся в `TypeNameIndex` (единственный источник); в дескрипторе хранятся только
/// вычисленные из него проекции. Вызывается ровно один раз при регистрации.
void applyTypeStorageProjection(TypeDescriptor& desc, const StorageKey& key);

} // namespace trust
