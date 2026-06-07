// attr_builtin.hpp — built-in attribute name constants
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

// ── Built-in attribute name constants ──
// These are the canonical names used to identify built-in attributes.

namespace attr {

inline constexpr std::string_view Const = "const";
inline constexpr std::string_view Pure = "pure";
inline constexpr std::string_view Send = "send";
inline constexpr std::string_view Sync = "sync";
inline constexpr std::string_view Thread = "thread";
inline constexpr std::string_view ReadOnly = "readonly";
inline constexpr std::string_view Optional = "optional";
inline constexpr std::string_view NoExcept = "noexcept";
inline constexpr std::string_view StackGuard = "stack_guard";
inline constexpr std::string_view Trust = "trust";
inline constexpr std::string_view Require = "require";
inline constexpr std::string_view Ensure = "ensure";
inline constexpr std::string_view DependMacro = "depend_macro";

// Legacy names (aliases for backward compatibility, kept in the same namespace)
inline constexpr std::string_view Immutable = "immutable";
inline constexpr std::string_view SmtPre = "smt_pre";
inline constexpr std::string_view SmtPost = "smt_post";
inline constexpr std::string_view FuncPure = "func_pure";
inline constexpr std::string_view FuncConst = "func_const";
inline constexpr std::string_view FuncConstexpr = "constexpr";
inline constexpr std::string_view ThreadLocal = "thread_local";

} // namespace attr

} // namespace trust