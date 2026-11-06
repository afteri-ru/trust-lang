#pragma once

#include <string_view>

namespace trust {

namespace attr {

// ЕДИНСТВЕННЫЙ атрибут иммутабельности данных ('^' в имени); FuncConst/FuncPure/FuncConstexpr
// относятся к эффектам/чистоте функций (другой смысл).
inline constexpr std::string_view ReadOnly = "readonly";
inline constexpr std::string_view Pure = "pure";
inline constexpr std::string_view Send = "send";
inline constexpr std::string_view Sync = "sync";
inline constexpr std::string_view Thread = "thread";
inline constexpr std::string_view Optional = "optional";
inline constexpr std::string_view NoExcept = "noexcept";

// Без аргумента - limit (макс. размер стека функции из .stack_sizes + reserve); с целым N -
// явный размер свободного места (check_overflow(N)). На функции вставляет проверку ПЕРЕД каждым
// вызовом; отдельные явные функции: %stack_check/%stack_check_limit/%set_reserve*.
inline constexpr std::string_view StackCheck = "stack_check";

// ОДИН строковый параметр - имя библиотеки (`@[link("m")]` -> `-lm`). Только статическая линковка;
// существование символа/библиотеки не проверяется (эмиссия флага линковщику).
inline constexpr std::string_view Link = "link";

// ОДИН строковый параметр - имя/директива заголовка; форму директивы (#include <...>/"...")
// определяет includeDirective (attrs/attr_args.hpp).
inline constexpr std::string_view Include = "include";

// НАЗВАНИЕ НАТИВНОГО СИМВОЛА при ЭКСПОРТЕ: ОДИН строковый параметр - C/C++-имя БЕЗ ведущего '%'
// (`@[export("fib")]` -> C-сцепление, `@[export("ns::fib")]` -> C++). Импорт задаётся RHS-целью
// `:= %cpp ...` и атрибута не требует; роль (импорт/экспорт) - производная от наличия определения.
inline constexpr std::string_view Export = "export";

// Переопределяемая функция сравнения в match: ОДИН строковый параметр - имя предиката
// `bool fn(T_value, T_pattern)`; вместо сравнения по значению эмитится `fn(tmp, <pattern>)`.
// Неприменим к сопоставлению по типу (`~>`/`~~>`/`~~~>`); эмиссия всегда if/else, не switch.
inline constexpr std::string_view Matcher = "matcher";

// Компиляйт-тайм проверка аргументов против форматной строки printf. ТРИ параметра
// `@[format("printf", <string_index>, <first_to_check>)]`, индексы 1-based (GCC-конвенция).
inline constexpr std::string_view Format = "format";

// Архетип форматной строки для @[format(<archetype>, ...)]: единый источник регистрации и проверки.
inline constexpr std::string_view FormatPrintf = "printf";

// Вид ссылки - плоский enum RefType (types/typekind.hpp). Первый параметр - мнемоника вида
// (value/shared/weak/unique/ptr/mptr/ref/rref/ptrptr); для shared/weak опциональны РЕАЛЬНОЕ имя
// класса синхронизации доступа (резолвится через TypeSystem, не строковым маппингом) и
// пер-объектный таймаут детектора (<ms|s|nano>, без суффикса - секунды). См. types/type/REFType.md.
inline constexpr std::string_view Reftype = "reftype";

// Deleter внешнего ресурса: ровно ОДИН аргумент - имя deleter-ТИПА (функтор void operator()(V*)).
// Для unique входит в тип, для shared стирается (применяется при создании через adopt);
// неприменим к не-владеющим видам. См. types/type/REFType.md §9.2.
inline constexpr std::string_view Deleter = "deleter";

// Отслеживание ЗАВИСИМЫХ данных: объект держит данные, производные от source; при мутации/
// перемещении source зависимый становится dangling. Ось независима от вида ссылки. Ставится на
// класс/тип, метод или переменную; КОПИРУЕТСЯ при создании/присваивании (переприсваивание
// независимым значением снимает зависимость). Severity - -Wborrowed=. См. semantic/nativeref.hpp.
inline constexpr std::string_view Borrowed = "borrowed";

// Стабильность адреса: адрес - часть идентичности объекта, поэтому move/copy недопустимы.
// Без параметров; авто-вывод помечается add_attr(..., manual=false). Обрабатывается анализатором.
inline constexpr std::string_view Pin = "pin";

// Область жизни невладеющего view. area (обязателен): self|param|var|named|program|thread|scope|_;
// name обязателен для param/var/named, иначе запрещён; `_` = «выведи здесь». Два wildcard-дефолта =>
// 1..2 аргумента. Обрабатывается анализатором.
inline constexpr std::string_view Lifetime = "lifetime";

// Function qualifiers / storage class: consumed by the C++ code generator.
inline constexpr std::string_view FuncConst = "func_const";
inline constexpr std::string_view FuncPure = "func_pure";
inline constexpr std::string_view FuncConstexpr = "constexpr";
inline constexpr std::string_view ThreadLocal = "thread_local";

// Корутинная (асинхронная) функция: передачи `|^|`/`|*|` допустимы только под этим атрибутом.
inline constexpr std::string_view Coroutine = "coroutine";

// ЗАРЕГИСТРИРОВАНЫ как встроенные (чтобы не выдавать -Wunknown-attributes), но НЕ реализованы:
// анализатор выдаёт явную ошибку «не реализовано» при использовании.
inline constexpr std::string_view Virtual = "virtual";
inline constexpr std::string_view Override = "override";

} // namespace attr

} // namespace trust
