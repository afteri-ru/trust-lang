// type_names.hpp — canonical names for all built-in types
//
// This file defines string_view constants for every built-in type name,
// organized into three namespace categories:
//   trust::type           — concrete builtin types (Data ≠ 0)
//   trust::type_generic   — generic / abstract group types (Any + group aliases)
//   trust::type_category  — abstract type categories (Struct, Function, Class, ...)
//
// Modeled after attr_builtin.hpp which does the same for attributes.
// Usage: type::Void, type::Int32, type_generic::Any, type_category::Function, etc.

#pragma once

#include <string_view>

namespace trust::type {

// ── Void types ────────────────────────────────────────────
inline constexpr std::string_view Void = "Void";
inline constexpr std::string_view None = "None";

// ── Logical ───────────────────────────────────────────────
inline constexpr std::string_view Bool = "Bool";

// ── Signed integers ───────────────────────────────────────
inline constexpr std::string_view Int8 = "Int8";
inline constexpr std::string_view Int16 = "Int16";
inline constexpr std::string_view Int32 = "Int32";
inline constexpr std::string_view Int64 = "Int64";

// ── Unsigned integers ─────────────────────────────────────
inline constexpr std::string_view UInt8 = "UInt8";
inline constexpr std::string_view UInt16 = "UInt16";
inline constexpr std::string_view UInt32 = "UInt32";
inline constexpr std::string_view UInt64 = "UInt64";

// ── Floating point ────────────────────────────────────────
inline constexpr std::string_view Float16 = "Float16";
inline constexpr std::string_view Float32 = "Float32";
inline constexpr std::string_view Float64 = "Float64";

// ── BFloat ────────────────────────────────────────────────
inline constexpr std::string_view BFloat16 = "BFloat16";

// ── Complex ───────────────────────────────────────────────
inline constexpr std::string_view Complex32 = "Complex32";
inline constexpr std::string_view Complex64 = "Complex64";

// ── String types ──────────────────────────────────────────
inline constexpr std::string_view StrChar = "StrChar";
inline constexpr std::string_view StrWide = "StrWide";

// ── Rational ──────────────────────────────────────────────
inline constexpr std::string_view Rational = "Rational";

// ── Integer/float aliases ─────────────────────────────────
inline constexpr std::string_view Char = "Char";
inline constexpr std::string_view Byte = "Byte";
inline constexpr std::string_view Word = "Word";
inline constexpr std::string_view DWord = "DWord";
inline constexpr std::string_view DDWord = "DDWord";
inline constexpr std::string_view Single = "Single";
inline constexpr std::string_view Double = "Double";

} // namespace trust::type

namespace trust::type_generic {

// ── Abstract / root type ──────────────────────────────────
inline constexpr std::string_view Any = "Any";

// ── Generalized groups (Data = 0) ─────────────────────────
inline constexpr std::string_view Integers = "Integers";
inline constexpr std::string_view Numbers = "Numbers";
inline constexpr std::string_view Strings = "Strings";
inline constexpr std::string_view Tensors = "Tensors";

} // namespace trust::type_generic

namespace trust::type_category {

// ── Structured types ──────────────────────────────────────
inline constexpr std::string_view Struct = "Struct";
inline constexpr std::string_view Enum = "Enum";
inline constexpr std::string_view Variant = "Variant";
inline constexpr std::string_view Tuple = "Tuple";
inline constexpr std::string_view Optional = "Optional";
inline constexpr std::string_view Expected = "Expected";

// ── Callable ──────────────────────────────────────────────
inline constexpr std::string_view Function = "Function";
inline constexpr std::string_view Closure = "Closure";
inline constexpr std::string_view Coroutine = "Coroutine";
inline constexpr std::string_view Generator = "Generator";
inline constexpr std::string_view Method = "Method";
inline constexpr std::string_view Delegate = "Delegate";

// ── Classes ───────────────────────────────────────────────
inline constexpr std::string_view Class = "Class";
inline constexpr std::string_view Interface = "Interface";

// ── Ranges ────────────────────────────────────────────────
inline constexpr std::string_view Range = "Range";
inline constexpr std::string_view Slice = "Slice";
inline constexpr std::string_view View = "View";

// ── Iterators ─────────────────────────────────────────────
inline constexpr std::string_view Forward = "Forward";
inline constexpr std::string_view Bidirectional = "Bidirectional";
inline constexpr std::string_view RandomAccess = "RandomAccess";

// ── Date/Time ─────────────────────────────────────────────
inline constexpr std::string_view Duration = "Duration";
inline constexpr std::string_view TimePoint = "TimePoint";

// ── Async ─────────────────────────────────────────────────
inline constexpr std::string_view Future = "Future";
inline constexpr std::string_view Awaitable = "Awaitable";

// ── Sync ──────────────────────────────────────────────────
inline constexpr std::string_view Thread = "Thread";
inline constexpr std::string_view Mutex = "Mutex";
inline constexpr std::string_view Condition = "Condition";

// ── Exceptions ────────────────────────────────────────────
inline constexpr std::string_view Exception = "Exception";
inline constexpr std::string_view Error = "Error";

// ── Ellipsis (variadic markers) ───────────────────────────
inline constexpr std::string_view EllipsisAny = "EllipsisAny";
inline constexpr std::string_view EllipsisTyped = "EllipsisTyped";

// ── Other abstract ────────────────────────────────────────
inline constexpr std::string_view Arithmetics = "Arithmetics";
inline constexpr std::string_view Native = "Native";
inline constexpr std::string_view TemplateParam = "TemplateParam";

} // namespace trust::type_category