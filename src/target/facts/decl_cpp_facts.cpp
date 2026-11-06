#include "target/decl_cpp_facts.hpp"

#include <utility>

namespace trust {

void DeclCppFacts::setCppName(DeclId id, std::string cppName) {
    m_byDecl[id].cppName = std::move(cppName);
}

void DeclCppFacts::setCppSuffix(DeclId id, std::string cppSuffix) {
    m_byDecl[id].cppSuffix = std::move(cppSuffix);
}

std::string_view DeclCppFacts::cppNameOf(DeclId id) const noexcept {
    auto it = m_byDecl.find(id);
    return it == m_byDecl.end() ? std::string_view{} : std::string_view{it->second.cppName};
}

std::string_view DeclCppFacts::cppSuffixOf(DeclId id) const noexcept {
    auto it = m_byDecl.find(id);
    return it == m_byDecl.end() ? std::string_view{} : std::string_view{it->second.cppSuffix};
}

} // namespace trust
