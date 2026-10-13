#include "symbols/decl/decl_registry.hpp"
#include "symbols/symbol_table.hpp"

#include "types/names.hpp"
#include "utils/strings.hpp"

#include "ast/ast_nodes.hpp"
#include "ast/token.hpp"

#include <array>
#include <utility>

namespace trust {

SymbolTable::SymbolTable()
: m_names(m_scopes, m_aliases, m_surface, m_imports, m_module, m_arena, m_keys)
, m_registry(m_scopes, m_surface, m_arena, m_module, m_imports, m_keys) {
}

SymbolTable::SymbolTable(SymbolTable&& other) noexcept
: m_scopes(std::move(other.m_scopes))
, m_imports(std::move(other.m_imports))
, m_aliases(std::move(other.m_aliases))
, m_surface(std::move(other.m_surface))
, m_arena(std::move(other.m_arena))
, m_types(other.m_types)
, m_module(std::move(other.m_module))
, m_keys(other.m_keys)
, m_names(m_scopes, m_aliases, m_surface, m_imports, m_module, m_arena, m_keys)
, m_registry(m_scopes, m_surface, m_arena, m_module, m_imports, m_keys, m_types) {
}

SymbolTable& SymbolTable::operator=(SymbolTable&& other) noexcept {
    m_scopes = std::move(other.m_scopes);
    m_imports = std::move(other.m_imports);
    m_aliases = std::move(other.m_aliases);
    m_surface = std::move(other.m_surface);
    m_arena = std::move(other.m_arena);
    m_types = other.m_types;
    m_module = std::move(other.m_module);
    m_keys = other.m_keys;
    m_names.rebind(m_scopes, m_aliases, m_surface, m_imports, m_module, m_arena, m_keys);
    m_registry.rebind(m_scopes, m_surface, m_arena, m_module, m_imports, m_keys);
    m_registry.setTypes(m_types);
    return *this;
}

void SymbolTable::push(const AstNodeBase* creator) {
    const bool moduleDecl = creator != nullptr && creator->kind() == ParserToken::Kind::ModuleDecl;
    // Корень модуля фиксируется ОДИН раз: первый ModuleNode либо (при его отсутствии) первый
    // вложенный скоуп.
    const bool firstModuleRoot = moduleDecl && !m_module.hasModuleRoot();
    const bool implicitModuleRoot = !moduleDecl && !m_module.hasModuleRoot() && m_scopes.depth() == 1;
    const ScopeKind kind = (firstModuleRoot || implicitModuleRoot) ? ScopeKind::ModuleRoot : ScopeKind::Block;
    m_scopes.push(creator, kind);
    if (firstModuleRoot) {
        m_module.enterModuleScope(m_scopes.currentIndex());
    }
}

void SymbolTable::pop() {
    if (m_scopes.depth() > 1) {
        const std::size_t leaving = m_scopes.currentIndex();
        if (m_module.hasModuleRoot() && m_module.moduleRootNode() == leaving) {
            m_module.leaveModuleScope(leaving);
        }
        m_scopes.pop();
    }
}

DeclResult SymbolTable::declare(const Symbol& sym) {
    const bool moduleRoot = atModuleLevel();
    const auto keys = declarationStorageKeys(sym.name, moduleRoot, declVisibilityOf(sym));
    return m_registry.store(current(), sym, keys, StorePolicy{.surface = surfacePolicy(), .completeForward = false});
}

DeclResult SymbolTable::declareOrComplete(const Symbol& sym) {
    const bool moduleRoot = atModuleLevel();
    const auto keys = declarationStorageKeys(sym.name, moduleRoot, declVisibilityOf(sym));
    return m_registry.store(current(), sym, keys, StorePolicy{.surface = surfacePolicy(), .completeForward = true, .moduleRoot = moduleRoot});
}

DeclResult SymbolTable::declareGlobalOrComplete(const Symbol& sym) {
    return m_registry.storeGlobal(sym);
}

void SymbolTable::declareGlobal(const Symbol& sym) {
    m_registry.storeGlobalIfAbsent(sym);
}

DeclResult SymbolTable::declareInRoot(const Symbol& sym) {
    // КОРНЕВОЙ для модуля скоуп - scope с creator=ModuleNode; prelude/без ModuleNode - global.
    // Перенос namespace-контейнера: имя уже квалифицировано; видимость (маски/транзитивность)
    // применяется к переносимому объявлению так же, как к top-level.
    const auto keys = declarationStorageKeys(sym.name, /*moduleRoot=*/true, declVisibilityOf(sym));
    const bool aliasOfThisFile = sym.decl != nullptr && sym.decl->is<FuncDecl>() && sym.decl->as<FuncDecl>()->aliasKind() != AliasKind::None;
    // Экспорт-поверхность обязана существовать и для УЖЕ зарегистрированного символа (иначе
    // `\path::name` из импортёра не находит имя) — публикацию решает SurfacePolicy::RootDeclaration.
    const StorePolicy policy{.surface = SurfacePolicy::RootDeclaration, .aliasOfThisFile = aliasOfThisFile, .completeForward = true, .moduleRoot = true};
    return m_registry.store(m_scopes.node(moduleRootScopeNode()), sym, keys, policy);
}

SurfacePolicy SymbolTable::surfacePolicy() const noexcept {
    if (atModuleLevel()) {
        return SurfacePolicy::DeclarationKey;
    }
    // Модуль мог быть загружен внутри блока интерфейса/вложенного импорта: объявления модульного
    // уровня публикуются и вне корневого скоупа. Локальные объявления в поверхность не входят.
    const bool inModuleBody = !m_imports.empty() && m_imports.top().imported == m_module.currentModule();
    return inModuleBody ? SurfacePolicy::PositionDerived : SurfacePolicy::None;
}

void SymbolTable::completeTypeDeclaration(const Symbol& sym, const StorageKey& key) {
    m_registry.completeType(sym, key);
}

bool SymbolTable::isForeignDeclaration(const Symbol& existing) const {
    return trust::isForeignDeclaration(existing, m_module.currentModule());
}

bool SymbolTable::hasNamespaceInModule(ModuleIndex module, std::string_view path) const {
    return m_surface.hasNamespace(module, path);
}

bool SymbolTable::hasNamespaceInScopes(std::string_view prefix) const {
    if (prefix.empty()) {
        return false;
    }
    for (std::size_t i = 0; i < m_scopes.depth(); ++i) {
        for (const auto& [key, sym] : m_scopes[i].symbols) {
            (void)sym;
            if (key.namespacePath == prefix) {
                return true;
            }
        }
    }
    return false;
}

const std::vector<ModuleIndex>* SymbolTable::ambiguousModules(std::string_view name) const {
    return m_names.ambiguousModules(name);
}

bool SymbolTable::bindAlias(std::string_view aliasName, const Symbol& target, const AstNodeBase& aliasDecl) {
    const bool moduleRoot = (atModuleLevel());
    const SiteDeclVisibility declVisibility = aliasDecl.isFilePrivate() ? SiteDeclVisibility::Private : SiteDeclVisibility::Public;
    const std::vector<StorageKey> keys = declarationStorageKeys(aliasName, /*moduleRoot=*/moduleRoot, declVisibility);
    // Имя символа — имя-алиас (оно и есть видимое имя); тип/перегрузки/`decl` — от цели,
    // поэтому ссылка через алиас нормализуется на объявление-цель.
    Symbol alias = target;
    alias.name = aliasName;
    // Экспорт-поверхность модуля — только для объявлений модульного уровня, не file-private и не
    // внутри функции/типа/кодового блока (позицию вычисляет surfaceKeyForPosition).
    const bool publishSurface = !aliasDecl.isFilePrivate() && !atFilePrivateLevel();
    const StorePolicy policy{
        .surface = publishSurface ? SurfacePolicy::PositionDerived : SurfacePolicy::None, .aliasOfThisFile = true, .completeForward = false};
    // Имя занято другим объявлением — алиас не переопределяет имена (диагностирует вызывающий).
    return m_registry.store(current(), alias, keys, policy) != DeclResult::Duplicate;
}

MapperRange SymbolTable::currentScopeRange() const {
    const AstNodeBase* creator = currentCreator();
    return creator != nullptr ? creator->range() : MapperRange{};
}

void SymbolTable::bindImportedModule(ModuleIndex imported, const utils::ImportMasks& masks, bool includeProtected) {
    // Привязка поверхности модуля к сайту импорта — зона регистрации (единая точка записи скоупов).
    m_registry.bindImportedModule(makeImportSite(imported, masks, includeProtected));
}

std::array<StorageKey, 4> SymbolTable::storageKeyVariants(std::string_view name) const {
    return m_keys.variants(m_module.currentModule(), name);
}

ImportSite SymbolTable::makeImportSite(ModuleIndex importedModule, const utils::ImportMasks& masks, bool includeProtected) const {
    ImportSite site;
    site.importer = m_module.currentModule();
    site.imported = importedModule;
    site.masks = masks;
    site.scopeRange = currentScopeRange();
    site.protectedAllowed = includeProtected;
    return site;
}

void SymbolTable::pushImportScope(ModuleIndex importedModule, const utils::ImportMasks& masks, bool includeProtected) {
    // Одна точка записи: сайт попадает и в активный стек, и в журнал (дедуп внутри).
    m_imports.push(makeImportSite(importedModule, masks, includeProtected));
}

void SymbolTable::popImportScope() {
    m_imports.pop();
}

std::vector<StorageKey> SymbolTable::declarationStorageKeys(std::string_view qualifiedName, bool moduleRoot, SiteDeclVisibility declVisibility) const {
    if (!moduleRoot) {
        // Локальный/блочный скоуп - видимость не фильтруется.
        return {m_keys.localKey(qualifiedName)};
    }
    return m_registry.declarationStorageKeysIn(m_module.currentModule(), qualifiedName, declVisibility);
}

StorageKey SymbolTable::declarationStorageKey(std::string_view name) const {
    const auto keys = declarationStorageKeys(name, /*moduleRoot=*/(atModuleLevel()), SiteDeclVisibility::Public);
    // Ключи регистрации всегда непусты (см. declarationStorageKeys).
    EXPECT(!keys.empty() && "declarationStorageKey: keys must never be empty");
    return keys.front();
}

const Symbol* SymbolTable::lookupCurrent(std::string_view name) const {
    for (const StorageKey& key : storageKeyVariants(name)) {
        if (const Symbol* found = current().lookup(key)) {
            return found;
        }
    }
    return nullptr;
}

const Symbol* SymbolTable::resolve(std::string_view name) const {
    return m_names.resolvePlain(name);
}

Symbol* SymbolTable::resolveMutable(std::string_view name) {
    return m_names.resolveBinding(name);
}

const Symbol* SymbolTable::findVisibleFromModule(ModuleIndex module, std::string_view qualifiedName, bool maskBypass, bool viaAlias) const {
    return m_names.findExported(module, qualifiedName, ResolveOptions{.maskBypass = maskBypass, .viaAlias = viaAlias});
}

} // namespace trust
