#include "symbols/import/import_sites.hpp"

#include "utils/error.hpp"

namespace trust {

std::size_t ImportSiteRegistry::record(const ImportSite& site) {
    for (std::size_t index = 0; index < m_journal.size(); ++index) {
        const ImportSite& existing = m_journal[index];
        if (existing.importer == site.importer && existing.imported == site.imported && existing.masks.sameAs(site.masks) &&
            existing.scopeRange.begin.asPacked() == site.scopeRange.begin.asPacked()) {
            return index;
        }
    }
    m_journal.push_back(site);
    return m_journal.size() - 1;
}

void ImportSiteRegistry::push(const ImportSite& site) { m_stack.push_back(record(site)); }

void ImportSiteRegistry::pop() {
    if (!m_stack.empty()) {
        m_stack.pop_back();
    }
}

const ImportSite& ImportSiteRegistry::top() const {
    EXPECT(!m_stack.empty() && "ImportSiteRegistry::top: no active import site");
    const std::size_t index = m_stack.back();
    EXPECT(index < m_journal.size() && "ImportSiteRegistry::top: active site index is outside the journal");
    return m_journal[index];
}

} // namespace trust
