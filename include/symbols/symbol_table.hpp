#pragma once

// Фасад таблицы символов: маршруты к зонам ответственности (см. symbols/README.md).
// Владение: Symbol-ы скоупов — ScopeStack; долгоживущие копии — SymbolArena (адреса стабильны);
// lookup/resolve невладеющие и валидны, пока жив соответствующий скоуп/арена.
// Инварианты (порядок обхода std::map, единое правило видимости, один факт — одно хранилище) —
// symbols/MEMORY.md.

#include "symbols/import/alias_registry.hpp"
#include "symbols/decl/decl_registry.hpp"
#include "symbols/import/import_sites.hpp"
#include "symbols/decl/module_surface.hpp"
#include "symbols/decl/module_context.hpp"
#include "symbols/decl/name_resolver.hpp"
#include "symbols/table/scope_stack.hpp"
#include "symbols/table/symbol.hpp"
#include "symbols/table/symbol_arena.hpp"
#include "symbols/symbol_index.hpp"
#include "symbols/table/symbol_key_factory.hpp"
#include "location/location.hpp"
#include "location/module_index.hpp"
#include "ast/token.hpp"
#include "types/call.hpp"
#include "types/names.hpp"
#include "types/type_id.hpp"
#include "utils/import_masks.hpp"
#include "utils/string_interner.hpp"

#include <array>
#include <string_view>
#include <utility>
#include <vector>

namespace trust {

class AstNodeBase;
class TypeSystem;

// Типы Symbol/SymbolOverload/DeclResult/Storage — symbols/table/symbol.hpp (общие для подсистем).
// Scope/PrefixScope/NamespaceAlias/ScopeStack — symbols/table/scope_stack.hpp.

/// SymbolTable - стек вложенных скоупов (единая таблица символов).
class SymbolTable {
  public:
    /// Стек всегда содержит глобальный скоуп (depth() >= 1).
    SymbolTable();

    /// Копирование запрещено: `Symbol*` в экспорт-поверхности указывают в арену таблицы (копия
    /// оставила бы висячие адреса). Перемещение сохраняет адреса элементов deque; зоны
    /// перепривязываются к членам новой таблицы.
    SymbolTable(const SymbolTable&) = delete;
    SymbolTable& operator=(const SymbolTable&) = delete;
    SymbolTable(SymbolTable&& other) noexcept;
    SymbolTable& operator=(SymbolTable&& other) noexcept;

    /// Вход во вложенный скоуп; creator - узел, открывший скоуп (nullptr = глобальный).
    void push(const AstNodeBase* creator = nullptr);

    /// Выход из вложенного скоупа. Глобальный скоуп не удаляется.
    void pop();

    /// Глубина стека (>= 1).
    std::size_t depth() const { return m_scopes.depth(); }

    /// Текущий (верхний) скоуп.
    Scope& current() { return m_scopes.current(); }
    const Scope& current() const { return m_scopes.current(); }

    /// Корневой для модуля скоуп (0 — модульного скоупа нет).
    [[nodiscard]] Scope& moduleRootScope() noexcept { return m_scopes.node(moduleRootScopeNode()); }
    /// Диапазон текущего скоупа (невалидный — файловый уровень).
    [[nodiscard]] MapperRange currentScopeRange() const;

    /// Текущий модуль (логический контекст; задаётся при входе в ModuleNode).
    void setCurrentModule(ModuleIndex module) noexcept { m_module.setCurrentModule(module); }
    [[nodiscard]] ModuleIndex currentModule() const noexcept { return m_module.currentModule(); }

    /// Контекст private области (`__::`/`__{...}` на top-level модуля): объявления
    /// регистрируются в ТЕКУЩЕМ (модульном) скоупе под module-qualified ключом и видны всему модулю.
    void enterFilePrivate() noexcept { m_module.enterFilePrivate(); }
    void exitFilePrivate() noexcept { m_module.leaveFilePrivate(); }

    /// Текущий скоуп — уровень идентичности модуля (ScopeKind::ModuleRoot). Без ModuleNode
    /// (stdlib DSL/тесты) им становится первый вложенный скоуп.
    [[nodiscard]] bool atModuleLevel() const noexcept { return m_scopes.current().kind == ScopeKind::ModuleRoot; }

    /// Текущий контекст — private область (`__::`/`__{...}` на top-level модуля).
    [[nodiscard]] bool inFilePrivateScope() const noexcept { return m_module.inFilePrivateScope(atModuleLevel()); }

