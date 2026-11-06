#include "module_loader/registry/module_registry.hpp"

namespace trust {

ModuleIndex ModuleRegistry::getOrLoad(std::string_view moduleId) {
    const auto it = m_indexById.find(moduleId);
    if (it != m_indexById.end()) {
        if (m_modules[it->second.value].m_bodyAst == nullptr) {
            // Запись существует, но AST ещё не сконвертирован - модуль в процессе загрузки.
            // Пользовательскую диагностику с цепочкой выдаёт загрузчик (checkImportCycle);
            // здесь FAULT = нарушение контракта вызывающего, а не ошибка программы.
            FAULT("Cyclic module dependency detected: {}", moduleId);
        }
        return it->second;
    }

    const ModuleIndex index{m_modules.size()};
    m_moduleIds.emplace_back(moduleId);
    m_modules.push_back(ModuleRecord{});
    m_indexById.emplace(std::string_view(m_moduleIds.back()), index);
    return index;
}

bool ModuleRegistry::isLoaded(ModuleIndex index) const {
    EXPECT(index.value < m_modules.size());
    return m_modules[index.value].m_bodyAst != nullptr;
}

std::string_view ModuleRegistry::modulePath(ModuleIndex index) const {
    EXPECT(index.value < m_modules.size());
    return m_moduleIds[index.value];
}

std::optional<ModuleIndex> ModuleRegistry::findIndex(std::string_view moduleId) const {
    const auto it = m_indexById.find(moduleId);
    if (it == m_indexById.end()) {
        return std::nullopt;
    }
    return it->second;
}

void ModuleRegistry::setInterfaceFacts(ModuleIndex index, ModuleInterfaceFacts facts) {
    EXPECT(index.value < m_modules.size());
    m_modules[index.value].m_interfaceFacts = std::move(facts);
}

const ModuleInterfaceFacts& ModuleRegistry::interfaceFacts(ModuleIndex index) const {
    EXPECT(index.value < m_modules.size());
    EXPECT(m_modules[index.value].m_interfaceFacts.has_value() && "module interface facts were not computed");
    return *m_modules[index.value].m_interfaceFacts;
}

bool ModuleRegistry::hasInterfaceFacts(ModuleIndex index) const {
    EXPECT(index.value < m_modules.size());
    return m_modules[index.value].m_interfaceFacts.has_value();
}

void ModuleRegistry::setBodyAst(ModuleIndex index, std::vector<AstNodePtr> bodyAst) {
    EXPECT(index.value < m_modules.size());
    m_modules[index.value].m_bodyAst = std::make_shared<std::vector<AstNodePtr>>(std::move(bodyAst));
}

const std::vector<AstNodePtr>& ModuleRegistry::bodyAst(ModuleIndex index) const {
    EXPECT(index.value < m_modules.size());
    EXPECT(m_modules[index.value].m_bodyAst != nullptr && "module body was not converted");
    return *m_modules[index.value].m_bodyAst;
}

std::shared_ptr<std::vector<AstNodePtr>> ModuleRegistry::bodyAstShared(ModuleIndex index) const {
    EXPECT(index.value < m_modules.size());
    EXPECT(m_modules[index.value].m_bodyAst != nullptr && "module body was not converted");
    return m_modules[index.value].m_bodyAst;
}

void ModuleRegistry::setScope(ModuleIndex index, std::filesystem::path scopeRoot) {
    EXPECT(index.value < m_modules.size());
    m_modules[index.value].m_scopeRoot = std::move(scopeRoot);
}

const std::filesystem::path& ModuleRegistry::scopeRoot(ModuleIndex index) const {
    EXPECT(index.value < m_modules.size());
    return m_modules[index.value].m_scopeRoot;
}

void ModuleRegistry::setIdentity(ModuleIndex index, ModulePath identity) {
    EXPECT(index.value < m_modules.size());
    m_modules[index.value].m_identity = std::move(identity);
}

std::optional<ModulePath> ModuleRegistry::identity(ModuleIndex index) const {
    EXPECT(index.value < m_modules.size());
    const ModuleRecord& record = m_modules[index.value];
    if (record.m_identity.has_value()) {
        return record.m_identity;
    }
    return ModulePath::parse(record.m_logicalName);
}

void ModuleRegistry::setLogicalName(ModuleIndex index, std::string name) {
    EXPECT(index.value < m_modules.size());
    m_modules[index.value].m_logicalName = std::move(name);
}

const std::string& ModuleRegistry::logicalName(ModuleIndex index) const {
    EXPECT(index.value < m_modules.size());
    return m_modules[index.value].m_logicalName;
}

} // namespace trust
