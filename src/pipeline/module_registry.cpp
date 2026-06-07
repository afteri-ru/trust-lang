// module_registry.cpp — реализация ModuleRegistry
#include "pipeline/module_registry.hpp"

namespace trust {

// ────────────────────────────────────────────────────────────────────────────
// ModuleRegistry implementation
// ────────────────────────────────────────────────────────────────────────────

std::size_t ModuleRegistry::getOrLoad(std::string_view moduleId) {
    // Линейный поиск среди зарегистрированных модулей
    for (std::size_t i = 0; i < m_modules.size(); ++i) {
        if (m_modules[i].m_moduleId == moduleId) {
            if (!m_modules[i].m_cacheApi) {
                // Запись существует, но cacheApi не заполнен — модуль в процессе загрузки
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
    return m_modules[index].m_cacheApi != nullptr;
}

void ModuleRegistry::setPreprocessed(std::size_t index, std::shared_ptr<std::vector<AstNodePtr>> body) {
    EXPECT(index < m_modules.size());
    m_modules[index].m_preprocessed = std::move(body);
}

void ModuleRegistry::setCacheApi(std::size_t index, std::shared_ptr<std::vector<AstNodePtr>> api) {
    EXPECT(index < m_modules.size());
    m_modules[index].m_cacheApi = std::move(api);
}

const std::vector<AstNodePtr>& ModuleRegistry::preprocessed(std::size_t index) const {
    EXPECT(index < m_modules.size());
    EXPECT(m_modules[index].m_preprocessed != nullptr && "module is not preprocessed");
    return *m_modules[index].m_preprocessed;
}

const std::vector<AstNodePtr>& ModuleRegistry::cacheApi(std::size_t index) const {
    EXPECT(index < m_modules.size());
    EXPECT(m_modules[index].m_cacheApi != nullptr && "module has no cache API");
    return *m_modules[index].m_cacheApi;
}

const std::string& ModuleRegistry::moduleName(std::size_t index) const {
    EXPECT(index < m_modules.size());
    return m_modules[index].m_moduleId;
}

} // namespace trust