#pragma once

// include/semantic/trust_analyzer.hpp
// Компонент семантики: TrustAnalyzer. Разделяет AnalysisContext с драйвером NameResolutionPass;
// рекурсия/вызовы других компонентов и драйвера идут через NameResolutionPass (friend).

#include "semantic/pass.hpp"
#include "semantic/symbol_table.hpp"
#include "ast/ast_nodes.hpp"
#include "types/type_id.hpp"
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace trust {

class NameResolutionPass;

class TrustAnalyzer {
  public:
    explicit TrustAnalyzer(AnalysisContext& actx, NameResolutionPass& core)
    : m_actx(actx)
    , m_core(core) {}
    void analyzeTrustContract(TrustContract& tc);
    void processTrustConditions(const std::vector<AstNodePtr>& trust, const AstNodeBase& owner);

  private:
    AnalysisContext& m_actx;
    NameResolutionPass& m_core;
};

} // namespace trust
