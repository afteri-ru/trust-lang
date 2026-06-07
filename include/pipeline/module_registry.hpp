// module_registry.hpp — реестр загруженных модулей
// ModuleRegistry — реестр на динамическом массиве.
// Идентификация модулей — только по индексу (std::size_t).
// moduleId (std::string_view) используется ТОЛЬКО при загрузке для
// детекции циклических зависимостей.
//
// Детекция циклической зависимости: если запись уже есть в реестре, но
// её cacheApi ещё не заполнен, значит модуль в процессе загрузки — FAULT.

#pragma once

#include "ast/ast_nodes.hpp"
#include "diag/location.hpp"
#include "utils/error.hpp"
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <variant>

namespace trust {

// ────────────────────────────────────────────────────────────────────────────
// ModuleRegistry — реестр загруженных модулей
// ────────────────────────────────────────────────────────────────────────────

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
    ///         Если модуль уже загружен — возвращает существующий индекс.
    ///         Если модуль не найден — создаёт новую запись и вставляет в реестр.
    ///         FAULT при циклической зависимости (запись существует, но cacheApi == nullptr).
    [[nodiscard]] std::size_t getOrLoad(std::string_view moduleId);

    /// Проверить, загружен ли модуль по индексу.
    /// FAULT если index >= count().
    [[nodiscard]] bool isLoaded(std::size_t index) const;

    /// Установить preprocessed (результат макропроцессинга) для модуля с указанным индексом.
    /// FAULT если index >= count().
    void setPreprocessed(std::size_t index, std::shared_ptr<std::vector<AstNodePtr>> body);

    /// Установить cacheApi для модуля с указанным индексом.
    /// FAULT если index >= count().
    void setCacheApi(std::size_t index, std::shared_ptr<std::vector<AstNodePtr>> api);

    /// Получить preprocessed модуля по индексу.
    /// FAULT если index >= count() или preprocessed == nullptr.
    [[nodiscard]] const std::vector<AstNodePtr>& preprocessed(std::size_t index) const;

    /// Получить cacheApi модуля по индексу.
    /// FAULT если index >= count() или cacheApi == nullptr.
    [[nodiscard]] const std::vector<AstNodePtr>& cacheApi(std::size_t index) const;

    /// Получить имя модуля по индексу.
    /// FAULT если index >= count().
    [[nodiscard]] const std::string& moduleName(std::size_t index) const;

    /// Количество зарегистрированных модулей.
    [[nodiscard]] std::size_t count() const noexcept { return m_modules.size(); }

  private:
    struct ModuleRecord {
        std::string m_moduleId;                                 ///< Канонический идентификатор (filePath)
        std::variant<MapperFile, void*> m_handle{MapperFile{}}; ///< MapperFile (source) или void* (binary, dlopen)

        std::shared_ptr<std::vector<AstNodePtr>> m_preprocessed; ///< Результат макропроцессинга (только для source)
        std::shared_ptr<std::vector<AstNodePtr>> m_cacheApi;     ///< API модуля (только объявления, для обоих типов)
    };

    std::vector<ModuleRecord> m_modules;
};

} // namespace trust