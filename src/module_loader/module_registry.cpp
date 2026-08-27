// module_registry.cpp - реализация ModuleRegistry
#include "module_loader/module_registry.hpp"

namespace trust {

// ----------------------------------------------------------------------------
// ModuleRegistry implementation
// ----------------------------------------------------------------------------

std::size_t ModuleRegistry::getOrLoad(std::string_view moduleId) {
    // Линейный поиск среди зарегистрированных модулей
    for (std::size_t i = 0; i < m_modules.size(); ++i) {
        if (m_modules[i].m_moduleId == moduleId) {
            if (!m_modules[i].m_body) {
                // Запись существует, но Term ещё не установлен - модуль в процессе загрузки
                FAULT("Cyclic module dependency detected: {}", moduleId);
            }
            return i;
        }
    }

    // Создаём новую запись
    std::size_t index = m_modules.size();
    ModuleRecord record;
    record.m_moduleId = std::string(moduleId);
    m_modules.push_back(std::move(record));
    return index;
}

bool ModuleRegistry::isLoaded(std::size_t index) const {
    EXPECT(index < m_modules.size());
    return m_modules[index].m_body != nullptr;
}

void ModuleRegistry::setBody(std::size_t index, TermPtr term) {
    EXPECT(index < m_modules.size());
    m_modules[index].m_body = std::move(term);
}

const TermPtr& ModuleRegistry::body(std::size_t index) const {
    EXPECT(index < m_modules.size());
    EXPECT(m_modules[index].m_body != nullptr && "module term is not set");
    return m_modules[index].m_body;
}

const std::string& ModuleRegistry::moduleName(std::size_t index) const {
    EXPECT(index < m_modules.size());
    return m_modules[index].m_moduleId;
}

void ModuleRegistry::setInterface(std::size_t index, std::vector<TermPtr> interface) {
    EXPECT(index < m_modules.size());
    m_modules[index].m_interface = std::move(interface);
}

const std::vector<TermPtr>& ModuleRegistry::interface(std::size_t index) const {
    EXPECT(index < m_modules.size());
    return m_modules[index].m_interface;
}

bool ModuleRegistry::hasInterface(std::size_t index) const {
    EXPECT(index < m_modules.size());
    return !m_modules[index].m_interface.empty();
}

void ModuleRegistry::setBodyAst(std::size_t index, std::vector<AstNodePtr> bodyAst) {
    EXPECT(index < m_modules.size());
    m_modules[index].m_bodyAst = std::move(bodyAst);
}

const std::vector<AstNodePtr>& ModuleRegistry::bodyAst(std::size_t index) const {
    EXPECT(index < m_modules.size());
    return m_modules[index].m_bodyAst;
}

} // namespace trust