    /// Поиск в ТЕКУЩЕМ скоупе с учётом private-ключа текущего модуля (для duplicate-проверок).
    [[nodiscard]] const Symbol* lookupCurrent(std::string_view name) const;

    /// Узел, открывший текущий скоуп (nullptr - глобальный).
    const AstNodeBase* currentCreator() const { return m_scopes.currentCreator(); }

    /// Регистрирует символ в текущем скоупе. Duplicate - имя занято ДРУГИМ объявлением
    /// (дубликат), AlreadyRegistered - повторная регистрация того же узла (идемпотентно;
    /// вызывающий не выдаёт диагностику и не уведомляет хуки). Диагностику формирует
    /// вызывающий (ядро), т.к. ему нужен range.
    DeclResult declare(const Symbol& sym);

    /// Регистрирует символ в ГЛОБАЛЬНОМ скоупе (уровень 0), не перезаписывая существующий.
    /// Используется для КВАЛИФИЦИРОВАННЫХ алиасов нативных шаблонов/классов (поиск по
    /// зарегистрированным оператором `... = X` областям имён: `... = std` + `vector` →
    /// `std::vector`). Алиас переживает pop вложенных скоупов (в отличие от локальных).
    void declareGlobal(const Symbol& sym);

    /// Регистрирует символ в ГЛОБАЛЬНОМ скоупе с поддержкой перегрузок/завершения
    /// forward-объявления (статически регистрируемые члены классов). В отличие от
    /// declareGlobal, второй overload НЕ теряется.
    DeclResult declareGlobalOrComplete(const Symbol& sym);

    /// Регистрирует символ в КОРНЕВОМ (модульном) скоупе под ключом `sym.name`
    /// (используется переносом namespace-контейнера при выходе из `::`/`name::`).
    DeclResult declareInRoot(const Symbol& sym);

    /// Тип-реестр для C++-идентичности сигнатур. Устанавливается AnalysisContext (агрегат прогона); SymbolTable
    /// не владеет реестром (время жизни - дольше таблицы).
    void setTypes(const TypeSystem& types) {
        m_types = &types;
        m_registry.setTypes(&types);
    }

    /// Регистрирует символ в текущем скоупе с поддержкой завершения forward-объявления.
    /// Возвращает DeclResult: Inserted (новое имя), Completed (существующее forward-объявление
    /// того же kind завершено определением - символ обновлён in-place) или Duplicate.
    /// Завершение допустимо ТОЛЬКО когда existing - forward (isForwardDecl) и новый символ -
    /// определение, при совпадении kind (variant index). Два forward, два определения, forward
    /// функции + определение переменной и т.п. → Duplicate.
    /// ИНВАРИАНТ: `sym` НЕ модифицируется (копия кладётся в таблицу) - после вызова хук
    /// `onDeclare(sym)` видит полноценный Symbol (имя/тип/decl), а не moved-from.
    DeclResult declareOrComplete(const Symbol& sym);

    /// Завершает объявленный тип определением в ТЕКУЩЕМ скоупе (forward → определение, тот же
    /// TypeId) и обновляет экспорт-поверхность. `declareOrComplete` для TypeDecl не подходит:
    /// `isForwardDecl` считает любой TypeDecl определением.
    void completeTypeDeclaration(const Symbol& sym, const StorageKey& key);

    /// Реестр экспорт-поверхности модулей (единственное хранилище записей).
    [[nodiscard]] ModuleSurface& surface() noexcept { return m_surface; }
    [[nodiscard]] const ModuleSurface& surface() const noexcept { return m_surface; }

    /// Persistent-дерево скоупов (включая закрытые): объявления переживают выход из блока.
    /// Только чтение — запись идёт через declare/enterScope/exitScope.
    [[nodiscard]] const ScopeStack& scopes() const noexcept { return m_scopes; }

    /// Связать АЛИАС функции: имя `aliasName` в текущем скоупе адресует тот же набор перегрузок,
    /// что и `target`; `Symbol::decl` остаётся объявлением-целью (его модуль и C++-имя).
    /// false — имя уже занято другим объявлением (диагностику выдаёт вызывающий).
    /// `aliasDecl` — узел-алиас: его `filePrivate` определяет приватность (как у обычных
    /// объявлений); локальный/блочный алиас в экспорт-поверхность модуля не попадает.
    /// Хуки объявлений уведомляет ВЫЗЫВАЮЩИЙ (он владеет узлом-алиасом): таблица намеренно не
    /// подменяет `decl` на узел алиаса, иначе ссылка через алиас не нормализуется на цель.
    [[nodiscard]] bool bindAlias(std::string_view aliasName, const Symbol& target, const AstNodeBase& aliasDecl);

