// module_export.hpp - сбор экспорт-интерфейса модуля из его AST-тела.
//
// Экспорт-набор файла: ЕСЛИ в заголовке задан блок интерфейса (`\path { ... };`), экспортируются
// только объявления этого блока; иначе - ВСЕ top-level объявления/определения в не-анонимных
// областях имён (глобальная `::`, именованные `ns::`). Из анонимной области `_`
// и из локальных (внутри функций/блоков) - НЕ экспортируются.
//
// Фильтр интерфейса при импорте: аргументы оператора `\module(mod, masks)` - список
// glob-масок (`*` - любая последовательность, `?` - один символ, через запятую = OR).
// Маска с `::` матчится с полным trust-именем (`ns::f`), без `::` - с коротким (`f`, `*`).
// Пустая строка масок означает «экспортировать всё»; несовпавшая маска - диагностика
// `-Wunmatched-mask` (severity настраивается), пустой результат импорта - `-Wempty-import`.

#pragma once

#include "ast/ast_nodes.hpp"
#include "location/namespace_alias.hpp"
#include "syntax/term/term_types.h"
#include "utils/import_masks.hpp"
#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace trust {

/// Экспортируемое объявление: узел AST и его полное trust-имя (`ns::name`).
struct ExportEntry {
    AstNodePtr node;
    std::string qualifiedName;
    AstNodeBase::Visibility visibility = AstNodeBase::Visibility::Public;
    /// Top-level узел, из которого получена запись (для объявлений блока интерфейса — сам блок):
    /// по нему экспорт-набор делится на «свой» и «реэкспорт» без второго обхода тела.
    const AstNodeBase* container = nullptr;
};

/// `TypeDecl` (`::=`/`=`) с RHS-RecordDecl: forward (`m_body == nullopt`, `struct c_T;`) либо
/// определение. Форма нужна и сканеру тела, и сборке фактов, и разметке реализаций экспорта.
[[nodiscard]] bool isRecordTypeDecl(const AstNodePtr& node, bool wantBody);

/// Все объявления уровня модуля с qualified-именами (без фильтра по маскам) — см.
/// `ModuleBodyScan::entries` (единственный обход тела). Экспорт-набор файла —
/// `ModuleInterfaceFacts::entries`. `roots` - тело модуля либо корни AST главного юнита: корневой
/// `ModuleNode` разворачивается (модуль - не namespace-сегмент).

/// Результат ОДНОГО обхода тела модуля: записи экспорт-набора с происхождением
/// (`ExportEntry::container`), экспортируемые namespace-алиасы, блок интерфейса и признак
/// out-of-line определений.
struct ModuleBodyScan {
    std::vector<ExportEntry> entries;
    std::vector<ModuleNamespaceAlias> namespaceAliases; ///< только из блока интерфейса (экспортируемые)
    /// Блок интерфейса файла (`\path { ... };`), если он есть в корне тела.
    const AstNodeBase* interfaceBlock = nullptr;
    /// Нужен ли модулю собственный `.cppt`: тела top-level функций (в т.ч. в областях имён),
    /// глобальные переменные с инициализатором и определение record, объявленного forward'ом в
    /// интерфейсе файла (сайт импорта видит только forward — определение живёт в TU владельца).
    /// Прочие определения типов и inline-методы эмитятся на сайте импорта, поэтому
    /// declaration-only модуль (объявления и native-импорты без тел) собственного `.cppt` не
    /// требует. Правило вычисляется ТЕМ ЖЕ обходом тела (`scanModuleBody`).
    bool hasOutOfLineDefinitions = false;
};

/// ЕДИНСТВЕННЫЙ обход тела модуля: экспорт-записи, namespace-алиасы, происхождение записей и
/// признаки, производные от того же дерева.
[[nodiscard]] ModuleBodyScan scanModuleBody(const std::vector<AstNodePtr>& roots);

/// Идентичность определения функции: qualified-имя + C++-суффикс (различает перегрузки).
struct ExportedFunctionKey {
    std::string qualifiedName;
    std::string cppSuffix;

