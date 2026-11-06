#include "attrs/attr_pool.hpp"
#include "attrs/attr_builtin.hpp"

namespace trust {

void AttrPool::registerBuiltinAttrs(AttrPool& pool) {
    // analyzer=true -> семантика, codegen=true -> транспилятор; оба сняты -> -Wunhandled-attr.

    // pure/send/sync/thread регистрируются без потребителя: явное использование -> -Wunhandled-attr.
    pool.register_builtin_attr(attr::Pure);
    pool.register_builtin_attr(attr::Send);
    pool.register_builtin_attr(attr::Sync);
    pool.register_builtin_attr(attr::Thread);
    pool.register_builtin_attr(attr::ReadOnly, {}, /*analyzer=*/true, /*codegen=*/true);
    pool.register_builtin_attr(attr::NoExcept, {}, /*codegen=*/true);
    // Пустой дефолт-параметр = wildcard (любое значение), см. Attr::matches_params.
    pool.register_builtin_attr(attr::StackCheck, {std::string_view{}}, /*analyzer=*/true, /*codegen=*/true);
    pool.register_builtin_attr(attr::FuncConst, {}, /*codegen=*/true);
    pool.register_builtin_attr(attr::FuncPure, {}, /*codegen=*/true);
    pool.register_builtin_attr(attr::FuncConstexpr, {}, /*codegen=*/true);
    pool.register_builtin_attr(attr::ThreadLocal, {}, /*analyzer=*/true, /*codegen=*/true);

    pool.register_builtin_attr(attr::Coroutine, {}, /*analyzer=*/true, /*codegen=*/false);
    pool.register_builtin_attr(attr::Link, {std::string_view{}}, /*codegen=*/true);
    pool.register_builtin_attr(attr::Include, {std::string_view{}}, /*analyzer=*/true, /*codegen=*/true);
    pool.register_builtin_attr(attr::Export, {std::string_view{}}, /*analyzer=*/true, /*codegen=*/true);
    pool.register_builtin_attr(attr::Matcher, {std::string_view{}}, /*analyzer=*/true, /*codegen=*/true);
    pool.register_builtin_attr(attr::Reftype, {std::string_view{}, std::string_view{}, std::string_view{}},
                               /*analyzer=*/true, /*codegen=*/true);
    pool.register_builtin_attr(attr::Deleter, {std::string_view{}}, /*analyzer=*/true, /*codegen=*/true);
    pool.register_builtin_attr(attr::Borrowed, {}, /*analyzer=*/true, /*codegen=*/false);
    pool.register_builtin_attr(attr::Pin, {}, /*analyzer=*/true, /*codegen=*/false);
    pool.register_builtin_attr(attr::Lifetime, {std::string_view{}, std::string_view{}}, /*analyzer=*/true, /*codegen=*/false);
    pool.register_builtin_attr(attr::Format, {attr::FormatPrintf, std::string_view{}, std::string_view{}}, /*analyzer=*/true);
    pool.register_builtin_attr(attr::Virtual, {}, /*analyzer=*/true, /*codegen=*/false);
    pool.register_builtin_attr(attr::Override, {}, /*analyzer=*/true, /*codegen=*/false);
}

} // namespace trust
