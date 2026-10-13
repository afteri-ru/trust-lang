// attr_builtin.cpp - AttrPool::registerBuiltinAttrs implementation

#include "attrs/attr_pool.hpp"
#include "attrs/attr_builtin.hpp"

namespace trust {

void AttrPool::registerBuiltinAttrs(AttrPool& pool) {
    // Flags declare which processing stage consumes the attribute (see detail::is_handled):
    //   analyzer=true -> handled by the semantic analyzer; codegen=true -> by the C++ transpiler.
    // An attribute with NEITHER flag is "unhandled" -> -Wunhandled-attr fires on its use.

    // Attributes without required parameters
    // pure/send/sync/thread are registered for compatibility, but currently NO stage
    // consumes them (no analyzer/codegen support) - an explicit use is flagged as unhandled.
    pool.register_builtin_attr(attr::Pure);
    pool.register_builtin_attr(attr::Send);
    pool.register_builtin_attr(attr::Sync);
    pool.register_builtin_attr(attr::Thread);
    pool.register_builtin_attr(attr::ReadOnly, {}, /*analyzer=*/true, /*codegen=*/true);
    pool.register_builtin_attr(attr::NoExcept, {}, /*codegen=*/true);
    // StackCheck: без аргумента - limit; с одним целым аргументом N - явный размер.
    // Пустой дефолт-параметр - wildcard (принимает любое значение); см. Attr::matches_params.
    pool.register_builtin_attr(attr::StackCheck, {std::string_view{}}, /*analyzer=*/true, /*codegen=*/true);
    // Function qualifiers / storage class attributes (consumed by the C++ code generator)
    pool.register_builtin_attr(attr::FuncConst, {}, /*codegen=*/true);
    pool.register_builtin_attr(attr::FuncPure, {}, /*codegen=*/true);
    pool.register_builtin_attr(attr::FuncConstexpr, {}, /*codegen=*/true);
    pool.register_builtin_attr(attr::ThreadLocal, {}, /*analyzer=*/true, /*codegen=*/true);

    // @coroutine - маркер корутинной (асинхронной) функции. Обрабатывается анализатором
    // (место потребителя - семантика); кодогенератор пока не читает (co_* не реализованы).
    pool.register_builtin_attr(attr::Coroutine, {}, /*analyzer=*/true, /*codegen=*/false);

    // @link("libname") - имя линкуемой библиотеки для нативной декларации.
    // Один строковый параметр; пустой дефолт - wildcard (принимает любое имя).
    // link обрабатывается только кодогенерацией (линковка, type_emit).
    pool.register_builtin_attr(attr::Link, {std::string_view{}}, /*codegen=*/true);

    // @include("header") - зависимый C++-заголовок для нативной декларации/типа.
    // Один строковый параметр (имя/директива заголовка); пустой дефолт - wildcard.
    pool.register_builtin_attr(attr::Include, {std::string_view{}}, /*analyzer=*/true, /*codegen=*/true);

    // @matcher(\"fn\") - переопределяемая функция сравнения в операторе match. Один строковый
    // параметр (имя функции-предиката); пустой дефолт - wildcard. Обрабатывается анализатором
    // (проверка сигнатуры) и кодогенерацией (эмиссия вызова fn(tmp, pattern)).
    pool.register_builtin_attr(attr::Matcher, {std::string_view{}}, /*analyzer=*/true, /*codegen=*/true);

    // @reftype("ptr") / @reftype("shared", <sync_policy>[, <timeout>]) - вид ссылки (плоский RefType)
    // + опционально класс синхронизации доступа и пер-объектный таймаут детектора. Три пустых
    // wildcard-дефолта => параметры опциональны по количеству (1..3), см. Attr::matches_params.
    pool.register_builtin_attr(attr::Reftype, {std::string_view{}, std::string_view{}, std::string_view{}},
                               /*analyzer=*/true, /*codegen=*/true);

    // @deleter(D) - deleter внешнего ресурса: РЕАЛЬНОЕ имя deleter-типа (функтор) в TypeSystem.
    // Один wildcard-параметр (принимает любое имя) => ровно 1 аргумент; семантика проверяет,
    // что D резолвится и является deleter-типом, и что вид владения - unique/shared.
    pool.register_builtin_attr(attr::Deleter, {std::string_view{}}, /*analyzer=*/true, /*codegen=*/true);

    // @borrowed - атрибут отслеживания зависимых данных. Без параметров. Ставится на класс/тип,
    // метод или переменную; поведение управляется -Wborrowed= (см. attr::Borrowed).
    // Borrowed обрабатывается только анализатором (semantic/nativeref); кодогенерация его не читает.
    pool.register_builtin_attr(attr::Borrowed, {}, /*analyzer=*/true, /*codegen=*/false);

    // @pin - стабильность адреса (ручная затравка; авто-вывод через add_attr(manual=false)).
    // Без параметров; валидируется анализатором. Pin-механика (запрет move/copy) - отдельная ветка.
    pool.register_builtin_attr(attr::Pin, {}, /*analyzer=*/true, /*codegen=*/false);

    // @lifetime(<area>[, <name>]) - область жизни view: первый аргумент обязателен (закрытый
    // набор area), второй опционален (name для param/var/named). Два wildcard-дефолта => 1..2
    // аргумента; закрытость набора area и правила name проверяет семантический валидатор
    // (не matches_params). Обрабатывается только анализатором.
    pool.register_builtin_attr(attr::Lifetime, {std::string_view{}, std::string_view{}}, /*analyzer=*/true, /*codegen=*/false);

    // @format("printf", string_index, first_to_check) - компиляйт-тайм проверка аргументов
    // на соответствие форматной строке printf (GCC-аналог). Первый параметр - архетип
    // формата ("printf"); два индекса - пустые wildcard (принимают любые 1-based значения).
    // Обрабатывается только анализатором (semantic/format_check), не кодогенерацией.
    pool.register_builtin_attr(attr::Format, {attr::FormatPrintf, std::string_view{}, std::string_view{}}, /*analyzer=*/true);

    // @virtual / @override - маркеры полиморфизма. Зарегистрированы (нет -Wunknown-attributes),
    // но НЕ реализованы: анализатор выдаёт явную ошибку «не реализовано» при использовании.
    pool.register_builtin_attr(attr::Virtual, {}, /*analyzer=*/true, /*codegen=*/false);
    pool.register_builtin_attr(attr::Override, {}, /*analyzer=*/true, /*codegen=*/false);
}

} // namespace trust