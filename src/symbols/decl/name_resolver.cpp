#include "symbols/decl/name_resolver.hpp"

#include "location/namespace_alias.hpp"
#include "symbols/decl/decl_registry.hpp"
#include "ast/ast_nodes.hpp"
#include "types/names.hpp"
#include "utils/error.hpp"
#include "utils/string_interner.hpp"
#include "utils/strings.hpp"

#include <algorithm>

namespace trust {

namespace {

/// Запись поверхности принадлежит экспорт-поверхности @p module: не file-private и не объявление
/// другого модуля, перенесённое сюда квалифицированно.
bool isEntryOfModule(const StorageKey& key, ModuleIndex module) noexcept {
    if (key.visibility == VisibilityClass::FilePrivate) {
        return false;
    }
    return !key.module.isValid() || key.module == module;
}

} // namespace

void ensureOverloadView(Symbol& symbol, std::string_view namespacePath) {
    if (symbol.overloads.empty() && symbol.decl != nullptr && symbol.decl->is<FuncDecl>()) {
        symbol.overloads.push_back(SymbolOverload{symbol.type.getTypeId(), CallableParamInfo{}, symbol.decl, internText(namespacePath)});
    }
    for (SymbolOverload& overload : symbol.overloads) {
        if (overload.namespacePath.empty()) {
            overload.namespacePath = internText(namespacePath);
        }
    }
}

NameResolver::NameResolver(ScopeStack& scopes, AliasRegistry& aliases, ModuleSurface& surface, ImportSiteRegistry& imports, ModuleContext& module,
                           SymbolArena& arena, const StorageKeyFactory& keys)
: m_scopes(&scopes)
, m_aliases(&aliases)
, m_surface(&surface)
, m_imports(&imports)
, m_module(&module)
, m_arena(&arena)
, m_keys(&keys) {
}

void NameResolver::rebind(ScopeStack& scopes, AliasRegistry& aliases, ModuleSurface& surface, ImportSiteRegistry& imports, ModuleContext& module,
                          SymbolArena& arena, const StorageKeyFactory& keys) noexcept {
    m_scopes = &scopes;
    m_aliases = &aliases;
    m_surface = &surface;
    m_imports = &imports;
    m_module = &module;
    m_arena = &arena;
    m_keys = &keys;
}

bool NameResolver::isSiteActive(const ImportSite& site) const {
    if (site.scopeRange.isInvalid()) {
        return true; // сайт уровня файла/модуля действует на всей территории модуля
    }
    for (std::size_t i = 0; i < m_scopes->depth(); ++i) {
        const Scope& scope = m_scopes->at(i);
        if (scope.creator != nullptr && !scope.creator->range().isInvalid() && scope.creator->range().begin.asPacked() == site.scopeRange.begin.asPacked()) {
            return true;
        }
    }
    return false;
}

bool NameResolver::hasVisibleDirectSite(ModuleIndex module, std::string_view qualifiedName, SiteDeclVisibility declVisibility, bool requireActiveScope) const {
    const std::string_view nsPath = utils::namespacePathOf(qualifiedName);
    const std::string_view name = utils::shortNameOf(qualifiedName);
    for (const ImportSite& site : m_imports->journal()) {
        if (site.importer != m_module->currentModule() || site.imported != module) {
            continue;
        }
        if (requireActiveScope && !isSiteActive(site)) {
            continue;
        }
        const SiteVisibilityInput input{site.importer, module, declVisibility, nsPath, name, site.masks, SiteAccess::Direct, site.protectedAllowed};
        if (siteVisibilityOf(input) == SiteVisibility::VisibleFlat) {
            return true;
        }
    }
    return false;
}

bool NameResolver::protectedAccessible(const Symbol& sym, ModuleIndex module, std::string_view qualifiedName) const {
    if (sym.decl == nullptr || sym.decl->visibility != AstNodeBase::Visibility::Protected) {
        return true; // не protected — правило видимости уже решено ключами/сайтом
    }
    if (sym.decl->module != module) {
        return true; // реэкспорт: ось видимости принадлежит ВЛАДЕЛЬЦУ, а не сайту фасада
    }
    return hasVisibleDirectSite(module, qualifiedName, SiteDeclVisibility::Protected, /*requireActiveScope=*/false);
}

Symbol* NameResolver::findKeyWithDecl(const StorageKey& key, const AstNodeBase* decl) const {
    if (decl == nullptr) {
        return nullptr;
    }
    for (std::size_t i = m_scopes->depth(); i-- > 0;) {
        auto found = m_scopes->at(i).symbols.find(key);
        if (found == m_scopes->at(i).symbols.end()) {
            continue;
        }
        // Ключ занят ДРУГИМ объявлением (локальная/иная сущность с тем же spelling):
        // явная модульная ссылка его не затеняется - продолжаем поиск во внешних скоупах.
        if (found->second.decl == decl) {
            return &found->second;
        }
    }
    return nullptr;
}

const Symbol* NameResolver::findExported(ModuleIndex module, std::string_view qualifiedName, ResolveOptions options) const {
    return findExportedMutable(module, qualifiedName, options);
}

Symbol* NameResolver::findExportedMutable(ModuleIndex module, std::string_view qualifiedName, ResolveOptions options) const {
    if (qualifiedName.empty()) {
        return nullptr;
    }
    if (!m_surface->hasModule(module)) {
        return nullptr;
    }
    const std::string_view nsPath = utils::namespacePathOf(qualifiedName);
    const std::string_view name = utils::shortNameOf(qualifiedName);
    // Идентичность модуля: ключ уникален, локальное имя ссылку не затеняет.
    const StorageKey identity = moduleExportKey(module, nsPath, name);
    Symbol* sym = nullptr;
    for (std::size_t i = m_scopes->depth(); i-- > 0;) {
        auto found = m_scopes->at(i).symbols.find(identity);
        if (found != m_scopes->at(i).symbols.end()) {
            sym = &found->second;
            break;
        }
    }
    // Реэкспорт: имя принадлежит ВЛАДЕЛЬЦУ, но опубликовано файлом-фасадом `module`
    // (`\lib(a, b);` в его блоке интерфейса). Отдельного ключа идентичности нет — имя ищем по
    // ключу владельца в таблице реэкспорта фасада.
    if (sym == nullptr) {
        if (const ExportRecord* re = m_surface->findReExport(module, nsPath, name); re != nullptr) {
            sym = re->symbol;
        }
    }
    // Имя объявлено в модуле, но identity-ключа в живом скоупе нет: объявления модульного уровня
    // импортируемого модуля могли быть зарегистрированы только в снятом скоупе блока. Запись
    // поверхности модуля — долговременный источник.
    if (sym == nullptr) {
        if (const ExportRecord* declaration = m_surface->findDeclaration(module, nsPath, name);
            declaration != nullptr && declaration->symbol != nullptr && isEntryOfModule(declaration->key, module)) {
            // Запись принадлежит экспорт-поверхности ЭТОГО модуля: file-private (`__::`) наружу
            // не видно, а перенесённое чужое объявление (`key.module` ≠ module) публикует свой модуль.
            sym = declaration->symbol;
        }
    }

    if (sym != nullptr) {
        if (!protectedAccessible(*sym, module, qualifiedName)) {
            return nullptr;
        }
        if (module == m_module->currentModule()) {
            return sym;
        }
        if (options.viaAlias) {
            return sym; // явная привязка (namespace-алиас/using) авторизует доступ к экспорту
        }
        // Прямой сайт импорта целевого модуля у текущего модуля. Транзитивный импорт и
        // `\owner::name` через фасад прямого сайта не имеют; импорт в закрытом блоке отсекается
        // живым плоским ключом ниже.
        const bool directVisibleSite = hasVisibleDirectSite(module, qualifiedName, SiteDeclVisibility::Public, /*requireActiveScope=*/true);
        if (!options.maskBypass && !directVisibleSite) {
            return nullptr;
        }
        if (options.maskBypass) {
            // Только диагностика «скрыто маской/транзитивностью», не резолв.
            if (findKeyWithDecl(moduleKey(module, nsPath, name, /*importHidden=*/true), sym->decl) != nullptr) {
                return sym;
            }
            return nullptr;
        }
        // Видимость сайта: плоская привязка к этому объявлению либо запись неоднозначности.
        if (findKeyWithDecl(exportedKeyOf(qualifiedName), sym->decl) != nullptr) {
            return sym;
        }
        if (const std::vector<ModuleIndex>* candidates = ambiguousModules(qualifiedName);
            candidates != nullptr && std::find(candidates->begin(), candidates->end(), module) != candidates->end()) {
            return sym;
        }
        // Активный прямой сайт с подходящей маской авторизует экспорт даже без плоского ключа
        // (тело модуля, объявление в снятом namespace-блоке).
        return sym;
    }
    // File-private (`__::`) объявление видно только СВОЕМУ модулю.
    if (module == m_module->currentModule()) {
        const StorageKey privateKey = moduleKey(module, nsPath, name, /*importHidden=*/false);
        for (std::size_t i = m_scopes->depth(); i-- > 0;) {
            auto found = m_scopes->at(i).symbols.find(privateKey);
            if (found != m_scopes->at(i).symbols.end()) {
                return &found->second;
            }
        }
    }
    return nullptr;
}

namespace {

/// Символ в скоупе по ключу (невалидируемая ссылка; nullptr — нет).
Symbol* lookupInScope(Scope& scope, const StorageKey& key) {
    auto found = scope.symbols.find(key);
    return found == scope.symbols.end() ? nullptr : &found->second;
}

} // namespace

Symbol* NameResolver::resolveImpl(std::string_view name, bool materialize) const {
    // Namespace-алиас: самый ДЛИННЫЙ привязанный префикс в ближайшем скоупе (вложенные алиасы).
    for (std::size_t i = m_scopes->depth(); i-- > 0;) {
        const NamespaceAliasRecord* best = nullptr;
        std::string_view bestRest;
        for (const std::size_t ref : m_scopes->at(i).aliasRefs) {
            const NamespaceAliasRecord& alias = m_aliases->aliasAt(ref);
            if (alias.owner != m_module->currentModule()) {
                continue; // алиас другого модуля (перенесённый из его тела) не виден
            }
            if (!matchesAliasPrefix(alias.alias, name) || name.size() == alias.alias.size()) {
                continue;
            }
            if (best == nullptr || alias.alias.size() > best->alias.size()) {
                best = &alias;
                bestRest = name.substr(alias.alias.size() + 2);
            }
        }
        if (best == nullptr) {
            continue;
        }
        // Цель алиаса. Модульная — точный резолв в модуле-владельце. Без модуля (отложенный резолв:
        // область неоднозначна по модулям) — раскрываем префикс и решаем ОБЫЧНЫМ поиском.
        const std::string qualified = utils::qualifyTrustName(best->path, bestRest);
        if (best->targetModule.isValid()) {
            if (Symbol* found = findExportedMutable(best->targetModule, qualified, ResolveOptions{.viaAlias = true})) {
                if (materialize) {
                    ensureOverloadView(*found, utils::namespacePathOf(qualified));
                }
                return found;
            }
        } else if (Symbol* found = resolveImplNoAlias(qualified, materialize)) {
            return found;
        }
        break; // внутренний скоуп привязал префикс: внешние алиасы затенены
    }
    return resolveImplNoAlias(name, materialize);
}

Symbol* NameResolver::resolveImplNoAlias(std::string_view name, bool materialize) const {
    // Плоский ключ (exported, без namespace-пути) — ключ локальных объявлений и flat-экспорта.
    const StorageKey flat = exportedKeyOf(name);
    for (std::size_t i = m_scopes->depth(); i-- > 0;) {
        Scope& scope = m_scopes->at(i);
        // Модульный уровень: видимость имени определяется ключом — private текущего модуля,
        // import-hidden (file-local) и плоский экспорт.
        if (scope.kind == ScopeKind::ModuleRoot) {
            for (const StorageKey& key : m_keys->variants(m_module->currentModule(), name)) {
                // Плоская привязка неоднозначна: если все кандидаты — функции, наборы перегрузок
                // сливаются и выбор делает OverloadResolver; иначе голое имя не резолвится.
                if (isExportedVisibility(key.visibility) && scope.ambiguous.count(key) != 0) {
                    if (Symbol* merged = mergeAmbiguousCallables(scope, key)) {
                        return merged;
                    }
                    continue;
                }
                if (Symbol* found = lookupInScope(scope, key)) {
                    return found;
                }
            }
            continue;
        }
        // Неоднозначность проверяется на ЛЮБОМ уровне скоупа: импорт в блоке пишет запись туда же,
        // где живёт привязка, поэтому искать её только на модульном уровне нельзя.
        if (scope.ambiguous.count(flat) != 0) {
            if (Symbol* merged = mergeAmbiguousCallables(scope, flat)) {
                return merged;
            }
            continue;
        }
        if (Symbol* found = lookupInScope(scope, flat)) {
            return found;
        }
    }
    return nullptr;
}

const Symbol* NameResolver::resolvePlain(std::string_view name, bool materialize) const {
    return resolveImpl(name, materialize);
}

Symbol* NameResolver::resolveBinding(std::string_view name) const {
    return resolveImpl(name, /*materialize=*/true);
}

const std::vector<ModuleIndex>* NameResolver::ambiguousModules(std::string_view name) const {
    const StorageKey flat = exportedKeyOf(name);
    for (std::size_t i = m_scopes->depth(); i-- > 0;) {
        const auto it = m_scopes->at(i).ambiguous.find(flat);
        if (it != m_scopes->at(i).ambiguous.end()) {
            return &it->second.modules;
        }
    }
    return nullptr;
}

UsingResolveResult NameResolver::resolveUsing(std::string_view name) const {
    UsingResolveResult result;
    for (std::size_t i = m_scopes->depth(); i-- > 0;) {
        Scope& scope = m_scopes->at(i);
        if (scope.usingRefs.empty()) {
            continue;
        }
        std::vector<Symbol*> hits;
        std::string hitText;
        for (const std::size_t ref : scope.usingRefs) {
            const UsingScopeRecord& search = m_aliases->usingAt(ref);
            const std::string q = utils::qualifyTrustName(search.path, name);
            if (Symbol* found = resolveImpl(q, /*materialize=*/true)) {
                if (std::find(hits.begin(), hits.end(), found) == hits.end()) {
                    hits.push_back(found);
                    hitText = q;
                }
            }
        }
        if (hits.empty()) {
            continue;
        }
        if (hits.size() == 1) {
            result.symbol = hits.front();
            result.qualifiedText = hitText;
            return result;
        }
        // Несколько целей в ОДНОМ скоупе: callable-наборы сливаются, выбор — по аргументам.
        std::vector<const Symbol*> candidates(hits.begin(), hits.end());
        Symbol* merged = mergeCallableCandidates(candidates, /*rejectDuplicateSignatures=*/false, *m_arena);
        if (merged == nullptr) {
            result.ambiguous = true;
            return result;
        }
        // Текст ссылки не квалифицируется: путь берётся у выбранной перегрузки.
        result.symbol = merged;
        return result;
    }
    return result;
}

Symbol* NameResolver::mergeAmbiguousCallables(const Scope& scope, const StorageKey& flatKey) const {
    const auto it = scope.ambiguous.find(flatKey);
    if (it == scope.ambiguous.end() || it->second.decls.size() < 2) {
        return nullptr;
    }
    std::vector<const Symbol*> candidates;
    candidates.reserve(it->second.decls.size());
    for (const AstNodeBase* decl : it->second.decls) {
        Symbol* candidate = findDeclaringSymbol(*m_scopes, *m_module, decl);
        if (candidate == nullptr || candidate->decl == nullptr || !candidate->decl->is<FuncDecl>() || candidate->overloads.empty()) {
            return nullptr; // не все кандидаты — вызываемые функции: имя остаётся неоднозначным
        }
        candidates.push_back(candidate);
    }
    return mergeCallableCandidates(candidates, /*rejectDuplicateSignatures=*/true, *m_arena);
}

} // namespace trust
