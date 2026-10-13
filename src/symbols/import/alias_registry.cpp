#include "symbols/import/alias_registry.hpp"

#include "utils/error.hpp"
#include "utils/string_interner.hpp"

#include <algorithm>
#include <tuple>

namespace trust {

namespace {

bool lessNamespaceAliasInfo(const NamespaceAliasInfo& a, const NamespaceAliasInfo& b) {
    return std::tie(a.importer, a.alias) < std::tie(b.importer, b.alias);
}

} // namespace

void AliasRegistry::recordUsing(Scope& scope, UsingScopeRecord record) {
    const std::string_view internedPath = internText(record.path);
    for (const std::size_t ref : scope.usingRefs) {
        const UsingScopeRecord& existing = m_using[ref];
        if (existing.importer == record.importer && existing.targetModule == record.targetModule && existing.path == internedPath) {
            return;
        }
    }
    record.path = internedPath;
    scope.usingRefs.push_back(m_using.size());
    m_using.push_back(std::move(record));
}

bool AliasRegistry::recordAlias(Scope& scope, NamespaceAliasRecord record) {
    if (record.alias.empty() || !record.owner.isValid() || !record.targetModule.isValid() || record.path.empty()) {
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

void AliasRegistry::recordExportedFacts(ModuleIndex owner, std::span<const ModuleNamespaceAlias> aliases) {
    EXPECT(owner.isValid() && "AliasRegistry::recordExportedFacts: owner module must be valid");
    for (const ModuleNamespaceAlias& alias : aliases) {
        EXPECT(!alias.alias.empty() && "exported namespace alias must have a non-empty name");
        EXPECT(!alias.path.empty() && "exported namespace alias must target a non-empty namespace path");
        EXPECT(alias.targetModule.isValid() && "exported namespace alias must target a valid module");
        const std::string_view internedAlias = internText(alias.alias);
        const std::string_view internedPath = internText(alias.path);
        bool exists = false;
        for (const NamespaceAliasRecord& existing : m_aliases) {
            if (existing.origin != AliasOrigin::ExportedFact || existing.owner != owner || existing.alias != internedAlias) {
                continue;
            }
            // Политика конфликта `(owner, alias)`: повторный ИДЕНТИЧНЫЙ факт идемпотентен, другая
            // цель того же префикса невозможна (язык запрещает привязку префикса к двум областям).
            EXPECT(existing.targetModule == alias.targetModule && existing.path == internedPath &&
                       "exported namespace alias conflicts with an already recorded target");
            exists = true;
            break;
        }
        if (!exists) {
            m_aliases.push_back(NamespaceAliasRecord{owner, internedAlias, alias.targetModule, internedPath, MapperRange{}, AliasOrigin::ExportedFact});
        }
    }
}

bool AliasRegistry::promoteAliasToRoot(Scope& rootScope, NamespaceAliasRecord record) {
    record.origin = AliasOrigin::PromotedToRoot;
    record.scopeRange = MapperRange{};
    return recordAlias(rootScope, std::move(record));
}

bool AliasRegistry::hasExportedFacts(ModuleIndex owner) const noexcept {
    for (const NamespaceAliasRecord& record : m_aliases) {
        if (record.origin == AliasOrigin::ExportedFact && record.owner == owner) {
            return true;
        }
    }
    return false;
}

const NamespaceAliasRecord* AliasRegistry::longestExportedAlias(ModuleIndex owner, std::string_view nsPath, std::string_view* rest) const {
    const NamespaceAliasRecord* best = nullptr;
    for (const NamespaceAliasRecord& record : m_aliases) {
        if (record.origin != AliasOrigin::ExportedFact || record.owner != owner || record.targetModule == owner) {
            continue;
        }
        std::string_view candidateRest;
        if (!matchesAliasPrefix(record.alias, nsPath, &candidateRest)) {
            continue;
        }
        if (best == nullptr || record.alias.size() > best->alias.size()) {
            best = &record;
            if (rest != nullptr) {
                *rest = candidateRest;
            }
        }
    }
    return best;
}

std::vector<UsingScopeInfo> AliasRegistry::usingProjection() const {
    std::vector<UsingScopeInfo> out;
    out.reserve(m_using.size());
    for (const UsingScopeRecord& record : m_using) {
        out.push_back(UsingScopeInfo{record.importer, record.targetModule, std::string(record.path), record.scopeRange});
    }
    return out;
}

std::vector<NamespaceAliasInfo> AliasRegistry::aliasProjection() const {
    std::vector<NamespaceAliasInfo> out;
    for (const NamespaceAliasRecord& record : m_aliases) {
        if (record.origin == AliasOrigin::ExportedFact) {
            continue; // отдельная проекция фактов интерфейса
        }
        out.push_back(NamespaceAliasInfo{record.owner, std::string(record.alias), record.targetModule, std::string(record.path), record.scopeRange});
    }
    return out;
}

std::vector<NamespaceAliasInfo> AliasRegistry::exportedProjection() const {
    std::vector<NamespaceAliasInfo> out;
    for (const NamespaceAliasRecord& record : m_aliases) {
        if (record.origin != AliasOrigin::ExportedFact) {
            continue;
        }
        out.push_back(NamespaceAliasInfo{record.owner, std::string(record.alias), record.targetModule, std::string(record.path), MapperRange{}});
    }
    std::sort(out.begin(), out.end(), lessNamespaceAliasInfo);
    return out;
}

} // namespace trust
