// attr_builtin.cpp - AttrPool::registerBuiltinAttrs implementation

#include "ast/attr_pool.hpp"
#include "ast/attr_builtin.hpp"

namespace trust {

void AttrPool::registerBuiltinAttrs(AttrPool& pool) {
    using namespace attr;

    // Attributes without required parameters
    pool.register_builtin_attr(Pure);
    pool.register_builtin_attr(Send);
    pool.register_builtin_attr(Sync);
    pool.register_builtin_attr(Thread);
    pool.register_builtin_attr(ReadOnly);
    pool.register_builtin_attr(NoExcept);
    pool.register_builtin_attr(StackGuard);
    // Function qualifiers / storage class attributes
    pool.register_builtin_attr(FuncConst);
    pool.register_builtin_attr(FuncPure);
    pool.register_builtin_attr(FuncConstexpr);
    pool.register_builtin_attr(ThreadLocal);

    // @trust has one required string parameter (the trusted assertion)
    pool.register_builtin_attr(Trust, {std::string_view{}});

    // @require / @ensure take a string parameter (code block or expression)
    pool.register_builtin_attr(Require, {std::string_view{}});
    pool.register_builtin_attr(Ensure, {std::string_view{}});

    // @depend_macro takes zero or more string/identifier arguments (variadic)
    // Register with one default param to indicate it takes params
    pool.register_builtin_attr(DependMacro, {std::string_view{}});

    // @link("libname") - имя линкуемой библиотеки для нативной декларации.
    // Один строковый параметр; пустой дефолт - wildcard (принимает любое имя).
    pool.register_builtin_attr(Link, {std::string_view{}});

    // @reftype("ptr") - вид ссылки (плоский RefType). Один строковый параметр -
    // мнемоническое имя вида; пустой дефолт - wildcard.
    pool.register_builtin_attr(Reftype, {std::string_view{}});

    // @format("printf", string_index, first_to_check) - компиляйт-тайм проверка аргументов
    // на соответствие форматной строке printf (GCC-аналог). Первый параметр - архетип
    // формата ("printf"); два индекса - пустые wildcard (принимают любые 1-based значения).
    pool.register_builtin_attr(Format, {"printf", std::string_view{}, std::string_view{}});
}

} // namespace trust