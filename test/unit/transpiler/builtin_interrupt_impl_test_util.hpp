#ifndef BUILTIN_INTERRUPT_IMPL_TEST_UTIL_HPP
#define BUILTIN_INTERRUPT_IMPL_TEST_UTIL_HPP
// test/unit/transpiler/builtin_interrupt_impl_test_util.hpp
// Реализации типов прерываний (IntAny/IntPlus/IntMinus) для юнит-тестов кодогенерации
// без загрузчика stdlib.
#include "support/cpp_facts_test_util.hpp"

namespace trust {

/// Реализации типов модуля `interrupt` для юнит-тестов без загрузчика stdlib.
/// В реальной сборке — stdlib/core/interrupt.src (`IntMinus = %trust::IntMinus`, ...).
inline void seedInterruptRuntimeImpl(Context& ctx) {
    const auto defineOnce = [&ctx](std::string_view name, std::string_view cpp) {
        if (!test::builtinImplemented(ctx.readTypes(), ctx.target(), name)) {
            test::defineBuiltinImpl(ctx.readTypes(), ctx.target(), name, std::string(cpp), {"@stdlib/interrupts.hpp"}, {});
        }
    };
    defineOnce("IntAny", "trust::IntAny");
    defineOnce("IntPlus", "trust::IntPlus");
    defineOnce("IntMinus", "trust::IntMinus");
}

} // namespace trust
#endif // BUILTIN_INTERRUPT_IMPL_TEST_UTIL_HPP
