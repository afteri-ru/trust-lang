#pragma once

// include/semantic/typer/method_call_typer.hpp
// Типизатор вызова метода `obj.method(args)`: поиск набора сигнатур метода (включая
// Range/Array/Tensor-подстановки), выбор перегрузки, привязка аргументов и многоточие.
// Выделен из AccessResolver: доступ и типизация результата вызова — разные зоны ответственности.

#include "semantic/typer/typer_base.hpp"

namespace trust {

class MethodCallTyper : public TyperBase {
  public:
    MethodCallTyper(AnalysisContext& actx, NameResolutionPass& core)
    : TyperBase(actx, core) {}

    void typeMethodCall(Binary& n);
};

} // namespace trust
