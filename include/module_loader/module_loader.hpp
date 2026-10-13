// module_loader.hpp - единый загрузчик модулей
#pragma once

#include "module_loader/registry/module_registry.hpp"
#include "module_loader/package/package_store.hpp"
#include "location/location.hpp"
#include "location/module_path.hpp"
#include "session/module_resolver.hpp"
#include "syntax/module_header.hpp"
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace trust {

class Context;

class ModuleLoader : public ModuleResolver {
  public:
    explicit ModuleLoader(Context& ctx);
    ModuleLoader(const ModuleLoader&) = delete;
    ModuleLoader& operator=(const ModuleLoader&) = delete;
    ModuleLoader(ModuleLoader&&) noexcept = delete;
    ModuleLoader& operator=(ModuleLoader&&) noexcept = delete;

    // -- Метод A: загрузка модуля из уже зарегистрированного source-файла --
    // Регистрирует модуль, рекурсивно вызывает Parser (expand_module=true) и СРАЗУ конвертирует
    // Term→AST (Term не удерживается; одна AST на модуль). src - реальный source-файл
    // (главный файл или загруженный import); парсинг выполняется из него,
    // чтобы маппинги привязывались к реальному файлу.
    // Возвращает индекс загруженного модуля (кешируется: повторный вызов вернёт тот же индекс).
    // При циклической зависимости - diag().report(Severity::Fatal, range, ...).
    [[nodiscard]] ModuleIndex parseSourceModule(std::string_view moduleId, MapperFile src, MapperRange range = {}, std::filesystem::path scopeRoot = {},
                                                std::string packageName = {}, bool allowNoHeader = true);

    // -- ModuleResolver: узкий интерфейс для парсера (загрузка + проверка загруженности) --
    [[nodiscard]] std::vector<ModuleIndex> resolveModules(std::string_view moduleId, MapperRange range) override;
    [[nodiscard]] std::optional<ModuleIndex> findLoaded(std::string_view moduleId) const override;
    [[nodiscard]] bool isModuleLoaded(std::string_view moduleId) const override;
    [[nodiscard]] std::string_view moduleLogicalName(ModuleIndex index) const override;
    bool registerModuleAlias(std::string_view alias, ModuleIndex target, ModuleIndex owner) override;
    [[nodiscard]] bool hasModuleAlias(ModuleIndex owner, std::string_view alias, ModuleIndex target) const override;
    [[nodiscard]] bool canAccessProtected(ModuleIndex importer, ModuleIndex owner) const override;

    // -- Проверка загруженности модуля --
    [[nodiscard]] bool isLoaded(ModuleIndex index) const;

    // -- Доступ к данным загруженного модуля --
    /// Канонический путь (идентификатор) модуля по индексу (view в хранилище реестра).
    [[nodiscard]] std::string_view modulePath(ModuleIndex index) const;

    /// Количество зарегистрированных модулей (включая главный).
    [[nodiscard]] std::size_t moduleCount() const noexcept { return registry().count(); }

    // -- Факты интерфейса модуля, считаются один раз на модуль --
    void setInterfaceFacts(ModuleIndex index, ModuleInterfaceFacts facts);
    /// Факты интерфейса, посчитанные загрузчиком ОДИН раз. Наличие гарантируется загрузкой
    /// (при ранней ошибке интерфейса факты всё равно фиксируются); иначе — EXPECT.
    [[nodiscard]] const ModuleInterfaceFacts& interfaceFacts(ModuleIndex index);
    /// Экспорт-набор модуля (проекция `interfaceFacts().entries`).
    [[nodiscard]] const std::vector<ExportEntry>& exportEntries(ModuleIndex index);

    /// Проекция сайта на модуль: экспорт после фильтра масками + алиасы + несовпавшие маски.
    /// Кэш на `(модуль, нормализованные маски)`: маски применяются ОДИН раз, объект РАЗДЕЛЯЕТСЯ
    /// сайтами с теми же масками (`shared_ptr` переживает `Pipeline`).
    [[nodiscard]] std::shared_ptr<const SiteProjection> siteProjection(ModuleIndex index, const utils::ImportMasks& masks);

    /// Module-алиасы, объявленные @p owner (spelling → индекс), детерминированный снимок для LSP.
    [[nodiscard]] std::vector<std::pair<std::string, ModuleIndex>> moduleAliases(ModuleIndex owner) const;

    // -- Сконвертированное тело модуля (AST), хранимое один раз и разделяемое сайтами --
    [[nodiscard]] const std::vector<AstNodePtr>& bodyAst(ModuleIndex index) const;
    /// Разделяемое тело: тот же вектор, что у реестра (копий списка нет; lifetime — компиляция).
    [[nodiscard]] std::shared_ptr<std::vector<AstNodePtr>> bodyAstShared(ModuleIndex index) const;

    // -- Текущий индекс модуля (верх стека) --
    [[nodiscard]] ModuleIndex currentModuleIndex() const override;

    // -- Каталог извлечения пакетов (.trust) - задаёт владелец (Pipeline): build/temp-каталог.
    // Должен быть задан до первого импорта из пакета; иначе импорт пакета - ошибка.
    void setPackageCacheDir(std::filesystem::path dir) { m_packages.setCacheDir(std::move(dir)); }

    /// Дополнительные корни поиска модулей (`-I`), в порядке приоритета.
    void setModuleSearchPaths(std::vector<std::filesystem::path> dirs) { m_modulePaths = std::move(dirs); }
    /// Дополнительные корни поиска пакетов (`--package-path`), в порядке приоритета.
    void setPackageSearchPaths(std::vector<std::filesystem::path> dirs) { m_packagePaths = std::move(dirs); }

    /// Архивы пакетов, реально использованные при резолве импортов (канонические пути,
    /// в порядке первого использования). Нужны записи кеша `--run` для инвалидации по
    /// содержимому пакета (в т.ч. по C++-заголовкам, которые не являются модулями).
    [[nodiscard]] const std::vector<std::filesystem::path>& usedPackageArchives() const noexcept { return m_packages.usedArchives(); }

  private:
    /// Разрешённая цель импорта: корни-кандидаты + РАЗОБРАННЫЙ spelling (`ModulePath`:
    /// тип префикса, компоненты, имя пакета). Один тип на весь домен путей модулей.
    struct ModuleTarget {
        std::vector<std::filesystem::path> roots;
        ModulePath path;
    };

    [[nodiscard]] ModuleTarget resolveModuleTarget(ModuleIndex callerIdx, std::string_view moduleId) const;
    /// Развернуть относительный якорь (`\$$`, `\\$$`, `\\\$$`) в конкретный путь от логического
    /// имени текущего модуля. `nullopt` — якорь не образует адресуемый путь (пустой префикс,
    /// headerless self и т.п.).
    [[nodiscard]] std::optional<ModulePath> resolveAnchorPath(const ModulePath& anchored, ModuleIndex caller) const;
    /// ЕДИНАЯ проверка циклической зависимости: запись уже есть, но тело ещё не сконвертировано
    /// (модуль в процессе загрузки). `Severity::Fatal` с цепочкой импорта - продолжать нельзя.
    void checkImportCycle(std::string_view moduleId, MapperRange range) const;
    /// Верх стека загрузки либо `ModuleIndex::invalid()` (вне загрузки: семантика/утилиты).
    [[nodiscard]] ModuleIndex activeModuleIndex() const noexcept;
    /// Кэш spelling импорта МОДУЛЯ-ВЛАДЕЛЬЦА (spelling резолвится от scope импортёра, поэтому
    /// храним результат у владельца, а не в глобальной/scope-таблице).
    [[nodiscard]] const std::vector<ModuleIndex>* findImportSpellingOfModule(ModuleIndex owner, std::string_view spelling) const;
    /// Spelling, однозначно встречающийся у всех владельцев (иначе nullptr: нет или конфликт).
    /// Только для вызовов ВНЕ загрузки (диагностика/тесты); резолв модулей — по активному модулю.
    [[nodiscard]] const std::vector<ModuleIndex>* findImportSpellingUnique(std::string_view spelling) const;
    /// Импортировал ли ЭТОТ владелец такой spelling (уникальность module-алиаса — в его модуле).
    [[nodiscard]] bool hasImportSpelling(ModuleIndex owner, std::string_view spelling) const;
    /// Все ли файлы кэшированного импорта загружены.
    [[nodiscard]] bool allImportsLoaded(const std::vector<ModuleIndex>& indices) const;
    /// Имя пакета-владельца из логического пути модуля (`\\\pkg\...`); пусто — файл проекта.
    [[nodiscard]] static std::string packageNameOf(std::string_view logicalName);
    /// Для относительного импорта из файла пакета сохранить `\\\pkg`-префикс найденного корня.
    void inheritPackageNameOfCaller(ModuleIndex caller, const std::filesystem::path& foundScope, std::string& packageName) const;
    /// Диапазон заголовка модуля для диагностик.
    [[nodiscard]] MapperRange moduleHeaderRange(MapperFile src, const ModuleHeaderScan& header) const;
    /// Алиас модуля, объявленный @p owner (`spelling → цель`); `nullopt` — не объявлен.
    [[nodiscard]] std::optional<ModuleIndex> findModuleAlias(ModuleIndex owner, std::string_view alias) const;
    /// Результат поиска spelling (алиас модуля либо сайт импорта) БЕЗ загрузки.
    struct SpellingLookup {
        enum class Kind : uint8_t { NotFound, ModuleAlias, Import };
        Kind kind = Kind::NotFound;
        std::vector<ModuleIndex> indices; ///< alias: 1 индекс; import: 1..N (группа).
        [[nodiscard]] bool found() const noexcept { return kind != Kind::NotFound; }
    };
    /// ЕДИНСТВЕННАЯ лестница поиска spelling: module-алиас активного модуля → карта импортов
    /// (scope активного модуля; вне загрузки — однозначный поиск по всем scope). Канонический
    /// путь ФС — ДРУГОЙ домен: в ссылках/проверке загруженности не участвует (только cycle-детект).
    [[nodiscard]] SpellingLookup lookupSpelling(std::string_view spelling) const;
    /// Загрузка группы файлов: файл или каталог (нерекурсивно). Возвращает индексы файлов.
    [[nodiscard]] std::vector<ModuleIndex> ensureLoadedGroup(std::string_view moduleId, MapperRange range);
    /// Фатальная диагностика ненайденного/неоднозначного импорта.
    [[noreturn]] void reportModuleNotFound(std::string_view moduleId, MapperRange range, const std::string& message) const;
    /// ЕДИНСТВЕННАЯ точка фатального выхода загрузчика: диагностика + прекращение компиляции.
    [[noreturn]] void reportFatal(MapperRange range, std::string message) const;
    /// Цепочка текущей загрузки (`main -> a -> b`) + замыкающий идентификатор - для диагностики.
    [[nodiscard]] std::string importChain(std::string_view closingModuleId) const;
    /// ЕДИНСТВЕННАЯ точка диагностики цикла: текст и Severity формируются здесь.
    [[noreturn]] void reportImportCycle(std::string_view moduleId, MapperRange range) const;
    /// Резолв цели `\file`/`\\group...`/`\\\pkg...` в список исходников: ЕДИНАЯ проверка
    /// «0 / >1 кандидатов», обход группы (нерекурсивно) и правила формы пути. `outScopeRoot`
    /// описывает область найденных файлов, `outPackageName` — пакетную идентичность. Fatal при
    /// невозможности резолва (без silent fallback).
    [[nodiscard]] std::vector<std::filesystem::path> resolveTargetFiles(const ModuleTarget& target, std::string_view moduleId, MapperRange range,
                                                                        std::filesystem::path& outScopeRoot, std::string& outPackageName);
    [[nodiscard]] ModuleRegistry& registry() { return m_registry; }
    [[nodiscard]] const ModuleRegistry& registry() const { return m_registry; }

    void pushModule(ModuleIndex idx);
    void popModule();

    /// RAII-состояние разбора модуля: стек загрузки, скоуп макросов и текущий модуль разбора
    /// снимаются даже при FatalError/исключении парсера.
    class ParseScope;

    Context& m_ctx;
    ModuleRegistry m_registry;
    std::vector<ModuleIndex> m_moduleStack;
    /// Spelling сайта импорта (`\dir\mod`) → индексы У ВЛАДЕЛЬЦА (модуля-импортёра). Один и тот же
    /// spelling у разных владельцев адресует разные файлы (их scope-корни различны) — глобальной
    /// таблицы нет. Канонические пути живут ТОЛЬКО в реестре (`ModuleRegistry::findIndex`).
    std::unordered_map<ModuleIndex, std::unordered_map<std::string, std::vector<ModuleIndex>, StringViewHash, std::equal_to<>>> m_importSpellings;
    /// Алиасы МОДУЛЕЙ (`\m :- \pkg\deep\mod;`): владелец spelling → (spelling → загруженный модуль).
    /// Алиас действует только в модуле объявления и не экспортируется.
    std::unordered_map<ModuleIndex, std::unordered_map<std::string, ModuleIndex, StringViewHash, std::equal_to<>>> m_moduleAliases;
    PackageStore m_packages;                           ///< поиск/извлечение пакетов (чистая зона без Context)
    std::vector<std::filesystem::path> m_modulePaths;  ///< дополнительные корни модулей (`-I`)
    std::vector<std::filesystem::path> m_packagePaths; ///< дополнительные корни пакетов (`--package-path`)
    /// Кэш проекций: ключ — (модуль, набор масок в ЕДИНОЙ нормализации
    /// `utils::ImportMasks::normalizedKey`, порядок/дубликаты не значимы).
    std::map<std::pair<ModuleIndex, std::string>, std::shared_ptr<const SiteProjection>> m_siteProjections;
};

} // namespace trust