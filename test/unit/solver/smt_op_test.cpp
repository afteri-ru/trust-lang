#include "solver/smt_op.hpp"
#include "solver/smt_ast.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <optional>
#include <string_view>

using namespace trust::solver;

// Единый источник имён операторов - X-макрос SOLVER_OPERATOR_LIST: enum SmtOp + smtOpName/parseSmtOp.
TEST(SmtOp, NameKnown) {
    EXPECT_EQ(smtOpName(SmtOp::BvAdd), "bvadd");
    EXPECT_EQ(smtOpName(SmtOp::BvSub), "bvsub");
    EXPECT_EQ(smtOpName(SmtOp::BvMul), "bvmul");
    EXPECT_EQ(smtOpName(SmtOp::BvSdiv), "bvsdiv");
    EXPECT_EQ(smtOpName(SmtOp::BvSrem), "bvsrem");
    EXPECT_EQ(smtOpName(SmtOp::BvNeg), "bvneg");
    EXPECT_EQ(smtOpName(SmtOp::BvSgt), "bvsgt");
    EXPECT_EQ(smtOpName(SmtOp::BvSge), "bvsge");
    EXPECT_EQ(smtOpName(SmtOp::BvSlt), "bvslt");
    EXPECT_EQ(smtOpName(SmtOp::BvSle), "bvsle");
    EXPECT_EQ(smtOpName(SmtOp::And), "and");
    EXPECT_EQ(smtOpName(SmtOp::Or), "or");
    EXPECT_EQ(smtOpName(SmtOp::Xor), "xor");
    EXPECT_EQ(smtOpName(SmtOp::Implies), "=>");
    EXPECT_EQ(smtOpName(SmtOp::Not), "not");
    EXPECT_EQ(smtOpName(SmtOp::Eq), "=");
}

// Обратный разбор совпадает для каждого оператора (round-trip).
TEST(SmtOp, ParseRoundTrip) {
    for (std::size_t i = 0; i < kSmtOpCount; ++i) {
        const SmtOp op = static_cast<SmtOp>(i);
        EXPECT_EQ(parseSmtOp(smtOpName(op)), op) << "for index " << i;
    }
}

// Неизвестные имена (в т.ч. пользовательские функции) - nullopt.
TEST(SmtOp, ParseUnknown) {
    EXPECT_EQ(parseSmtOp("bogus"), std::nullopt);
    EXPECT_EQ(parseSmtOp("add"), std::nullopt); // пользовательская функция
    EXPECT_EQ(parseSmtOp("+"), std::nullopt);   // не-BV арифметика - не из списка
    EXPECT_EQ(parseSmtOp(""), std::nullopt);
}

// SmtTerm::op по умолчанию - nullopt (терм без встроенного оператора).
TEST(SmtOp, TermDefaultOpIsNullopt) {
    SmtTerm t;
    EXPECT_FALSE(t.op.has_value());
    EXPECT_EQ(t.kind, SmtTermKind::kConst);
    EXPECT_EQ(t.fun_name, "");
}

// static_assert полноты enum (см. smt_op.hpp) гарантирует совпадение списков.
TEST(SmtOp, EnumCountMatchesNames) {
    EXPECT_EQ(static_cast<std::size_t>(SmtOp::kCount), kSmtOpCount);
}
