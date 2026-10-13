#pragma once

// include/semantic/ref/borrow_flow.hpp
// Flow-sensitive анализ ИНВАЛИДАЦИИ производных представлений: CFG + dataflow по
// стабильным местам.
//
// Модель — НЕ заём Rust: контролируется инвалидация производного представления
// (weak-наблюдатель из `shared`, `@[borrowed]`-переменная/результат метода) при
// мутации/перемещении ИСТОЧНИКА. Спецификация — include/semantic/ref/BORROW_CHECKER.md.
//
// Свойства:
//   * место = узел объявления (PlaceId), одноимённые переменные не смешиваются;
//   * живость per-path (may-анализ), освобождение при перепривязке/`= _`/выходе из скоупа
//     на ВСЕХ путях (CFG-`ScopeExit`), учёт циклов и ветвлений;
//   * мутация источника диагностируется один раз в точке мутации: для weak — только если
//     представление используется позже на некотором пути (backward liveness), для
//     @[borrowed] — по контракту (в точке мутации);
//   * escape: возврат/сохранение в non-view место @[borrowed]-представления; вызов не-const
//     метода на объекте-источнике трактуется как мутация (параметры — по значению);
//   * проверка согласованности `@[lifetime]` с выведенным регионом.
//
// Запускается `Pipeline` после семантики: DA, затем borrow (CFG строится по MIR: сахар lowering'а
// уже развёрнут). Анализ не строит CFG сам — получает готовый `MirModule`.

#include "ir/analysis_manager.hpp"

#include <span>
#include <string_view>
#include <vector>

namespace trust {

class Context;

namespace semantic {

class BorrowFlowPass : public flow::Analysis {
  public:
    explicit BorrowFlowPass(Context& ctx)
    : m_ctx(ctx) {}

    [[nodiscard]] std::string_view name() const override { return "borrow"; }
    [[nodiscard]] std::span<const std::string_view> dependencies() const override {
        static constexpr std::string_view kDeps[] = {"definite-assignment"};
        return kDeps;
    }

    void run(const flow::MirModule& mir) override;

  private:
    Context& m_ctx;
};

} // namespace semantic
} // namespace trust
