#ifndef BUILTIN_ANY_IMPL_TEST_UTIL_HPP
#define BUILTIN_ANY_IMPL_TEST_UTIL_HPP
// test/unit/transpiler/builtin_any_impl_test_util.hpp
// Реализация встроенного Any (std::any, RTTI) для юнит-тестов кодогенерации без загрузчика
// stdlib. В реальной сборке — stdlib/builtin/any.src (`Any = %std::any { };`).
#include "support/cpp_facts_test_util.hpp"

namespace trust {

inline void seedAnyRuntimeImpl(Context& ctx) {
    if (!test::builtinImplemented(ctx.readTypes(), ctx.target(), "Any")) {
        test::defineBuiltinImpl(ctx.readTypes(), ctx.target(), "Any", "std::any", {"#include <any>"}, {});
    }
}

} // namespace trust
#endif // BUILTIN_ANY_IMPL_TEST_UTIL_HPP
