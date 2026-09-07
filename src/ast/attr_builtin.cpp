// attr_builtin.cpp - AttrPool::registerBuiltinAttrs implementation

#include "ast/attr_pool.hpp"
#include "ast/attr_builtin.hpp"

namespace trust {

void AttrPool::registerBuiltinAttrs(AttrPool& pool) {
    using namespace attr;

    // Flags declare which processing stage consumes the attribute (see detail::is_handled):
    //   analyzer=true -> handled by the semantic analyzer; codegen=true -> by the C++ transpiler.
    // An attribute with NEITHER flag is "unhandled" -> -Wunhandled-attr fires on its use.

    // Attributes without required parameters
    // pure/send/sync/thread are registered for compatibility, but currently NO stage
    // consumes them (no analyzer/codegen support) - an explicit use is flagged as unhandled.
    pool.register_builtin_attr(Pure);
    pool.register_builtin_attr(Send);
    pool.register_builtin_attr(Sync);
    pool.register_builtin_attr(Thread);
    pool.register_builtin_attr(ReadOnly, {}, /*analyzer=*/true, /*codegen=*/true);
    pool.register_builtin_attr(NoExcept, {}, /*codegen=*/true);
    // StackCheck: без аргумента - limit; с одним целым аргументом N - явный размер.
    // Пустой дефолт-параметр - wildcard (принимает любое значение); см. Attr::matches_params.
    pool.register_builtin_attr(StackCheck, {std::string_view{}}, /*analyzer=*/true, /*codegen=*/true);
    // Function qualifiers / storage class attributes (consumed by the C++ code generator)
    pool.register_builtin_attr(FuncConst, {}, /*codegen=*/true);
    pool.register_builtin_attr(FuncPure, {}, /*codegen=*/true);
    pool.register_builtin_attr(FuncConstexpr, {}, /*codegen=*/true);
    pool.register_builtin_attr(ThreadLocal, {}, /*analyzer=*/true, /*codegen=*/true);

    // @link("libname") - имя линкуемой библиотеки для нативной декларации.
    // Один строковый параметр; пустой дефолт - wildcard (принимает любое имя).
    // link обрабатывается только кодогенерацией (линковка, type_emit).
    pool.register_builtin_attr(Link, {std::string_view{}}, /*codegen=*/true);

    // @include("header") - зависимый C++-заголовок для нативной декларации/типа.
    // Один строковый параметр (имя/директива заголовка); пустой дефолт - wildcard.
    pool.register_builtin_attr(Include, {std::string_view{}}, /*analyzer=*/true, /*codegen=*/true);

    // @matcher(\"fn\") - переопределяемая функция сравнения в операторе match. Один строковый
    // параметр (имя функции-предиката); пустой дефолт - wildcard. Обрабатывается анализатором
    // (проверка сигнатуры) и кодогенерацией (эмиссия вызова fn(tmp, pattern)).
    pool.register_builtin_attr(Matcher, {std::string_view{}}, /*analyzer=*/true, /*codegen=*/true);

    // @reftype("ptr") / @reftype("shared", <sync_policy>[, <timeout>]) - вид ссылки (плоский RefType)
    // + опционально класс синхронизации доступа и пер-объектный таймаут детектора. Три пустых
    // wildcard-дефолта => параметры опциональны по количеству (1..3), см. Attr::matches_params.
    pool.register_builtin_attr(Reftype, {std::string_view{}, std::string_view{}, std::string_view{}},
                               /*analyzer=*/true, /*codegen=*/true);

    // @reftrace - условный атрибут отслеживания инвалидации ссылок. Без параметров. Ставится на
    // класс/тип или метод; поведение управляется -Wreftrace= (см. attr::RefTrace).
    // RefTrace обрабатывается только анализатором (semantic/nativeref); кодогенерация его не читает.
    pool.register_builtin_attr(RefTrace, {}, /*analyzer=*/true, /*codegen=*/false);

    // @format("printf", string_index, first_to_check) - компиляйт-тайм проверка аргументов
    // на соответствие форматной строке printf (GCC-аналог). Первый параметр - архетип
    // формата ("printf"); два индекса - пустые wildcard (принимают любые 1-based значения).
    // Обрабатывается только анализатором (semantic/format_check), не кодогенерацией.
    pool.register_builtin_attr(Format, {"printf", std::string_view{}, std::string_view{}}, /*analyzer=*/true);
}

} // namespace trust