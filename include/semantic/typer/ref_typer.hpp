#pragma once

// include/semantic/typer/ref_typer.hpp
// Типизатор ссылочных выражений: `& expr` (borrow), `*ref`/`*^ref` (take),
// нативные `%&`/`%*`. Выделен из ExprTyper как отдельная зона ответственности.

#include "semantic/typer/typer_base.hpp"

namespace trust {

class RefTyper : public TyperBase {
  public:
    RefTyper(AnalysisContext& actx, NameResolutionPass& core)
    : TyperBase(actx, core) {}

    void typeRefExpr(AstNodeBase* node);
};

} // namespace trust
