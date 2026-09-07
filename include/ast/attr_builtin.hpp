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

// Reftype - вид ссылки (`@[reftype("ptr")]`), плоский enum RefType (types/typekind.hpp).
// Расширенная форма для синхронизированной ссылки:
//   @[reftype("shared"|"weak"[, <sync_policy>][, <timeout>])]
//   - первый параметр - мнемоническое имя вида: value/shared/weak/unique/ptr/mptr/ref/rref/ptrptr
//     (см. types/REFType.md);
//   - второй (опциональный, ТОЛЬКО для shared/weak) - РЕАЛЬНОЕ имя класса синхронизации доступа,
//     зарегистрированного в TypeRegistry (встроенные политики Group::kSyncPolicy:
//     SyncMutexPolicy / SyncRwMutexPolicy / SyncSingleThreadPolicy, cppName trust::Sync*Policy);
//     резолвится через реестр (findType), НЕ строковым маппингом. Транслируется в
//     trust::SyncShared<T, Policy> (shared) / trust::Weak<trust::SyncShared<T, Policy>> (weak);
//   - третий (опциональный) - пер-объектный таймаут детектора взаимной блокировки
//     (<ms|s|nano>, без суффикса = секунды), перекрывает глобальный (дефолт 5s).
// Семантика при трансляции: первая ссылка на тип без признака - fast-path бит withRefType;
// вложенность (ссылка на ссылочный тип) - составной узел getOrCreateRefType.
inline constexpr std::string_view Reftype = "reftype";

// RefTrace - условный атрибут отслеживания инвалидации ссылок (`@[reftrace@]`).
// Ставится на КЛАСС/ТИП или на МЕТОД (НЕ на переменную):
//   - на классе: любая переменная такого типа всегда внутри держит ссылку/указатель на чужие
//     данные (напр. std::span, base_iterator) -> создание ЛЮБОЙ переменной этого типа
//     автоматически отслеживается (без указания в коде);
//   - на методе: метод возвращает ссылку во внутренние данные объекта -> результат зависим.
// Помимо атрибута АВТОМАТИЧЕСКИ (без него) отслеживаются: переменные чисто ссылочных типов
// (kRef/kRref/kPtr/kPtrPtr; умные shared/weak/unique/locker - НЕ отслеживаются), индексный
// доступ obj[i] и любой метод, возвращающий чисто ссылочный тип.
// Поведение (ошибка/предупреждение/игнор) управляется severity-опцией -Wreftrace=
// (DiagId::RefTrace в semantic/diag.hpp). См. semantic/nativeref.hpp.
inline constexpr std::string_view RefTrace = "reftrace";

// Legacy names (aliases for backward compatibility, kept in the same namespace)
inline constexpr std::string_view FuncConst = "func_const";
inline constexpr std::string_view FuncPure = "func_pure";
inline constexpr std::string_view FuncConstexpr = "constexpr";
inline constexpr std::string_view ThreadLocal = "thread_local";

} // namespace attr

} // namespace trust