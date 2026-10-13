#pragma once

// include/symbols/symbol_index.hpp
// Результат сбора объявлений семантики для LSP-сервера: имя, разрешённый тип (TypeId),
// имя типа, диапазоны имени и скоупа объявления. Собирается SymbolCollectorHook
// (см. symbol_collector.hpp) в обходе NameResolutionPass.
//
// SymbolIndex — ПРОЕКЦИЯ объявлений для LSP. Владелец объявлений на время анализа —
// SymbolTable; факты для бэкендов материализуются в полях узлов AST.

#include "location/location.hpp"
#include "location/module_index.hpp"
#include "types/names.hpp"
#include "types/type_id.hpp"
#include "utils/import_masks.hpp"
#include "utils/strings.hpp"

#include <optional>
#include <string>
#include <vector>

namespace trust {

/// Одна запись символа, собранная анализатором.
struct SymbolInfo {
    std::string_view name; ///< Каноническое имя (как в таблице символов; может содержать $/%)
    /// Namespace-путь объявления (`ns`/`ns::sub`; пусто - глобальная область). Нужен для
    /// завершения квалифицированных имён (`ns::<TAB>`); view в пул строк компиляции.
    std::string_view namespacePath;
    TypeId type;            ///< Разрешённый тип (валиден: реестр живёт в CachedSource)
    std::string typeName;   ///< Каноническое имя типа (TypeSystem::getTypeName)
    MapperRange nameRange;  ///< Диапазон ИМЕНИ в исходнике (go-to-definition / фильтр по позиции)
    MapperRange scopeRange; ///< Диапазон скоупа объявления (видимость локальных по позиции курсора)
    bool isMacro = false;   ///< true - имя макроса (добавляется отдельно, не из семантики)
    /// Имена полей словаря/кортежа из инициализатора-литерала `x := (a=1, b=2,)`.
    /// Для member-завершения `x.` (тип литерала - универсальный Dict, поля в нём не хранятся).
    std::vector<std::string> dictFields;
    /// Документирующий комментарий (`///`, `/** */`, `##`) перед объявлением (для hover/док).
    std::string_view documentation;
    /// Модуль объявления (`nullopt` — вне модуля: прелюдия/stdlib DSL). Видимость в LSP
    /// оценивается per-site для объявлений и per-module для макросов (см. `isSymbolVisible`/
    /// `isMacroVisible`); один и тот же optional обслуживает оба правила — отдельного признака
    /// «глобальный макрос» не нужно.
    std::optional<ModuleIndex> module;
    /// Класс видимости ОБЪЯВЛЕНИЯ в ЕГО модуле (site-зависимый import-hidden сюда не входит).
    /// Для макросов не используется: макросы не экспортируются (правило `isMacroVisible`).
    VisibilityClass visibility = VisibilityClass::Exported;
};

/// Сайт импорта — ОДНА структура для стека разбора тела, резолва и LSP-проекции:
/// `importer` — модуль-импортёр (в чьём скоупе действует сайт), `imported` — импортированный
/// модуль, `masks` — разобранные маски (`utils::ImportMasks` — единственный парсер).
struct ImportSite {
    ModuleIndex importer{};
    ModuleIndex imported{};
    utils::ImportMasks masks;
    /// Скоуп действия оператора импорта (невалидный — корень модуля/файла). Нужен, чтобы не
    /// показывать имена, импортированные внутри чужого/уже закрытого блока.
    MapperRange scopeRange;
    /// Сайт видит protected-имена (`_::`) модуля: файлы той же группы/пакета. Решение —
    /// правило `types/site_visibility.hpp`; LSP-проекция (`visibleSite`) protected-ось не
    /// моделирует и это поле не читает.
    bool protectedAllowed = false;
};

/// Имя, РЕЭКСПОРТИРУЕМОЕ фасадом от владельца (`\lib(a, b);` в блоке интерфейса файла).
/// Идентичность имени остаётся за владельцем, поэтому `owner` — модуль объявления и может
/// не совпадать с модулем сайта импорта (фасадом).
struct ReExportedNameInfo {
    ModuleIndex facade{};
    ModuleIndex owner{};
    std::string namespacePath;
    std::string name;
};

/// Область поиска `... = X` (using), зарегистрированная в скоупе `scopeRange`
/// (невалидный диапазон = уровень файла). `targetModule` задан для `... = \lib::ns`.
struct UsingScopeInfo {
    ModuleIndex importer{};
    std::optional<ModuleIndex> targetModule;
    std::string path;
    MapperRange scopeRange;
};

/// Namespace-алиас (`ns:: :- \lib::ns::;`): привязка префикса `alias` к области
/// (`targetModule`, `path`), действующая в скоупе `scopeRange`.
struct NamespaceAliasInfo {
    ModuleIndex importer{};
    std::string alias;
    ModuleIndex targetModule{};
    std::string path;
    MapperRange scopeRange;
};

/// Видимость МАКРОСА в файле главного модуля: макросы не экспортируются, маски к ним не
/// применяются. Определение вне модуля (прелюдия/stdlib DSL) видно везде, определение в модуле —
/// только этому модулю (макросы импортируемого модуля не наследуются).
[[nodiscard]] inline bool isMacroVisible(const SymbolInfo& info, ModuleIndex mainModule) {
    return !info.module.has_value() || *info.module == mainModule;
}

/// Контекст видимости объявления в файле: главный модуль + прямые сайты импорта + реэкспорт.
struct VisibilitySites {
    ModuleIndex mainModule{};
    const std::vector<ImportSite>* sites = nullptr;
    const std::vector<ReExportedNameInfo>* reExports = nullptr;
};

/// Сайт, через который имя видимо в файле (`nullopt` — не видимо). `onlyModule` ограничивает
/// поиск одним модулем. Свой модуль видит всё, включая private;
/// чужой — только экспорт, попавший в маску прямого сайта, либо реэкспорт фасада.
[[nodiscard]] inline std::optional<ModuleIndex> visibleSite(const SymbolInfo& info, const VisibilitySites& visibility,
                                                            std::optional<ModuleIndex> onlyModule = std::nullopt) {
    if (info.module.has_value() && *info.module == visibility.mainModule) {
        if (onlyModule.has_value() && *onlyModule != visibility.mainModule) {
            return std::nullopt;
        }
        return visibility.mainModule;
    }
    if (!isExportedVisibility(info.visibility) || !info.module.has_value()) {
        return std::nullopt;
    }
    const ModuleIndex owner = *info.module;
    for (const ImportSite& site : *visibility.sites) {
        if (onlyModule.has_value() && site.imported != *onlyModule) {
            continue;
        }
        if (!site.masks.matches(info.namespacePath, info.name)) {
            continue;
        }
        if (site.imported == owner) {
            return site.imported;
        }
        for (const ReExportedNameInfo& re : *visibility.reExports) {
            if (re.facade == site.imported && re.owner == owner && re.namespacePath == info.namespacePath && re.name == info.name) {
                return re.facade;
            }
        }
    }
    return std::nullopt;
}

/// Видимость объявления в файле главного модуля (аналог clangd `isVisible`).
[[nodiscard]] inline bool isSymbolVisible(const SymbolInfo& info, const VisibilitySites& visibility) {
    return visibleSite(info, visibility).has_value();
}

using SymbolIndex = std::vector<SymbolInfo>;

} // namespace trust
