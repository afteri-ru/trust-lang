#pragma once

// include/semantic/typer/binary_typer.hpp
// Типизатор бинарных узлов (арифметика/сравнения/присваивания/swap, append, ++/--,
// операторы сравнения типов). Выделен из ExprTyper как самостоятельная зона ответственности.

#include "types/type_read.hpp"
#include "semantic/typer/typer_base.hpp"

namespace trust {

class Binary;
class TypeSystem;

class BinaryTyper : public TyperBase {
  public:
    BinaryTyper(AnalysisContext& actx, NameResolutionPass& core)
    : TyperBase(actx, core) {}

    void typeAppendStmt(Binary& b);
    void typeBinaryNode(Binary& b);

  private:
    /// Унарный минус (`-x`): продвижение типа операнда + флаг переполнения.
    TypeId typeUnaryMinus(Binary& b);
    /// `++`/`--` (унарные): пользовательский operator++/-- либо numeric-проверки и ±1 overflow.
    TypeId typeIncDec(Binary& b);
    /// Swap `a :=: b` / `var :=: _`: проверка совместимости и тип результата (lt).
    TypeId typeSwap(Binary& b, TypeId lt, TypeId rt, const TypeReadModel& reg);
    TypeId typeBinaryResult(Binary& b);
    /// Сравнение с объявленным оператором для record/native class (без Bool-fallback):
    /// разрешение - в semantic::resolveUserOperator, здесь только применение к узлу.
    TypeId typeUserComparison(Binary& b, TypeId lt, TypeId rt);
    /// Тензорная арифметика/присваивание; вызывается только при тензорном операнде.
    TypeId typeTensorBinary(Binary& b, TypeId lt, TypeId rt, const TypeReadModel& reg);
    TypeId typeCheckBinaryResult(Binary& b, TypeId lt, TypeId rt);
    void checkDoubleCapture(const Binary& b);
};

} // namespace trust
