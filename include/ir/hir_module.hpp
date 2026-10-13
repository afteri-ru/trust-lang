#pragma once

// include/ir/hir_module.hpp
// HIR (high-level IR) — производная от AST структурная форма, которую читает кодогенерация.
//
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
#include "utils/error.hpp"

#include <cstddef>
#include <utility>
#include <vector>

namespace trust {

class Context;

class HirModule {
  public:
    explicit HirModule(std::vector<AstNodePtr> nodes)
    : m_nodes(std::move(nodes)) {
    }

    [[nodiscard]] const std::vector<AstNodePtr>& nodes() const noexcept { return m_nodes; }
    [[nodiscard]] std::size_t size() const noexcept { return m_nodes.size(); }

    /// Хвост начиная с `first` (тот же владелец узлов: `shared_ptr` разделяются, дерево не
    /// копируется). Нужен, чтобы отсечь prelude-часть при выделении HIR главного модуля.
    [[nodiscard]] HirModule tailFrom(std::size_t first) const {
        EXPECT(first <= m_nodes.size() && "HirModule::tailFrom: index is out of range");
        return HirModule(std::vector<AstNodePtr>(m_nodes.begin() + static_cast<std::ptrdiff_t>(first), m_nodes.end()));
    }

  private:
    std::vector<AstNodePtr> m_nodes;
};

/// Построить HIR по неизменяемой AST: глубокий клон + lowering на клоне.
/// Исходная AST не изменяется; диагностики lowering выдаются в общий контекст.
[[nodiscard]] HirModule buildHir(const std::vector<AstNodePtr>& astNodes, Context& ctx);

} // namespace trust