    /// Связать УЖЕ проанализированный модуль с текущим сайтом импорта: добавить привязки сайта
    /// (плоский экспорт либо site-hidden ключи) для всех объявлений модуля. Конфликт плоского
    /// ключа с объявлением другого модуля НЕ диагностируется здесь - он фиксируется как
    /// неоднозначность и сообщается на сайте ИСПОЛЬЗОВАНИЯ имени.
    void bindImportedModule(ModuleIndex imported, const utils::ImportMasks& masks, bool includeProtected = false);

    /// Кандидаты неоднозначного имени на текущей позиции (`nullptr` - имя не неоднозначно).
    /// Диагностику формирует ядро (ему известен range узла).
    [[nodiscard]] const std::vector<ModuleIndex>* ambiguousModules(std::string_view name) const;

    /// Объявление принадлежит ДРУГОМУ модулю (site-binding импорта), а не текущему.
    [[nodiscard]] bool isForeignDeclaration(const Symbol& sym) const;

    /// true, если под префиксом есть ХОТЬ ОДНО зарегистрированное имя (`prefix::name`) в стеке
    /// скоупов. Проверка `... = X` на выходе из скоупа (опечатка/несуществующая область).
    [[nodiscard]] bool hasNamespaceInScopes(std::string_view prefix) const;
    /// Существует ли область `path` в экспорт-наборе указанного модуля.
    [[nodiscard]] bool hasNamespaceInModule(ModuleIndex module, std::string_view path) const;

    /// Поиск имени от текущего скоупа вверх (учитывает вложенность и shadowing).
    const Symbol* resolve(std::string_view name) const;

    /// Найти имя так, как его связал сайт импорта модуля @p module: имя обязано быть
    /// экспортом ЭТОГО модуля и видимым на текущем сайте (маски, M1). `maskBypass=true`
    /// дополнительно находит имена, скрытые маской сайта — ТОЛЬКО для точной диагностики,
    /// резолв по нему запрещён. `qualifiedName` — trust-имя внутри модуля (`f`, `ns::f`).
    /// `viaAlias=true` — доступ идёт через явную привязку (namespace-алиас / `using`): прямой
    /// сайт импорта целевого модуля не требуется, авторизацией служит сама привязка.
    const Symbol* findVisibleFromModule(ModuleIndex module, std::string_view qualifiedName, bool maskBypass = false, bool viaAlias = false) const;

    /// Не-const вариант resolve() - для on-the-fly расширения выводимого типа
    /// (join по истории присвоений) анализатором типов выражений.
    Symbol* resolveMutable(std::string_view name);

    /// Плоская таблица глобальных имён (уровень 0).
    const Scope& global() const { return m_scopes.front(); }
    std::size_t globalSize() const { return m_scopes.front().symbols.size(); }

    // Объявления импортируемого модуля регистрируются ПОЛНОСТЬЮ, но под file-local ключом,
    // если маска сайта импорта их скрывает (или импорт транзитивный). Ключ хранит ВИДИМОСТЬ,
    // поэтому поиск фильтруется выбором ключа, а не удалением объявлений.
    /// Войти в тело импортируемого модуля: owner - модуль, в чьём скоупе регистрируются имена
    /// (текущий до переключения), imported - модуль тела, masks - маски сайта (пусто = все).
    /// `includeProtected` — сайт вправе видеть `_::`-имена модуля (та же группа/пакет); правило
    /// видимости — `types/site_visibility.hpp`.
    void pushImportScope(ModuleIndex importedModule, const utils::ImportMasks& masks, bool includeProtected = false);
    void popImportScope();

    /// Реестр сайтов импорта (стек активных сайтов + журнал за компиляцию): проекции читаются
    /// напрямую из зоны (`imports().journal()`), копий нет.
    [[nodiscard]] ImportSiteRegistry& imports() noexcept { return m_imports; }
    [[nodiscard]] const ImportSiteRegistry& imports() const noexcept { return m_imports; }

