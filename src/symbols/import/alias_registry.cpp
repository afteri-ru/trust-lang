#include "symbols/import/alias_registry.hpp"

#include "utils/error.hpp"
#include "utils/string_interner.hpp"

namespace trust {

void AliasRegistry::recordUsing(Scope& scope, UsingScopeRecord record) {
    const std::string_view internedPath = internText(record.path);
    for (const std::size_t ref : scope.usingRefs) {
        const UsingScopeRecord& existing = m_using[ref];
        if (existing.importer == record.importer && existing.path == internedPath) {
            return;
        }
    }
    record.path = internedPath;
    scope.usingRefs.push_back(m_using.size());
    m_using.push_back(std::move(record));
}

bool AliasRegistry::recordAlias(Scope& scope, NamespaceAliasRecord record) {
    if (record.alias.empty() || !record.owner.isValid() || record.path.empty()) {
        return false;
    }
    const std::string_view internedAlias = internText(record.alias);
    const std::string_view internedPath = internText(record.path);
    for (const std::size_t ref : scope.aliasRefs) {
        const NamespaceAliasRecord& existing = m_aliases[ref];
        if (existing.owner != record.owner || existing.alias != internedAlias) {
            continue;
        }
        return existing.targetModule == record.targetModule && existing.path == internedPath;
    }
    record.alias = internedAlias;
    record.path = internedPath;
    scope.aliasRefs.push_back(m_aliases.size());
    m_aliases.push_back(std::move(record));
    return true;
}

bool AliasRegistry::promoteAliasToRoot(Scope& rootScope, NamespaceAliasRecord record) {
    record.origin = AliasOrigin::PromotedToRoot;
    record.scopeRange = MapperRange{};
    return recordAlias(rootScope, std::move(record));
}

std::vector<UsingScopeInfo> AliasRegistry::usingProjection() const {
    std::vector<UsingScopeInfo> out;
    out.reserve(m_using.size());
    for (const UsingScopeRecord& record : m_using) {
        out.push_back(UsingScopeInfo{record.importer, std::string(record.path), record.scopeRange});
    }
    return out;
}

std::vector<NamespaceAliasInfo> AliasRegistry::aliasProjection() const {
    std::vector<NamespaceAliasInfo> out;
    for (const NamespaceAliasRecord& record : m_aliases) {
        out.push_back(NamespaceAliasInfo{record.owner, std::string(record.alias), record.targetModule, std::string(record.path), record.scopeRange});
    }
    return out;
}


} // namespace trust
