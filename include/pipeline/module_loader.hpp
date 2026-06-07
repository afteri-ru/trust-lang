// module_loader.hpp — единый загрузчик модулей
#pragma once

#include "pipeline/module_registry.hpp"
#include "diag/location.hpp"
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace trust {

class Context;

class ModuleLoader {
  public:
    explicit ModuleLoader(Context& ctx);
    ModuleLoader(const ModuleLoader&) = delete;
    ModuleLoader& operator=(const ModuleLoader&) = delete;
    ModuleLoader(ModuleLoader&&) noexcept = delete;
    ModuleLoader& operator=(ModuleLoader&&) noexcept = delete;

    // ── Метод A: загрузка модуля из уже зарегистрированного source-файла ──
    // Регистрирует модуль, рекурсивно вызывает Parser (expand_module=true),
    // конвертирует в AST и кладёт результат в реестр (preprocessed + cacheApi).
    // src — реальный source-файл (главный файл или загруженный import); парсинг
    // выполняется из него, чтобы маппинги привязывались к реальному файлу.
    // Возвращает индекс загруженного модуля (кешируется: повторный вызов вернёт тот же индекс).
    // При циклической зависимости — diag().report(Severity::Fatal, range, ...).
    [[nodiscard]] std::size_t parseSourceModule(std::string_view moduleId, MapperFile src, MapperRange range = {});

    // ── Метод B: загрузка по идентификатору (резолв файла) ──
    // Ищет файл <path>.trust (заглушка) или <path>.src, загружает его и вызывает
    // Метод A с реальным MapperFile. При отсутствии модуля/заглушке —
    // diag().report(Severity::Fatal, range, ...) с позицией вызова.
    [[nodiscard]] std::size_t ensureLoaded(std::string_view moduleId, MapperRange range = {});

    // ── Поиск индекса модуля по имени/каноническому пути (только lookup, без загрузки) ──
    [[nodiscard]] std::optional<std::size_t> indexOf(std::string_view moduleIdOrName) const;

    // ── Проверка загруженности модуля ──
    [[nodiscard]] bool isLoaded(std::size_t index) const;

    // ── Доступ к данным загруженного модуля ──
    /// Преобразованные AST-узлы тела модуля (FAULT если не загружен).
    [[nodiscard]] const std::vector<AstNodePtr>& preprocessed(std::size_t index) const;
    /// Каноническое имя (путь) модуля по индексу.
    [[nodiscard]] const std::string& moduleName(std::size_t index) const;

    // ── Текущий индекс модуля (верх стека) ──
    [[nodiscard]] std::size_t currentModuleIndex() const;

  private:
    [[nodiscard]] std::string resolveModulePath(std::size_t callerIdx, std::string_view moduleId) const;
    [[nodiscard]] ModuleRegistry& registry() { return m_registry; }
    [[nodiscard]] const ModuleRegistry& registry() const { return m_registry; }

    void pushModule(std::size_t idx);
    void popModule();

    Context& m_ctx;
    ModuleRegistry m_registry;
    std::vector<std::size_t> m_moduleStack;
    std::unordered_map<std::string, std::size_t> m_indexByName;
};

} // namespace trust