    /// Реестр using-областей и namespace-алиасов (единственное хранилище записей).
    [[nodiscard]] AliasRegistry& aliases() noexcept { return m_aliases; }
    [[nodiscard]] const AliasRegistry& aliases() const noexcept { return m_aliases; }

    /// Зона резолва имён: единственная точка ответа «что означает имя в этой позиции».
    [[nodiscard]] const NameResolver& names() const noexcept { return m_names; }

    /// Ключи регистрации: идентичность модуля (`ModuleExport`) первой, затем привязки сайта
    /// (плоский экспорт либо site-hidden/owner-hidden). Private-объявление — один ключ.
    /// `declVisibility` — видимость объявления в его модуле (`__::`/protected); правило —
    /// `types/site_visibility.hpp` (siteKeysFor). `qualifiedName` обязан быть СТАБИЛЬНЫМ view.
    [[nodiscard]] std::vector<StorageKey> declarationStorageKeys(std::string_view qualifiedName, bool moduleRoot, SiteDeclVisibility declVisibility) const;
    /// Ключи поиска в модульном скоупе: private, идентичность своего модуля, hidden, плоский.
    [[nodiscard]] std::array<StorageKey, 4> storageKeyVariants(std::string_view name) const;
    /// Primary-ключ регистрации имени в текущем контексте (для типов: storage-ключ реестра).
    [[nodiscard]] StorageKey declarationStorageKey(std::string_view name) const;

    /// Итерация скоупов от текущего (внутреннего) к глобальному. callback(const Scope&).
    /// Используется анализатором для вывода контекста (область имён/функция) из создателей
    /// скоупов вместо параллельного состояния.
    template <typename F>
    void forEachScope(F&& f) const {
        m_scopes.forEachFromInner(std::forward<F>(f));
    }

  private:
    /// Узел дерева скоупов, зафиксированный как корень модуля (0 — корня нет: global).
    [[nodiscard]] std::size_t moduleRootScopeNode() const noexcept { return m_module.moduleRootNode(); }

    /// Построить описание сайта импорта (позиция оператора + глубина цепочки).
    [[nodiscard]] ImportSite makeImportSite(ModuleIndex importedModule, const utils::ImportMasks& masks, bool includeProtected) const;

    /// Объявление находится в file-private области на уровне модуля (не внутри функции).
    [[nodiscard]] bool atFilePrivateLevel() const noexcept { return m_module.inFilePrivateScope(atModuleLevel()); }
    /// Публикация в экспорт-поверхность: модульный уровень — по ключу регистрации; глубже — по
    /// позиции, но только в теле импортируемого модуля (имена интерфейсного блока/области имён).
    [[nodiscard]] SurfacePolicy surfacePolicy() const noexcept;

    ScopeStack m_scopes;
    /// Активные сайты импорта (вложенные - транзитивные) и журнал за компиляцию: единственная
    /// точка записи обоих представлений (см. symbols/import/import_sites.hpp).
    ImportSiteRegistry m_imports;
    /// Единственное хранилище using-областей и namespace-алиасов (скоупы держат индексы записей).
    AliasRegistry m_aliases;
    /// Единственное хранилище экспорт-поверхности модулей (объявления ядра + реэкспорт).
    ModuleSurface m_surface;
    /// Владелец СИНТЕТИЧЕСКИХ Symbol: слитые наборы перегрузок `using`/неоднозначностей
    /// (адреса стабильны). Объявления скоупов живут в ScopeStack. `mutable` — пополняется и в
    /// const-путях резолва.
    mutable SymbolArena m_arena;
    /// Невладеющий реестр для C++-идентичности сигнатур (setTypes из AnalysisContext).
    const TypeSystem* m_types = nullptr;
    /// Логический модульный контекст (текущий модуль, корневой скоуп, private-уровень).
    ModuleContext m_module;
    /// Единственная точка построения ключей таблицы (привязана к текущему модулю).
    StorageKeyFactory m_keys;
    /// Зона резолва имён: единственная точка ответа «что означает имя в этой позиции».
    /// Объявлена после зон-членов: держит указатели на них.
    NameResolver m_names;
    /// Зона регистрации объявлений: единственная точка записи скоупов и экспорт-поверхности.
    /// Объявлена последней: держит ссылки на зоны-члены выше.
    DeclarationRegistry m_registry;
};

} // namespace trust
