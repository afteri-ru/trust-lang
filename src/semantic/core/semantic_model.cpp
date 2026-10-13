#include "semantic/semantic_model.hpp"

#include "ast/ast_nodes.hpp"
#include "semantic/core/pass.hpp"
#include "utils/error.hpp"

#include <utility>

namespace trust {

namespace {

bool containsImportedModule(const std::vector<ImportSite>& sites, ModuleIndex module) {
    for (const ImportSite& site : sites) {
        if (site.imported == module) {
            return true;
        }
    }
    return false;
}

} // namespace

SemanticModel::SemanticModel(std::unique_ptr<AnalysisContext> analysis, std::shared_ptr<StringInterner> strings, std::shared_ptr<const TypeSystem> types,
                             ModuleProjection modules, ReferenceIndex references)
: m_analysis(std::move(analysis))
, m_types(std::move(types))
, m_strings(std::move(strings))
, m_modules(std::move(modules))
, m_references(std::move(references)) {
    EXPECT(m_analysis != nullptr && "SemanticModel requires the analysis context of a finished semantic run");
    EXPECT(m_types != nullptr && "SemanticModel requires the type system owner");
    const SymbolTable& table = m_analysis->symbols();
    const ModuleIndex main = m_modules.mainModuleIndex;
    // Прямые сайты импорта ГЛАВНОГО файла: импорт не транзитивен.
    for (const ImportSite& site : table.imports().journal()) {
        if (site.importer == main) {
            m_importSites.push_back(site);
        }
    }
    for (ReExportedNameInfo re : table.surface().reExportProjection()) {
        if (containsImportedModule(m_importSites, re.facade)) {
            m_reExports.push_back(std::move(re));
        }
    }
    for (UsingScopeInfo scope : table.aliases().usingProjection()) {
        if (scope.importer == main) {
            m_usingScopes.push_back(std::move(scope));
        }
    }
    for (NamespaceAliasInfo alias : table.aliases().aliasProjection()) {
        if (alias.importer == main) {
            m_namespaceAliases.push_back(std::move(alias));
        }
    }
    for (NamespaceAliasInfo alias : table.aliases().exportedProjection()) {
        if (containsImportedModule(m_importSites, alias.importer)) {
            m_exportedNamespaceAliases.push_back(std::move(alias));
        }
    }
}

SemanticModel::~SemanticModel() = default;
SemanticModel::SemanticModel(SemanticModel&&) noexcept = default;
SemanticModel& SemanticModel::operator=(SemanticModel&&) noexcept = default;

const SymbolTable& SemanticModel::table() const {
    return m_analysis->symbols();
}

const SymbolIndex& SemanticModel::symbols() const {
    return m_analysis->state().symbolIndex();
}

const ReferenceIndex& SemanticModel::references() const {
    return m_references;
}

std::size_t SemanticModel::scopeAt(MapperRange point) const {
    EXPECT(!point.isInvalid() && "scopeAt requires a valid source range");
    const ScopeStack& scopes = table().scopes();
    std::size_t best = 0;
    std::size_t bestDepth = 0;
    for (std::size_t index = 0; index < scopes.size(); ++index) {
        const Scope& scope = scopes.node(index);
        if (scope.creator == nullptr) {
            continue;
        }
        const MapperRange range = scope.creator->range();
        if (range.isInvalid() || range.begin.fileIdx() != point.begin.fileIdx()) {
            continue;
        }
        if (point.begin.offset() < range.begin.offset() || point.begin.offset() > range.end.offset()) {
            continue;
        }
        std::size_t depth = 0;
        for (std::size_t node = index; scopes.node(node).parent != Scope::kNoParent; node = scopes.node(node).parent) {
            ++depth;
        }
        if (best == 0 || depth > bestDepth) {
            best = index;
            bestDepth = depth;
        }
    }
    return best;
}

const Symbol* SemanticModel::lookupAt(std::size_t scopeNode, const StorageKey& key) const {
    const ScopeStack& scopes = table().scopes();
    EXPECT(scopeNode < scopes.size() && "lookupAt: scope node index out of range");
    for (std::size_t node = scopeNode;;) {
        const Scope& scope = scopes.node(node);
        if (const Symbol* found = scope.lookup(key)) {
            return found;
        }
        if (scope.parent == Scope::kNoParent) {
            return nullptr;
        }
        node = scope.parent;
    }
}

const Symbol* SemanticModel::symbolOf(const AstNodeBase* decl) const {
    if (decl == nullptr) {
        return nullptr;
    }
    const ScopeStack& scopes = table().scopes();
    for (std::size_t index = 0; index < scopes.size(); ++index) {
        for (const auto& [key, symbol] : scopes.node(index).symbols) {
            (void)key;
            if (symbol.decl == decl) {
                return &symbol;
            }
            for (const SymbolOverload& overload : symbol.overloads) {
                if (overload.decl == decl) {
                    return &symbol;
                }
            }
        }
    }
    return nullptr;
}

} // namespace trust
