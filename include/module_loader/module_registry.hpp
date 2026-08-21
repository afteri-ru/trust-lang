// module_registry.hpp - реестр загруженных модулей
// ModuleRegistry - реестр на динамическом массиве.
// Идентификация модулей - только по индексу (std::size_t).
// moduleId (std::string_view) используется ТОЛЬКО при загрузке для
// детекции циклических зависимостей.
//
// Детекция циклической зависимости: если запись уже есть в реестре, но
// её cacheApi ещё не заполнен, значит модуль в процессе загрузки - FAULT.

#pragma once

#include "ast/ast_nodes.hpp"
#include "location/location.hpp"
#include "syntax/term_types.h"
#include "utils/error.hpp"
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <variant>

namespace trust {

// ----------------------------------------------------------------------------
// ModuleRegistry - реестр загруженных модулей
// ----------------------------------------------------------------------------

class ModuleRegistry {
  public:
    ModuleRegistry() = default;

    ModuleRegistry(const ModuleRegistry&) = delete;
    ModuleRegistry& operator=(const ModuleRegistry&) = delete;
    ModuleRegistry(ModuleRegistry&&) noexcept = default;
    ModuleRegistry& operator=(ModuleRegistry&&) noexcept = default;

    /// Получить существующий загруженный модуль или создать новый для загрузки.
    /// @param moduleId Канонический идентификатор модуля (filePath).
    /// @return Индекс записи о модуле в массиве.
    ///         Если модуль уже загружен - возвращает существующий индекс.
    ///         Если модуль не найден - создаёт новую запись и вставляет в реестр.
    ///         FAULT при циклической зависимости (запись существует, но cacheApi == nullptr).
    [[nodiscard]] std::size_t getOrLoad(std::string_view moduleId);

    /// Проверить, загружен ли модуль по индексу.
    /// FAULT если index >= count().
    [[nodiscard]] bool isLoaded(std::size_t index) const;

    /// Установить исходный Term-тело модуля (результат макропроцессинга).
    /// Это единственное хранимое поле (аналог ModuleNode::m_body): хранится как Term,
    /// а в AstNode конвертируется рекурсивно при построении ModuleNode.
    /// FAULT если index >= count().
    void setBody(std::size_t index, TermPtr term);

    /// Получить Term-тело модуля по индексу.
    /// FAULT если index >= count() или term == nullptr.
    [[nodiscard]] const TermPtr& body(std::size_t index) const;

    /// Получить имя модуля по индексу.
    /// FAULT если index >= count().
    [[nodiscard]] const std::string& moduleName(std::size_t index) const;

    /// Установить экспортируемый интерфейс модуля (вектор деклараций-термов).
    /// Интерфейс заполняется анализатором; это «полный» список экспортов модуля.
    /// FAULT если index >= count().
    void setInterface(std::size_t index, std::vector<TermPtr> interface);

    /// Получить экспортируемый интерфейс модуля. FAULT если index >= count().
    [[nodiscard]] const std::vector<TermPtr>& interface(std::size_t index) const;

    /// Есть ли заполненный интерфейс у модуля. FAULT если index >= count().
    [[nodiscard]] bool hasInterface(std::size_t index) const;

    /// Установить сконвертированное тело модуля (AST), хранимое ОДИН раз.
    /// Используется семантикой (анализ модуля один раз) и кодогенерацией
    /// (forward-decl объявлений по экспортированным именам).
    /// FAULT если index >= count().
    void setBodyAst(std::size_t index, std::vector<AstNodePtr> bodyAst);

    /// Получить сконвертированное тело модуля (AST). FAULT если index >= count().
    [[nodiscard]] const std::vector<AstNodePtr>& bodyAst(std::size_t index) const;

    /// Количество зарегистрированных модулей.
    [[nodiscard]] std::size_t count() const noexcept { return m_modules.size(); }

  private:
    struct ModuleRecord {
        std::string m_moduleId;                                 ///< Канонический идентификатор (filePath)
        std::variant<MapperFile, void*> m_handle{MapperFile{}}; ///< MapperFile (source) или void* (binary, dlopen)

        TermPtr m_body; ///< Исходное Term-тело модуля (результат макропроцессинга)

        /// Экспортируемые декларации-термы модуля (заполняет анализатор; «полный» экспорт).
        std::vector<TermPtr> m_interface;
        /// Сконвертированное тело модуля (AST), хранимое один раз.
        std::vector<AstNodePtr> m_bodyAst;
    };

    std::vector<ModuleRecord> m_modules;
};

} // namespace trust