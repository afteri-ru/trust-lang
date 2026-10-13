#pragma once

// include/semantic/ref/definite_assignment.hpp
// Definite assignment как ПОСТ-проход по MIR (CFG + dataflow).
//
// Запускается `Pipeline` после семантики и buildHir/buildMir: сахар lowering'а
// (SemicolonStmt, goto-метки, временные анализатора) уже материализован. Места (places) -
// узлы объявлений HIR (VarDecl/ArgNode/цель DestructureDecl), а не Symbol*: SymbolTable после
// обхода пуста, а resolvedDecl на ссылках переживает pop скоупов.
//
// Анализ intra-procedural: каждая функция/лямбда - отдельный CFG в `MirModule`. Глобалы/статические
// переменные не отслеживаются (инициализация вне функции — вне DA). Capture-by-value лямбды —
// чтение внешней переменной в точке создания лямбды; в теле лямбды захват считается
// инициализированным. VarDecl считается инициализированным ДО чтения своего инициализатора
// (поддержка shadowing self-reference `s := s + 1`). Недостижимые блоки не анализируются.

#include "ir/analysis_manager.hpp"

#include <span>
#include <string_view>
#include <vector>

namespace trust {

class Context;

namespace semantic {

class DefiniteAssignmentPass : public flow::Analysis {
  public:
    explicit DefiniteAssignmentPass(Context& ctx)
    : m_ctx(ctx) {}

    [[nodiscard]] std::string_view name() const override { return "definite-assignment"; }
    [[nodiscard]] std::span<const std::string_view> dependencies() const override { return {}; }

    /// Анализирует все функции/лямбды в MIR. Диагностики:
    ///  - read-before-init (Error, в точке чтения);
    ///  - cannot infer type для untyped `x := _`, если не было read-before-init.
    void run(const flow::MirModule& mir) override;

  private:
    Context& m_ctx;
};

} // namespace semantic
} // namespace trust
