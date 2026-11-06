#ifndef BUILTIN_STRING_IMPL_TEST_UTIL_HPP
#define BUILTIN_STRING_IMPL_TEST_UTIL_HPP
// test/unit/transpiler/builtin_string_impl_test_util.hpp
// Реализация StrChar/StrWide для юнит-тестов кодогенерации без загрузчика stdlib.
//
// По модели встроенных типов интерфейс StrChar/StrWide (группа, методы) предобъявлен в ядре,
// а C++-класс ГЕНЕРИРУЕТСЯ из провайдера stdlib/builtin/string.src (базис `%std::string` в синтаксисе объявления).
// Юнит-тесты транслируют без загрузчика stdlib, поэтому сеют эквивалентную реализацию (C++-имя
// `std::string`/`std::wstring` + `<string>`) напрямую в BuiltinRegistry.
#include "support/cpp_facts_test_util.hpp"

namespace trust {

inline void seedStringRuntimeImpl(Context& ctx) {
    if (!test::builtinImplemented(ctx.readTypes(), ctx.target(), "StrChar")) {
        test::defineBuiltinImpl(ctx.readTypes(), ctx.target(), "StrChar", "std::string", {"#include <string>"});
    }
    if (!test::builtinImplemented(ctx.readTypes(), ctx.target(), "StrWide")) {
        test::defineBuiltinImpl(ctx.readTypes(), ctx.target(), "StrWide", "std::wstring", {"#include <string>"});
    }
}

} // namespace trust
#endif // BUILTIN_STRING_IMPL_TEST_UTIL_HPP