#pragma once

// include/symbols/decl/name_resolver.hpp
// Зона резолва имён таблицы символов: единственная точка ответа на вопрос «что означает имя в
// этой позиции» (namespace-алиасы → модульный уровень → плоский ключ; видимость сайта импорта).
// Скоупы зона НЕ мутирует: единственная запись — материализация «символа как значения»
// (непустой набор перегрузок + namespace-путь у каждой) в точках привязки, через SymbolArena.
// Правило видимости — `types/site_visibility.hpp` (единственная точка комбинации маска+protected+
// прямой доступ — `hasVisibleDirectSite`).

#include "symbols/import/alias_registry.hpp"
#include "symbols/import/import_sites.hpp"
#include "symbols/decl/module_context.hpp"
#include "symbols/decl/module_surface.hpp"
#include "symbols/table/scope_stack.hpp"
#include "symbols/table/symbol.hpp"
#include "symbols/table/symbol_arena.hpp"
#include "symbols/table/symbol_key_factory.hpp"
#include "location/module_index.hpp"
#include "types/names.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace trust {

class AstNodeBase;

/// Опции поиска имени в модуле-владельце.
struct ResolveOptions {
    bool maskBypass = false; ///< искать имена, скрытые маской сайта (ТОЛЬКО для диагностики)
    bool viaAlias = false;   ///< доступ авторизован явной привязкой (namespace-алиас/`using`)
};

/// Результат резолва голого имени через `using`-области.
struct UsingResolveResult {
    Symbol* symbol = nullptr;
    std::string qualifiedText; ///< квалифицированное trust-имя попадания (пусто при слиянии наборов)
    std::optional<ModuleIndex> module;
    bool ambiguous = false; ///< несколько не-callable целей в одном скоупе
};

/// ЕДИНСТВЕННАЯ точка финализации «символа как значения» перед кодогеном: у функции обязан быть
/// непустой набор перегрузок (представитель) и namespace-путь у каждой перегрузки.
void ensureOverloadView(Symbol& symbol, std::string_view namespacePath);

class NameResolver {
  public:
    NameResolver(ScopeStack& scopes, AliasRegistry& aliases, ModuleSurface& surface, ImportSiteRegistry& imports, ModuleContext& module,
                 SymbolArena& arena, const StorageKeyFactory& keys);
    /// Перепривязать зоны после перемещения таблицы.
    void rebind(ScopeStack& scopes, AliasRegistry& aliases, ModuleSurface& surface, ImportSiteRegistry& imports, ModuleContext& module,
                SymbolArena& arena, const StorageKeyFactory& keys) noexcept;

    /// Резолв простого имени от текущего скоупа вверх. Скоупы не мутируются;
    /// `materialize` включает материализацию «символа как значения» (точки привязки).
    [[nodiscard]] const Symbol* resolvePlain(std::string_view name, bool materialize = false) const;
    /// То же с невладеющим mutable-доступом: анализатор типов дописывает выведенный тип в символ.
    [[nodiscard]] Symbol* resolveBinding(std::string_view name) const;

    /// Имя так, как его связал сайт импорта модуля @p module (идентичность → реэкспорт → поверхность).
    [[nodiscard]] const Symbol* findExported(ModuleIndex module, std::string_view qualifiedName, ResolveOptions options = {}) const;
    /// То же с mutable-доступом (точка привязки `using`: путь объявления дописывается в символ).
    [[nodiscard]] Symbol* findExportedMutable(ModuleIndex module, std::string_view qualifiedName, ResolveOptions options = {}) const;

    /// Резолв голого имени через `using`-области: обход от внутреннего скоупа, внутри — ВСЕ цели;
    /// callable-наборы СЛИВАЮТСЯ (выбор по аргументам), не-callable конфликт — `ambiguous`.
    [[nodiscard]] UsingResolveResult resolveUsing(std::string_view name) const;

    /// Кандидаты неоднозначного имени на текущей позиции (`nullptr` — имя не неоднозначно).
    [[nodiscard]] const std::vector<ModuleIndex>* ambiguousModules(std::string_view name) const;

    /// Есть ли у ТЕКУЩЕГО модуля прямой сайт импорта @p module, с масками которого имя видно.
    /// `requireActiveScope` — ортогональная проверка активности оператора импорта.
    [[nodiscard]] bool hasVisibleDirectSite(ModuleIndex module, std::string_view qualifiedName, SiteDeclVisibility declVisibility,
                                            bool requireActiveScope) const;
    /// Сайт действует на текущей позиции: его скоуп ещё открыт (невалидный range — корень файла).
    [[nodiscard]] bool isSiteActive(const ImportSite& site) const;

  private:
    /// Разрешение неоднозначного плоского имени в объединённый набор перегрузок (в арене).
    [[nodiscard]] Symbol* mergeAmbiguousCallables(const Scope& scope, const StorageKey& flatKey) const;
    /// Символ по ключу в цепочке скоупов, чьё объявление совпадает с @p decl (иначе nullptr).
    [[nodiscard]] Symbol* findKeyWithDecl(const StorageKey& key, const AstNodeBase* decl) const;
    /// Резолв с mutable-доступом: `materialize` включает материализацию перегрузок.
    [[nodiscard]] Symbol* resolveImpl(std::string_view name, bool materialize) const;
    /// Доступен ли protected-символ (`_::`) с активного сайта (правило владельца, не сайта фасада).
    [[nodiscard]] bool protectedAccessible(const Symbol& sym, ModuleIndex module, std::string_view qualifiedName) const;

    ScopeStack* m_scopes;
    AliasRegistry* m_aliases;
    ModuleSurface* m_surface;
    ImportSiteRegistry* m_imports;
    ModuleContext* m_module;
    SymbolArena* m_arena;
    const StorageKeyFactory* m_keys;
};

} // namespace trust
