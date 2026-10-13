#pragma once

// include/symbols/import/import_sites.hpp
// Единственная точка записи сайтов импорта таблицы символов: активный стек (резолв видимости)
// и журнал за компиляцию (проекция для LSP/Pipeline). Один тип `ImportSite` обслуживает оба
// представления; дедупликация журнала — по (импортёр, импортированный, маски, позиция скоупа).

#include "symbols/symbol_index.hpp"

#include <cstddef>
#include <vector>

namespace trust {

class ImportSiteRegistry {
  public:
    /// Записать сайт в журнал (дедуп) и вернуть его индекс в журнале.
    std::size_t record(const ImportSite& site);
    /// Записать сайт в журнал (дедуп) И сделать активным. Единственный путь записи обоих
    /// представлений (первый анализ тела импортируемого модуля).
    void push(const ImportSite& site);
    /// Снять активный сайт (стек); журнал сохраняется.
    void pop();
    /// RAII: сайт активен только на время вызова (повторный/ромбовидный импорт). Сайт пишется
    /// в журнал (дедуп) — активный стек хранит ТОЛЬКО индексы журнала.
    class Guard {
      public:
        Guard(ImportSiteRegistry& registry, ImportSite site)
        : m_registry(registry) {
            m_registry.m_stack.push_back(m_registry.record(site));
        }
        ~Guard() { m_registry.m_stack.pop_back(); }
        Guard(const Guard&) = delete;
        Guard& operator=(const Guard&) = delete;

      private:
        ImportSiteRegistry& m_registry;
    };

    [[nodiscard]] bool empty() const noexcept { return m_stack.empty(); }
    [[nodiscard]] std::size_t depth() const noexcept { return m_stack.size(); }
    [[nodiscard]] const ImportSite& top() const;
    [[nodiscard]] const std::vector<ImportSite>& journal() const noexcept { return m_journal; }

  private:
    /// Активные сайты — индексы журнала: сами сайты хранятся ОДИН раз.
    std::vector<std::size_t> m_stack;
    std::vector<ImportSite> m_journal;
};

} // namespace trust
