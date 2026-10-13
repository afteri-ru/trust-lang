#pragma once

// include/analysis/runtime_checks.hpp
// ЕДИНЫЙ источник классификации проверок безопасности операций: один предикат «какая проверка
// нужна узлу» для кодогена и верификатора. Политика (флаги вроде `-foverflow-check`) остаётся у
// потребителя; классификация опирается на ФАКТЫ семантики (`Binary::m_overflowCheck`,
// `RefTakeExpr::m_lowering`), а не на повторный вывод по типам/скоупам.
//
// Область ответственности — только проверки, которые реально выполняет рантайм-код:
//   * переполнение знаковой машинной арифметики (`+ - *`, `+= -= *=`);
//   * деление/остаток целых на ноль (и `INT_MIN / -1`);
//   * разыменование `ptr`/`unique` (проверка nullptr).
// Проверки, которых у кодогена пока нет (границы индексов, сужение), классификатор НЕ выдумывает:
// они появятся вместе с обязательствами (см. backlog `solver-verification-completion.md`).

#include "ast/ast_nodes.hpp"
#include "ast/binary_op.hpp"
#include "ast/ast_nodes.hpp"

#include <cstdint>

namespace trust {

/// Вид проверки безопасности, требуемой узлом.
enum class RuntimeCheckKind : std::uint8_t {
    kNone = 0,
    kIntOverflow, ///< знаковое `+ - *` / `+= -= *=`: результат вне диапазона типа приёмника
    kDivZero,     ///< целочисленное `//`/`//=`/`%`/`%=`: нулевой делитель (и `INT_MIN / -1`)
    kDerefNull,   ///< `*ptr` / `*unique`: указатель равен null
};

/// Классификация проверки для узла. Значение `kNone` — узел не порождает рантайм-проверку.
[[nodiscard]] inline RuntimeCheckKind runtimeCheckOf(const AstNodeBase& node) noexcept {
    if (node.is<RefTakeExpr>()) {
        const auto& take = static_cast<const RefTakeExpr&>(node);
        switch (take.m_lowering) {
        case RefTakeExpr::TakeLowering::Direct:
        case RefTakeExpr::TakeLowering::UniqueDirect:
            return RuntimeCheckKind::kDerefNull; // `trust::checked_deref(...)`
        case RefTakeExpr::TakeLowering::StaticUnique:
        case RefTakeExpr::TakeLowering::UserOperator:
            return RuntimeCheckKind::kNone;
        }
        return RuntimeCheckKind::kNone;
    }
    if (!node.is<Binary>()) {
        return RuntimeCheckKind::kNone;
    }
    const auto& binary = static_cast<const Binary&>(node);
    const ParserToken::Kind kind = binary.kind();
    if (kind != ParserToken::Kind::MathOp && kind != ParserToken::Kind::AssignOp) {
        return RuntimeCheckKind::kNone; // сравнения/логика/индексация/поля проверок не несут
    }
    if (isOverflowCheckableOp(binary.m_op) && binary.m_overflowCheck.value_or(false)) {
        return RuntimeCheckKind::kIntOverflow;
    }
    switch (binary.m_op) {
    case BinaryOp::IntDiv:
    case BinaryOp::IntDivAssign:
    case BinaryOp::Mod:
    case BinaryOp::ModAssign:
        return RuntimeCheckKind::kDivZero;
    default:
        return RuntimeCheckKind::kNone;
    }
}

} // namespace trust
