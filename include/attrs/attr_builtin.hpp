// attr_builtin.hpp - built-in attribute name constants
//
// This file defines the canonical names for all built-in attributes.
// Registration is handled automatically by AttrPool constructor.
//
// Built-in attributes are identified by their name string, not by an enum.
// This allows the storage system to treat built-in and user-defined
// attributes uniformly.

#pragma once

#include <string_view>

namespace trust {

// -- Built-in attribute name constants --
// These are the canonical names used to identify built-in attributes.

namespace attr {

// ReadOnly - ЕДИНСТВЕННЫЙ атрибут иммутабельности ('^' в имени). Прежний Const
// и синонимы Immutable/FuncConst удалены: иммутабельность данных выражается
// только attr::ReadOnly. FuncConst/FuncPure/FuncConstexpr - про эффекты/чистоту
// функций, это ДРУГОЙ смысл (не иммутабельность данных), они оставлены.
inline constexpr std::string_view ReadOnly = "readonly";
inline constexpr std::string_view Pure = "pure";
inline constexpr std::string_view Send = "send";
inline constexpr std::string_view Sync = "sync";
inline constexpr std::string_view Thread = "thread";
inline constexpr std::string_view Optional = "optional";
inline constexpr std::string_view NoExcept = "noexcept";
// StackCheck - защита стека от переполнения. Без аргумента = limit (проверка максимального
// размера стека функции из .stack_sizes + резерв reserve); с целочисленным аргументом N -
// явный размер свободного места (check_overflow(N)). Прежний StackGuard ("stack_guard") заменён
// на StackCheck. Атрибут на функцию - вставка проверки ПЕРЕД каждым вызовом этой функции;
// отдельные явные функции (не атрибуты) - %stack_check/%stack_check_limit/%set_reserve/%set_reserve_functions.
inline constexpr std::string_view StackCheck = "stack_check";

// Link - библиотека, линкуемая для нативной декларации (%...). Несёт ОДИН строковый
// параметр - имя библиотеки (`@[link("m")]` → `-lm`). Только статическая линковка;
// существование символа/библиотеки не проверяется (эмиссия флага линковщику).
inline constexpr std::string_view Link = "link";

// Include - зависимый C++-заголовок для нативной декларации/типа (`@[include("vector")@]`).
// Несёт ОДИН строковый параметр - имя/директиву заголовка. Правило формы эмитируемой директивы:
//   - аргумент с ведущим `"` → `#include "<...>"` (локальный/кавычечный инклуд);
//   - аргумент с ведущим `<` → `#include <...>` (угловой, директива задана целиком);
//   - иначе (голое имя, напр. "vector") → `#include <vector>` (угловой по умолчанию).
// Заголовок попадает в собранные директивы (m_requiredIncludes, дедуп) и prepend'ится в конце
// кодогенерации. Для нативных ТИПОВ (Этап B, структурные шаблоны) тот же атрибут будет
// переноситься в preprocIncludes типа и подтягиваться только при его использовании.
inline constexpr std::string_view Include = "include";

// Matcher - переопределяемая функция сравнения в операторе match (`@match(x) @[matcher("fn")] ==> { ... }`).
// Несёт ОДИН строковый параметр - имя функции-предиката `bool fn(T_value, T_pattern)`. В каждой ветке
// вместо сравнения по значению (==/===>) кодогенерация эмитит вызов `fn(tmp, <pattern>)`. Семантика
// резолвит имя функции, проверяет, что она объявлена как `bool fn(...)` с ровно двумя параметрами;
// matcher неприменим к сопоставлению по типу (`~>`/`~~>`/`~~~>`). Кодогенерация читает атрибут и
// эмитит вызов (всегда if/else, никогда switch).
inline constexpr std::string_view Matcher = "matcher";

// Format - компиляйт-тайм проверка типов аргументов на соответствие форматной строке
// printf (GCC-аналог `__attribute__((format(...)))`). Несёт ТРИ параметра:
//   @[format("printf", <string_index>, <first_to_check>)]
// индексы 1-based (конвенция GCC). Проверяется, что аргументы вызова соответствуют
// спецификаторам формата (см. semantic/format_check.hpp).
inline constexpr std::string_view Format = "format";

// Архетип форматной строки для @[format(<archetype>, ...)]; пока поддержан только printf
// (единый источник: registration wildcard-дефолт и проверка в семантике/транспиляторе).
inline constexpr std::string_view FormatPrintf = "printf";

// Reftype - вид ссылки (`@[reftype("ptr")]`), плоский enum RefType (types/typekind.hpp).
// Расширенная форма для синхронизированной ссылки:
//   @[reftype("shared"|"weak"[, <sync_policy>][, <timeout>])]
//   - первый параметр - мнемоническое имя вида: value/shared/weak/unique/ptr/mptr/ref/rref/ptrptr
//     (см. types/REFType.md);
//   - второй (опциональный, ТОЛЬКО для shared/weak) - РЕАЛЬНОЕ имя класса синхронизации доступа,
//     зарегистрированного в TypeRegistry (встроенные политики Group::kAccessPolicy:
//     AccessMutex / AccessRwMutex / AccessSingleThread, cppName trust::Sync*Policy);
//     резолвится через реестр (findType), НЕ строковым маппингом. Транслируется в
//     trust::AccessShared<T, Policy> (shared) / trust::Weak<trust::AccessShared<T, Policy>> (weak);
//   - третий (опциональный) - пер-объектный таймаут детектора взаимной блокировки
//     (<ms|s|nano>, без суффикса = секунды), перекрывает глобальный (дефолт 5s).
// Семантика при трансляции: первая ссылка на тип без признака - fast-path бит withRefType;
// вложенность (ссылка на ссылочный тип) - составной узел getOrCreateRefType.
inline constexpr std::string_view Reftype = "reftype";

// Deleter - deleter внешнего (не-память) ресурса (`@[deleter(D)]`), отдельный атрибут на
// владеющем объявлении (`@[reftype("unique"|"shared")]`). Ровно ОДИН аргумент - имя
// deleter-ТИПА (функтор `void operator()(V*)`), зарегистрированного в TypeRegistry: встроенный
// deleter-тип (Group::kDeleterPolicy, напр. FreeDeleter) или пользовательский нативный класс.
// Для `unique` D входит в ТИП (`trust::Unique<T,D>`, как `unique_ptr<T,D>`), для `shared`
// D стирается (тип остаётся `trust::Shared<T>`; применяется при создании через `adopt`).
// Неприменим к не-владеющим видам (ptr/ref/weak/...) - диагностика ошибки. См. types/REFType.md §9.2.
inline constexpr std::string_view Deleter = "deleter";

// Borrowed - атрибут отслеживания ЗАВИСИМЫХ данных (`@[borrowed@]`): объект (переменная/ссылка/
// класс) держит данные, производные от главного (source) объекта; при мутации/перемещении source
// зависимый становится невалидным (dangling). Ось НЕЗАВИСИМА от вида ссылки (RefType).
// Ставится на КЛАСС/ТИП, на МЕТОД или на ПЕРЕМЕННУЮ:
//   - на классе: любая переменная такого типа всегда внутри держит ссылку/указатель на чужие
//     данные (напр. std::span, base_iterator) -> создание ЛЮБОЙ переменной этого типа
//     автоматически отслеживается (без указания в коде);
//   - на методе: метод возвращает ссылку во внутренние данные объекта -> результат зависим;
//   - на переменной: явно объявляет переменную зависимой.
// Атрибут КОПИРУЕТСЯ при создании/присваивании: переменная, инициализированная/присвоенная из
// зависимой, тоже становится зависимой от того же корневого источника; переприсваивание
// независимым значением снимает зависимость (release).
// Поведение (ошибка/предупреждение/игнор) управляется severity-опцией -Wborrowed=
// (DiagId::Borrowed в semantic/diag.hpp). См. semantic/nativeref.hpp.
inline constexpr std::string_view Borrowed = "borrowed";

// Pin - стабильность адреса объекта (`@[pin]`). Ручная «затравка» для самоссылающихся/
// интрузивных типов и value-семантичных классов с внутренними ссылками: адрес является частью
// идентичности объекта, поэтому перемещение/копирование недопустимо. Без параметров.
// Обрабатывается анализатором; авто-вывод помечается `add_attr(..., /*manual=*/false)`
// (существующий kAttrManualFlag), по образцу stack_check_infer. Пока - регистрация и проверка
// применимости; pin-механика (запрет move/copy) - отдельная ветка (см. types/REFType.md).
inline constexpr std::string_view Pin = "pin";

// Lifetime - область жизни невладеющего view (`@[lifetime(<area>[, <name>])]`). Контракт:
//   area (обязателен): self | param | var | named | program | thread | scope | _
//   name (опционален):  обязателен для param/var/named, иначе запрещён
// `_` = «выведи здесь» (элизия для позиции); отсутствие атрибута = вывод по умолчанию.
// Классы хранения (static/auto) в синтаксис НЕ выносятся; маппинг storage->region - в доке.
// Два wildcard-дефолта => 1..2 аргумента (см. Attr::matches_params). Обрабатывается анализатором.
inline constexpr std::string_view Lifetime = "lifetime";

// Legacy names (aliases for backward compatibility, kept in the same namespace)
inline constexpr std::string_view FuncConst = "func_const";
inline constexpr std::string_view FuncPure = "func_pure";
inline constexpr std::string_view FuncConstexpr = "constexpr";
inline constexpr std::string_view ThreadLocal = "thread_local";

// Virtual / Override - маркеры полиморфизма (виртуальные методы и переопределение).
// ЗАРЕГИСТРИРОВАНЫ как встроенные (чтобы не выдавалось `-Wunknown-attributes`), но НЕ
// реализованы: анализатор объявлений выдаёт явную ошибку «не реализовано» при их использовании
// (см. semantic/record_decl_analyzer.cpp), чтобы они не игнорировались молча.
inline constexpr std::string_view Virtual = "virtual";
inline constexpr std::string_view Override = "override";

} // namespace attr

} // namespace trust