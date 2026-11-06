#pragma once

// Менеджер flow-анализов над MIR: ЕДИНСТВЕННАЯ точка запуска анализов.
// Контракт:
//   * Анализ объявляет имя и зависимости (имена других анализов); менеджер строит
//     топологический порядок, сохраняя порядок регистрации для независимых анализов.
//   * Анализ получает ГОТОВЫЙ `MirModule` (CFG построены один раз), а не HIR.
//   * Цикл зависимостей/неизвестная зависимость/дубль имени — внутренняя ошибка (FAULT),
//     а не тихий пропуск: порядок анализов влияет на диагностики.
//   * Кэш между прогонами не вводится (incremental-кэш — вне текущей архитектуры); в пределах
//     одного прогона каждый анализ выполняется ровно один раз.

#include "ir/cfg.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>
#include "utils/error.hpp"

namespace trust::flow {

class Analysis {
  public:
    virtual ~Analysis() = default;

    /// Уникальное имя анализа (ключ зависимостей).
    [[nodiscard]] virtual std::string_view name() const = 0;

    /// Имена анализов, которые обязаны выполниться ДО этого.
    [[nodiscard]] virtual std::span<const std::string_view> dependencies() const { return {}; }

    /// Выполнить анализ над готовым MIR.
    virtual void run(const MirModule& mir) = 0;
};

class AnalysisManager {
  public:
    void add(std::unique_ptr<Analysis> analysis) { m_analyses.push_back(std::move(analysis)); }

    void run(const MirModule& mir) {
        const std::vector<std::size_t> order = topologicalOrder();
        for (const std::size_t index : order) {
            m_analyses[index]->run(mir);
        }
    }

  private:
    enum class VisitState : std::uint8_t { Unvisited, Visiting, Done };

    [[nodiscard]] std::vector<std::size_t> topologicalOrder() const {
        // Имя анализа — ПУБЛИЧНЫЙ контракт плагина (ключ зависимостей), а не identity факта
        // компилятора: дубли/циклы отвергаются ниже, порядок независимых — регистрация (R42).
        std::unordered_map<std::string_view, std::size_t> byName;
        byName.reserve(m_analyses.size());
        for (std::size_t i = 0; i < m_analyses.size(); ++i) {
            const std::string_view name = m_analyses[i]->name();
            EXPECT(!name.empty() && "analysis must have a non-empty name");
            EXPECT(byName.emplace(name, i).second && "analysis names must be unique");
        }

        std::vector<VisitState> state(m_analyses.size(), VisitState::Unvisited);
        std::vector<std::size_t> order;
        order.reserve(m_analyses.size());

        const auto visit = [&](const auto& self, std::size_t index) -> void {
            if (state[index] == VisitState::Done) {
                return;
            }
            if (state[index] == VisitState::Visiting) {
                FAULT("analysis dependency cycle at '{}'", m_analyses[index]->name());
            }
            state[index] = VisitState::Visiting;
            for (const std::string_view dependency : m_analyses[index]->dependencies()) {
                const auto it = byName.find(dependency);
                if (it == byName.end()) {
                    FAULT("analysis '{}' depends on unknown analysis '{}'", m_analyses[index]->name(), dependency);
                }
                self(self, it->second);
            }
            state[index] = VisitState::Done;
            order.push_back(index);
        };

        for (std::size_t i = 0; i < m_analyses.size(); ++i) {
            visit(visit, i);
        }
        return order;
    }

    std::vector<std::unique_ptr<Analysis>> m_analyses;
};

} // namespace trust::flow
