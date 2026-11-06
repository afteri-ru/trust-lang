#ifndef BUILTIN_CONTAINER_IMPL_TEST_UTIL_HPP
#define BUILTIN_CONTAINER_IMPL_TEST_UTIL_HPP
// test/unit/transpiler/builtin_container_impl_test_util.hpp
// Реализации Array/Range для юнит-тестов кодогенерации без загрузчика stdlib.
#include "support/cpp_facts_test_util.hpp"

namespace trust {

inline void seedArrayRuntimeImpl(Context& ctx) {
    if (!test::builtinImplemented(ctx.readTypes(), ctx.target(), "Array")) {
        test::defineBuiltinImpl(ctx.readTypes(), ctx.target(), "Array", "std::vector", {"@stdlib/builtin/array.hpp"}, {});
    }
}

inline void seedRangeRuntimeImpl(Context& ctx) {
    if (!test::builtinImplemented(ctx.readTypes(), ctx.target(), "Range")) {
        test::defineBuiltinImpl(ctx.readTypes(), ctx.target(), "Range", "trust::Range", {"@stdlib/builtin/range.hpp"}, {});
    }
}

} // namespace trust
#endif // BUILTIN_CONTAINER_IMPL_TEST_UTIL_HPP