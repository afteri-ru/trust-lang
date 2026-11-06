#pragma once

// HIR (high-level IR) — производная от AST структурная форма, которую читает кодогенерация.
// Контракт:
//   * HIR строится ОДИН раз как НОВОЕ дерево (`buildHir`): клон AST + lowering на клоне.
//     Исходная AST не изменяется и остаётся доступной для диагностик/source-map/solver.
//   * После построения HIR не мутируется структурно (`nodes()` — только чтение); это позволяет
//     перезапускать анализы по AST и держать один авторитетный вход для кодогенерации.
//   * HIR — ЕДИНСТВЕННЫЙ вход бэкендов: публичные API кодогенерации принимают `HirModule`,
//     а не `std::vector<AstNodePtr>` (AST и HIR не взаимозаменяемы на границе слоёв).
//   * Форма HIR — структурная (не SSA): `if/while/match` остаются структурированными узлами,
//     но весь «сахар» уже развёрнут (semicolon-обёртки, метки/goto, временные анализатора).

#include "ast/token_base.hpp"

#include <cstddef>
#include <utility>
#include <vector>

namespace trust {

class Context;
class TypeSystem;

class HirModule {
  public:
    explicit HirModule(std::vector<AstNodePtr> nodes)
    : m_nodes(std::move(nodes)) {
    }

    [[nodiscard]] const std::vector<AstNodePtr>& nodes() const noexcept { return m_nodes; }
    [[nodiscard]] std::size_t size() const noexcept { return m_nodes.size(); }

  private:
    std::vector<AstNodePtr> m_nodes;
};

/// Построить HIR по неизменяемой AST: глубокий клон + lowering на клоне.
/// Исходная AST не изменяется; диагностики lowering выдаются в общий контекст.
[[nodiscard]] HirModule buildHir(const std::vector<AstNodePtr>& astNodes, Context& ctx, TypeSystem& registry);

} // namespace trust
