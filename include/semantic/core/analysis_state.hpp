#pragma once

// include/semantic/core/analysis_state.hpp
// Изменяемое состояние ОДНОГО прогона семантики, не являющееся фактом AST:
// проекция символов для LSP, набор уже проанализированных тел модулей и счётчики
// синтезируемых временных. Факты для бэкендов живут в полях узлов AST, не здесь.

#include "location/module_index.hpp"
#include "symbols/symbol_index.hpp"

#include <unordered_set>

namespace trust {

class AnalysisState {
  public:
    /// Проекция объявлений для LSP (заполняется SymbolCollectorHook).
    SymbolIndex& symbolIndex() { return m_symbolIndex; }
    const SymbolIndex& symbolIndex() const { return m_symbolIndex; }

    /// Тело модуля уже проанализировано в этой компиляции (одна AST на модуль).
    [[nodiscard]] bool isModuleBodyAnalyzed(ModuleIndex module) const { return m_analyzedModuleBodies.contains(module); }
    void markModuleBodyAnalyzed(ModuleIndex module) { m_analyzedModuleBodies.insert(module); }

    /// Уникальный id синтезируемой временной scrutinee match (`_matchN`).
    int nextMatchTempId() noexcept { return ++m_match_temp_counter; }
    /// Уникальный id синтезируемой временной hoist возврата (`__trust_res_N`).
    int nextResultTempId() noexcept { return ++m_result_temp_counter; }

  private:
    SymbolIndex m_symbolIndex;
    std::unordered_set<ModuleIndex> m_analyzedModuleBodies;
    int m_match_temp_counter = 0;
    int m_result_temp_counter = 0;
};

} // namespace trust