    bool operator==(const ExportedFunctionKey&) const = default;
};

struct ExportedFunctionKeyHash {
    [[nodiscard]] std::size_t operator()(const ExportedFunctionKey& key) const noexcept {
        const std::size_t nameHash = std::hash<std::string_view>{}(key.qualifiedName);
        return nameHash ^ (std::hash<std::string_view>{}(key.cppSuffix) * 0x9e3779b9u);
    }
};

/// Полные факты интерфейса модуля, вычисляемые ОДИН раз и переиспользуемые сайтами импорта,
/// заголовком и графом зависимостей.
struct ModuleInterfaceFacts {
    std::vector<ExportEntry> entries;                   ///< Публичный экспорт-набор (для интерфейса — блок + реэкспорт).
    std::vector<ExportEntry> protectedEntries;          ///< `_::`: видимо файлам той же группы/пакета.
    std::vector<ModuleNamespaceAlias> namespaceAliases; ///< Экспортированные привязки префиксов.
    /// qualified-имя → определения функций. Ключ БЕЗ C++-суффикса: суффикс назначается семантикой
    /// позже, поэтому по нему нельзя индексировать на этапе загрузки; сравнение — при запросе.
    std::unordered_map<std::string, std::vector<AstNodePtr>> functionDefinitions;
    bool hasOutOfLineDefinitions = false; ///< Нужен ли модулю собственный `.cppt`.

    /// Определение функции для экспортной записи: qualified-имя + C++-суффикс перегрузки
    /// (суффикс назначается семантикой, поэтому поиск идёт при запросе). `nullptr` — определения нет.
    [[nodiscard]] AstNodePtr findFunctionDefinition(std::string_view qualifiedName, std::string_view cppSuffix) const;
};

/// Собрать факты интерфейса из тела модуля. `scan` — результат ОДНОГО обхода тела
/// (`scanModuleBody`); `interfaceExports` — объявления блока интерфейса (пусто, если блока нет);
/// `reExported` — отфильтрованный реэкспорт этого файла.
[[nodiscard]] ModuleInterfaceFacts collectModuleInterface(const ModuleBodyScan& scan, const std::vector<ExportEntry>& interfaceExports = {},
                                                          const std::vector<ExportEntry>& reExported = {});

/// Удобная форма для вызывающего, которому нужен только экспорт-набор: тело обходится ОДИН раз
/// внутри (не используйте в паре с отдельным `scanModuleBody` — это второй обход).
[[nodiscard]] ModuleInterfaceFacts collectModuleInterface(const std::vector<AstNodePtr>& roots, const std::vector<ExportEntry>& interfaceExports = {},
                                                          const std::vector<ExportEntry>& reExported = {});

/// Записи экспорт-набора, выбранные масками сайта (единственное правило матчинга —
/// `utils::ImportMasks`). Общий шаг проекции сайта (узлы) и реэкспорта (записи интерфейса).
/// Указатели валидны, пока жив `entries`.
[[nodiscard]] std::vector<const ExportEntry*> selectExportEntries(const std::vector<ExportEntry>& entries, const utils::ImportMasks& masks);

/// Проекция полного списка экспортов по маскам сайта импорта (единственное правило матчинга —
/// `utils::ImportMasks`). Вызывается на каждый сайт, но обход тела и qualified-имена уже
/// посчитаны один раз (`ModuleInterfaceFacts::entries`). `namespaceAliases` участвуют в проверке
/// несовпавших масок: имя namespace-алиаса — тоже публичная привязка файла (маска может
/// выбирать только её, не выбирая ни одного объявления).
[[nodiscard]] std::vector<AstNodePtr> filterExportEntries(const std::vector<ExportEntry>& entries, const std::vector<ModuleNamespaceAlias>& namespaceAliases,
                                                          const utils::ImportMasks& masks, std::vector<std::string>* unmatchedMasks = nullptr);

} // namespace trust