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
inline constexpr std::string_view StackGuard = "stack_guard";
inline constexpr std::string_view Trust = "trust";
inline constexpr std::string_view Require = "require";
inline constexpr std::string_view Ensure = "ensure";
inline constexpr std::string_view DependMacro = "depend_macro";

// Link - библиотека, линкуемая для нативной декларации (%...). Несёт ОДИН строковый
// параметр - имя библиотеки (`@[link("m")]` → `-lm`). Только статическая линковка;
// существование символа/библиотеки не проверяется (эмиссия флага линковщику).
inline constexpr std::string_view Link = "link";

// Format - компиляйт-тайм проверка типов аргументов на соответствие форматной строке
// printf (GCC-аналог `__attribute__((format(...)))`). Несёт ТРИ параметра:
//   @[format("printf", <string_index>, <first_to_check>)]
// индексы 1-based (конвенция GCC). Проверяется, что аргументы вызова соответствуют
// спецификаторам формата (см. semantic/format_check.hpp).
inline constexpr std::string_view Format = "format";

// Reftype - вид ссылки (`@[reftype("ptr")]`), плоский enum RefType (types/typekind.hpp).
// Несёт ОДИН строковый параметр - мнемоническое имя вида: value/shared/weak/unique/
// ptr/mptr/ref/rref/ptrptr (см. types/REFType.md). Семантика при трансляции:
// первая ссылка на тип без признака - fast-path бит withRefType; вложенность
// (ссылка на ссылочный тип) - составной узел getOrCreateRefType.
inline constexpr std::string_view Reftype = "reftype";

// Legacy names (aliases for backward compatibility, kept in the same namespace)
inline constexpr std::string_view SmtPre = "smt_pre";
inline constexpr std::string_view SmtPost = "smt_post";
inline constexpr std::string_view FuncConst = "func_const";
inline constexpr std::string_view FuncPure = "func_pure";
inline constexpr std::string_view FuncConstexpr = "constexpr";
inline constexpr std::string_view ThreadLocal = "thread_local";

} // namespace attr

} // namespace trust