// attr_builtin.hpp — built-in attribute definitions
//
// This file defines the canonical names and required parameter types
// for all built-in attributes. The setup function register_builtin_attrs()
// registers them all into an AttrPool in one call.
//
// Built-in attributes are identified by their name string, not by an enum.
// This allows the storage system to treat built-in and user-defined
// attributes uniformly.

#pragma once

#include "ast/attr_pool.hpp"
#include <string_view>

namespace trust {

/// Register all built-in attributes into the given pool.
/// Safe to call multiple times — duplicate registration is a no-op.
void register_builtin_attrs(AttrPool& pool);

// ── Built-in attribute name constants ──
// These are the canonical names used to identify built-in attributes.

namespace attr_names {

inline constexpr std::string_view kConst = "const";
inline constexpr std::string_view kPure = "pure";
inline constexpr std::string_view kSend = "send";
inline constexpr std::string_view kSync = "sync";
inline constexpr std::string_view kThread = "thread";
inline constexpr std::string_view kReadOnly = "readonly";
inline constexpr std::string_view kNoExcept = "noexcept";
inline constexpr std::string_view kStackGuard = "stack_guard";
inline constexpr std::string_view kTrust = "trust";
inline constexpr std::string_view kRequire = "require";
inline constexpr std::string_view kEnsure = "ensure";

} // namespace attr_names

} // namespace trust