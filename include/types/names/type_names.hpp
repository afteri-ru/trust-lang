// Канонические имена всех встроенных типов: string_view-константы, сгруппированные по namespace:
//   trust::type           - конкретные встроенные типы (Data != 0)
//   trust::type_generic   - generic/абстрактные группы (Any + алиасы групп)
//   trust::type_category  - абстрактные категории типов (Struct, Function, Class, ...)

#pragma once

#include <string_view>

namespace trust::type {

inline constexpr std::string_view Void = "Void";
inline constexpr std::string_view None = "None";

inline constexpr std::string_view Bool = "Bool";

inline constexpr std::string_view Int8 = "Int8";
inline constexpr std::string_view Int16 = "Int16";
inline constexpr std::string_view Int32 = "Int32";
inline constexpr std::string_view Int64 = "Int64";

inline constexpr std::string_view UInt8 = "UInt8";
inline constexpr std::string_view UInt16 = "UInt16";
inline constexpr std::string_view UInt32 = "UInt32";
inline constexpr std::string_view UInt64 = "UInt64";

inline constexpr std::string_view Float16 = "Float16";
inline constexpr std::string_view Float32 = "Float32";
inline constexpr std::string_view Float64 = "Float64";

inline constexpr std::string_view BFloat16 = "BFloat16";

inline constexpr std::string_view Complex32 = "Complex32";
inline constexpr std::string_view Complex64 = "Complex64";

inline constexpr std::string_view StrChar = "StrChar";
inline constexpr std::string_view StrWide = "StrWide";

// Результат метода StrChar.%c_str(); транслируется в `const char*` (C++).
inline constexpr std::string_view CString = "CString";

inline constexpr std::string_view Rational = "Rational";

// Та же группа kArbitraryPrecision, что и у Rational (Data: Rational=1, BigInteger=2).
inline constexpr std::string_view BigInteger = "BigInteger";

// C++-классы прерываний потока (IntAny - база, IntPlus - возврат, IntMinus - ошибка).
// C++-имена `trust::IntAny`/`IntPlus`/`IntMinus` приходят из `stdlib/core/exceptions.src`
// (при регистрации — forward-интерфейс без cppName).
inline constexpr std::string_view IntAny = "IntAny";
inline constexpr std::string_view IntPlus = "IntPlus";
inline constexpr std::string_view IntMinus = "IntMinus";

// Базовый C++-класс исключений (catch(:StdException) → `catch (std::exception&)`).
// Ловит ЛЮБОЕ std::exception-производное (в т.ч. ошибки конверсии std::stoll/BigInteger
// и trust::IntMinus, т.к. тот наследует std::runtime_error). catch - обобщённый аргумент,
// не только IntAny/IntPlus/IntMinus.
inline constexpr std::string_view StdException = "StdException";

inline constexpr std::string_view Dict = "Dict";
inline constexpr std::string_view Dictionary = "Dictionary";

inline constexpr std::string_view Tensor = "Tensor";
inline constexpr std::string_view DenseTensor = "DenseTensor";
inline constexpr std::string_view SparseTensor = "SparseTensor";

inline constexpr std::string_view Array = "Array";

/// C++-имя встроенного контейнера `:Array` (mutable → std::vector, const → std::array).
[[nodiscard]] inline bool isBuiltinContainerCppName(std::string_view cpp_name) noexcept {
    return cpp_name == "std::vector" || cpp_name == "std::array";
}

// Группа Group::kAccessPolicy; C++-имя (trust::Access*) приходит из stdlib/core/mem.src
// (при регистрации — forward-интерфейс). Имя в реестре = РЕАЛЬНОЕ имя класса:
// 2-й аргумент reftype (`@[reftype("shared", AccessMutex)@]`) резолвится через findType.
inline constexpr std::string_view AccessMutex = "AccessMutex";
inline constexpr std::string_view AccessRwMutex = "AccessRwMutex";
inline constexpr std::string_view AccessSingleThread = "AccessSingleThread";
/// Обёртка синхронизированной сильной ссылки (cppName trust::AccessShared). Имя берётся из реестра
/// (findType/emitTypeName), НЕ хардкодится строкой в кодогенерации.
inline constexpr std::string_view AccessShared = "AccessShared";

/// Обёртка СТАТИЧЕСКОГО эксклюзивного владения (cppName trust::StaticUnique; inline zero-cost).
/// Имя берётся из реестра (findType/emitTypeName), НЕ хардкодится строкой.
inline constexpr std::string_view StaticUnique = "StaticUnique";

// Группа Group::kDeleterPolicy; C++-имя (trust::FreeDeleter) приходит из stdlib/core/mem.src.
// Имя в реестре = РЕАЛЬНОЕ имя
// класса: аргумент атрибута `@[deleter(FreeDeleter)]` резолвится через findType.
inline constexpr std::string_view FreeDeleter = "FreeDeleter";
inline constexpr std::string_view FileDeleter = "FileDeleter";

inline constexpr std::string_view Char = "Char";
inline constexpr std::string_view Byte = "Byte";
inline constexpr std::string_view Word = "Word";
inline constexpr std::string_view DWord = "DWord";
inline constexpr std::string_view DDWord = "DDWord";
inline constexpr std::string_view Single = "Single";
inline constexpr std::string_view Double = "Double";

} // namespace trust::type

