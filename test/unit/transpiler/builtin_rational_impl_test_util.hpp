#ifndef BUILTIN_RATIONAL_IMPL_TEST_UTIL_HPP
#define BUILTIN_RATIONAL_IMPL_TEST_UTIL_HPP
// test/unit/transpiler/builtin_rational_impl_test_util.hpp
// Реализация Rational/BigInteger для юнит-тестов кодогенерации без загрузчика stdlib.
#include "support/cpp_facts_test_util.hpp"

namespace trust {

/// Реализация Rational/BigInteger для юнит-тестов без загрузчика stdlib.
/// В реальной сборке — stdlib/builtin/rational.src (`Rational = %trust::Rational`, GMP).
inline void seedRationalRuntimeImpl(Context& ctx) {
    if (!test::builtinImplemented(ctx.readTypes(), ctx.target(), "Rational")) {
        test::defineBuiltinImpl(ctx.readTypes(), ctx.target(), "Rational", "trust::Rational", {"@stdlib/builtin/rational.hpp"}, {"gmp"});
    }
    if (!test::builtinImplemented(ctx.readTypes(), ctx.target(), "BigInteger")) {
        test::defineBuiltinImpl(ctx.readTypes(), ctx.target(), "BigInteger", "trust::BigInteger", {"@stdlib/builtin/big_integer.hpp"}, {"gmp"});
    }
}

} // namespace trust
#endif // BUILTIN_RATIONAL_IMPL_TEST_UTIL_HPP