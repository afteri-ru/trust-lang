#pragma once

// Анализатор объявлений переменных: аннотация/вывод типа, инициализатор, атрибуты ref-вида,
// trust-условия и регистрация символа. Выделен из DeclAnalyzer.

#include "semantic/core/pass.hpp"

namespace trust {

class NameResolutionPass;

class VarDeclAnalyzer {
  public:
    VarDeclAnalyzer(AnalysisContext& actx, NameResolutionPass& core)
    : m_actx(actx)
    , m_core(core) {
    }

    void analyzeVarDecl(VarDecl& var_node);

  private:
    AnalysisContext& m_actx;
    NameResolutionPass& m_core;
};

} // namespace trust
