#pragma once

// include/semantic/stack_check_infer.hpp
// Опциональный анализатор (InlineAnalysisHook) контроля переполнения стека для рекурсивных
// функций (режимы --stack-check=recursion|auto). Строит граф вызовов по разрешённым CallExpr
// (текущая функция -> callee) и на этапе finalize (после полного обхода) находит рекурсивные
// функции (прямо/взаимно, по достижимости функции из самой себя):
//   - режим recursion: диагностика -Wstack-check-infer на незащищённые рекурсивные функции
//     (пользователь сам решает, пометить @[stack_check@] или нет);
//   - режим auto: авто-добавление атрибута stack_check (manual=false) на рекурсивные функции,
//     чтобы транспилятор вставил проверку перед каждым вызовом (через ту же точку резолва callee).
// В режимах off/explicit анализатор не подключается (см. SemanticPassRunner).

#include "semantic/inline_hook.hpp"
#include "semantic/pass.hpp"
#include "semantic/symbol_table.hpp"
#include "semantic/diag.hpp"
#include "ast/ast_nodes.hpp"

#include <map>
#include <set>

namespace trust {

class StackCheckInferHook : public InlineAnalysisHook {
  public:
    explicit StackCheckInferHook(AnalysisContext& actx);

    std::optional<semantic::FlagKind> gateFlag() const override { return semantic::FlagKind::StackCheck; }

    bool onNode(AstNodePtr& node) override;
    void finalize() override;

  private:
    AnalysisContext& m_actx;
    /// Граф вызовов: вызывающая функция -> вызываемые (разрешённые) функции.
    std::map<const FuncDecl*, std::set<const FuncDecl*>> m_calls;
    /// Диапазон объявления функции (для диагностики/авто-маркировки).
    std::map<const FuncDecl*, MapperRange> m_ranges;

    /// Достижима ли функция из самой себя по графу m_calls (прямая/взаимная рекурсия).
    bool isRecursive(const FuncDecl* f) const;
};

} // namespace trust
