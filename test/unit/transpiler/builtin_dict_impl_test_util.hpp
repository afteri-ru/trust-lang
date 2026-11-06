#ifndef BUILTIN_DICT_IMPL_TEST_UTIL_HPP
#define BUILTIN_DICT_IMPL_TEST_UTIL_HPP
// test/unit/transpiler/builtin_dict_impl_test_util.hpp
// Реализация Dict для юнит-тестов кодогенерации без загрузчика stdlib.
#include "support/cpp_facts_test_util.hpp"

namespace trust {

inline void seedDictRuntimeImpl(Context& ctx) {
    if (!test::builtinImplemented(ctx.readTypes(), ctx.target(), "Dict")) {
        test::defineBuiltinImpl(ctx.readTypes(), ctx.target(), "Dict", "trust::Dict", {"@stdlib/builtin/dict.hpp"}, {});
    }
}

} // namespace trust
#endif // BUILTIN_DICT_IMPL_TEST_UTIL_HPP