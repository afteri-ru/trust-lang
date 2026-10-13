#include "symbols/decl/module_context.hpp"

#include "utils/error.hpp"

namespace trust {

void ModuleContext::enterModuleScope(std::size_t rootNode) noexcept {
    if (m_rootNode == kNoModuleRoot) {
        m_rootNode = rootNode;
    }
}

void ModuleContext::leaveModuleScope(std::size_t rootNode) {
    EXPECT(m_rootNode == rootNode && "ModuleContext::leaveModuleScope: leaving a scope that is not the module root");
    m_rootNode = kNoModuleRoot;
}

} // namespace trust
