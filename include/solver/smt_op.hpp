#pragma once

// include/solver/smt_op.hpp
// Встроенные операторы SMT-LIB 2 (BitVec-арифметика/сравнения, логика, равенство).
// Единый источник: один список порождает enum SmtOp, имя (smtOpName) и обратный разбор
// (parseSmtOp) - по образцу X-макросов SOLVER_MODE_LIST (semantic/solver.hpp) и diag_set.hpp.
// Пользовательские функции (произвольные имена, не из списка) хранятся в SmtTerm::fun_name,
// у них SmtTerm::op = nullopt.

#include <cstddef>
#include <optional>
#include <string_view>

// -- Единый список операторов: имя (enum) + SMT-LIB 2 имя (как в печати/диспетчере) --
#define SOLVER_OPERATOR_LIST(M) \
    M(BvAdd, "bvadd")           \
    M(BvSub, "bvsub")           \
    M(BvMul, "bvmul")           \
    M(BvSdiv, "bvsdiv")         \
    M(BvSrem, "bvsrem")         \
    M(BvNeg, "bvneg")           \
    M(BvSgt, "bvsgt")           \
    M(BvSge, "bvsge")           \
    M(BvSlt, "bvslt")           \
    M(BvSle, "bvsle")           \
    M(BvUdiv, "bvudiv")         \
    M(BvUrem, "bvurem")         \
    M(BvUgt, "bvugt")           \
    M(BvUge, "bvuge")           \
    M(BvUlt, "bvult")           \
    M(BvUle, "bvule")           \
    M(BvAnd, "bvand")           \
    M(BvOr, "bvor")             \
    M(BvXor, "bvxor")           \
    M(BvNot, "bvnot")           \
    M(BvShl, "bvshl")           \
    M(BvLshr, "bvlshr")         \
    M(BvAshr, "bvashr")         \
    M(And, "and")               \
    M(Or, "or")                 \
    M(Xor, "xor")               \
    M(Implies, "=>")            \
    M(Not, "not")               \
    M(Eq, "=")                  \
    M(Ite, "ite")               \
    M(SignExt, "sign_extend")   \
    M(ZeroExt, "zero_extend")   \
    M(Select, "select")         \
    M(Store, "store")

namespace trust {
namespace solver {

#define SOLVER_OP_ENUM(name, smt) name,
enum class SmtOp { SOLVER_OPERATOR_LIST(SOLVER_OP_ENUM) kCount };
#undef SOLVER_OP_ENUM

#define SOLVER_OP_NAME(name, smt) smt,
inline constexpr std::string_view kSmtOpNames[] = {SOLVER_OPERATOR_LIST(SOLVER_OP_NAME)};
#undef SOLVER_OP_NAME

inline constexpr std::size_t kSmtOpCount = sizeof(kSmtOpNames) / sizeof(kSmtOpNames[0]);

// Компайлтайм-проверка полноты: по одному имени на каждый enumerator.
static_assert(static_cast<std::size_t>(SmtOp::kCount) == kSmtOpCount, "SOLVER_OPERATOR_LIST and kSmtOpNames must stay in sync");

/// Имя оператора (для диагностик/печати). Известное значение обязательно.
[[nodiscard]] inline constexpr std::string_view smtOpName(SmtOp op) noexcept {
    const int idx = static_cast<int>(op);
    return (idx >= 0 && idx < static_cast<int>(kSmtOpCount)) ? kSmtOpNames[idx] : "unknown";
}

/// Разбор строкового имени оператора. Неизвестное имя (в т.ч. пользовательская функция) - nullopt.
[[nodiscard]] inline constexpr std::optional<SmtOp> parseSmtOp(std::string_view v) noexcept {
    for (std::size_t i = 0; i < kSmtOpCount; ++i) {
        if (kSmtOpNames[i] == v) {
            return static_cast<SmtOp>(i);
        }
    }
    return std::nullopt;
}

} // namespace solver
} // namespace trust

#undef SOLVER_OPERATOR_LIST
