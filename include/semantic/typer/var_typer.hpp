#pragma once

// include/semantic/typer/var_typer.hpp
// Типизатор объявлений переменных и лямбд: inferred/explicit/forward формы, with-биндинги,
// коэрция инициализатора к целевой аннотации. Выделен из ExprTyper как отдельная зона
// ответственности.

#include "semantic/typer/typer_base.hpp"

namespace trust {

class ContainerTyper;

class VarTyper : public TyperBase {
  public:
    VarTyper(AnalysisContext& actx, NameResolutionPass& core, ContainerTyper& container)
    : TyperBase(actx, core)
    , m_container(container) {}

    void typeVarDeclNode(VarDecl& v);
    void typeLambdaNode(FuncDecl& f);

  private:
    void typeInferredVarDecl(VarDecl& v);
    void typeExplicitVarDecl(VarDecl& v);
    void typeForwardVarDecl(VarDecl& v);
    void checkWithBindingCapture(const VarDecl& v);
    /// Неполнота (forward) при ОПУЩЕННОЙ аннотации: полнота проверяется по
    /// объявлению callee (возвращаемый тип) — клиент не вправе получить неполный тип по значению
    /// и через вывод (`h := make();`).
    void requireInferredCompleteType(const VarDecl& v, QualType state) const;

    ContainerTyper& m_container;
};

} // namespace trust
