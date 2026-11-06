#pragma once

// Владелец СИНТЕТИЧЕСКИХ Symbol: слитые наборы перегрузок (`mergeCallableCandidates`) и символы
// без скоупа. Объявления скоупов живут в persistent-дереве (ScopeStack) и в арене не нуждаются;
// `std::deque` даёт стабильные адреса при росте, элементы не удаляются до разрушения арены.

#include "symbols/table/symbol.hpp"

#include <deque>

namespace trust {

class SymbolArena {
  public:
    /// Копия символа со стабильным адресом. const — арена является mutable-состоянием таблицы,
    /// которое пополняется и в const-путях резолва (слияние неоднозначных callable-наборов).
    Symbol& adopt(const Symbol& symbol) const {
        m_symbols.push_back(symbol);
        return m_symbols.back();
    }

    Symbol& adopt(Symbol&& symbol) const {
        m_symbols.push_back(std::move(symbol));
        return m_symbols.back();
    }

    [[nodiscard]] std::size_t size() const noexcept { return m_symbols.size(); }

  private:
    mutable std::deque<Symbol> m_symbols;
};

} // namespace trust