namespace trust::type_generic {

inline constexpr std::string_view Any = "Any";
// Внутренний sentinel «тип не выведен» (НЕ пользовательский тип, до кодогена не доходит).
inline constexpr std::string_view Auto = "Auto";

inline constexpr std::string_view Integers = "Integers";
inline constexpr std::string_view Numbers = "Numbers";
inline constexpr std::string_view Strings = "Strings";
inline constexpr std::string_view Tensors = "Tensors";

} // namespace trust::type_generic

namespace trust::type_category {

inline constexpr std::string_view Struct = "Struct";
inline constexpr std::string_view Enum = "Enum";
inline constexpr std::string_view Variant = "Variant";
inline constexpr std::string_view Tuple = "Tuple";
inline constexpr std::string_view Optional = "Optional";
inline constexpr std::string_view Expected = "Expected";

inline constexpr std::string_view Function = "Function";
inline constexpr std::string_view Closure = "Closure";
inline constexpr std::string_view Coroutine = "Coroutine";
inline constexpr std::string_view Generator = "Generator";
inline constexpr std::string_view Method = "Method";
inline constexpr std::string_view Delegate = "Delegate";

inline constexpr std::string_view Class = "Class";
inline constexpr std::string_view Interface = "Interface";

inline constexpr std::string_view Range = "Range";
inline constexpr std::string_view Slice = "Slice";
inline constexpr std::string_view View = "View";

inline constexpr std::string_view Forward = "Forward";
inline constexpr std::string_view Bidirectional = "Bidirectional";
inline constexpr std::string_view RandomAccess = "RandomAccess";

inline constexpr std::string_view Duration = "Duration";
inline constexpr std::string_view TimePoint = "TimePoint";

inline constexpr std::string_view Future = "Future";
inline constexpr std::string_view Awaitable = "Awaitable";

inline constexpr std::string_view Thread = "Thread";
inline constexpr std::string_view Mutex = "Mutex";
inline constexpr std::string_view Condition = "Condition";

inline constexpr std::string_view Exception = "Exception";
inline constexpr std::string_view Error = "Error";

inline constexpr std::string_view EllipsisAny = "EllipsisAny";
inline constexpr std::string_view EllipsisTyped = "EllipsisTyped";

inline constexpr std::string_view Arithmetics = "Arithmetics";
inline constexpr std::string_view Native = "Native";
inline constexpr std::string_view TemplateParam = "TemplateParam";

} // namespace trust::type_category