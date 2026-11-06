#pragma once

// Логический модульный контекст таблицы символов: модуль объявления, узел КОРНЕВОГО
// (модульного) скоупа и глубина private-области. Единственная точка ответа на вопросы
// «в каком модуле мы регистрируем имена» и «открыт ли корень модуля»: индексы/глубина
// стека наружу не выходят.

#include "location/module_index.hpp"

#include <cstddef>

namespace trust {

class ModuleContext {
  public:
    /// Узел-корень модуля отсутствует (stdlib DSL/тестовые таблицы без ModuleNode).
    static constexpr std::size_t kNoModuleRoot = static_cast<std::size_t>(-1);

    void setCurrentModule(ModuleIndex module) noexcept { m_currentModule = module; }
    [[nodiscard]] ModuleIndex currentModule() const noexcept { return m_currentModule; }

    /// Вход в скоуп-корень модуля (push ModuleNode): фиксируется ПЕРВЫЙ такой узел дерева.
    void enterModuleScope(std::size_t rootNode) noexcept;
    /// Выход из скоупа-корня модуля (pop ModuleNode). `rootNode` обязан совпадать с
    /// зафиксированным корнем: вызов на другом узле — внутренняя ошибка.
    void leaveModuleScope(std::size_t rootNode);

    [[nodiscard]] bool hasModuleRoot() const noexcept { return m_rootNode != kNoModuleRoot; }
    /// Узел дерева скоупов, зафиксированный как корень модуля (0 — корня нет: global).
    [[nodiscard]] std::size_t moduleRootNode() const noexcept { return hasModuleRoot() ? m_rootNode : 0; }

    void enterFilePrivate() noexcept { ++m_privateDepth; }
    void leaveFilePrivate() noexcept { --m_privateDepth; }

    /// Текущий скоуп — уровень идентичности модуля и открыта private-область (`__::`).
    /// `atModuleLevel` вычисляет владелец дерева (ScopeKind::ModuleRoot).
    [[nodiscard]] bool inFilePrivateScope(bool atModuleLevel) const noexcept { return m_privateDepth > 0 && atModuleLevel; }

  private:
    ModuleIndex m_currentModule{};
    std::size_t m_rootNode = kNoModuleRoot;
    int m_privateDepth = 0;
};

} // namespace trust
