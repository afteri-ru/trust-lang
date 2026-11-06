#ifndef BUILTIN_MEM_IMPL_TEST_UTIL_HPP
#define BUILTIN_MEM_IMPL_TEST_UTIL_HPP
// test/unit/transpiler/builtin_mem_impl_test_util.hpp
// Реализации типов памяти/ссылок (политики доступа, AccessShared, StaticUnique, deleter'ы)
// для юнит-тестов кодогенерации без загрузчика stdlib.
#include "support/cpp_facts_test_util.hpp"

namespace trust {

/// Реализации типов модуля `mem` для юнит-тестов без загрузчика stdlib.
/// В реальной сборке — stdlib/core/mem.src (`AccessMutex = %trust::AccessMutex`, ...).
inline void seedMemRuntimeImpl(Context& ctx) {
    const auto defineOnce = [&ctx](std::string_view name, std::string_view cpp, std::string_view hdr) {
        if (!test::builtinImplemented(ctx.readTypes(), ctx.target(), name)) {
            test::defineBuiltinImpl(ctx.readTypes(), ctx.target(), name, std::string(cpp), {std::string(hdr)}, {});
        }
    };
    defineOnce("AccessMutex", "trust::AccessMutex", "@stdlib/trusted-cpp-sync.hpp");
    defineOnce("AccessRwMutex", "trust::AccessRwMutex", "@stdlib/trusted-cpp-sync.hpp");
    defineOnce("AccessSingleThread", "trust::AccessSingleThread", "@stdlib/trusted-cpp-sync.hpp");
    defineOnce("AccessShared", "trust::AccessShared", "@stdlib/trusted-cpp-sync.hpp");
    defineOnce("StaticUnique", "trust::StaticUnique", "@stdlib/trusted-cpp.hpp");
    defineOnce("FreeDeleter", "trust::FreeDeleter", "@stdlib/resource.hpp");
    defineOnce("FileDeleter", "trust::FileDeleter", "@stdlib/resource.hpp");
}

} // namespace trust
#endif // BUILTIN_MEM_IMPL_TEST_UTIL_HPP
