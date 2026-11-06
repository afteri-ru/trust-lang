// Маркер ЯВНО ЧАСТИЧНОГО switch по enum (предикат множества: «kind входит в набор?»).
// Такой switch намеренно перечисляет только элементы множества; остальные попадают в
// `default:` и это ОСОЗНАННОЕ решение (не молчаливый пропуск). Под `-Wswitch-enum`
// маркер подавляет предупреждение ровно вокруг одного switch и делает решение видимым
// в коде и в grep: `TRUST_PARTIAL_ENUM_SWITCH` - единственный легальный способ написать
// частичный switch по enum без `-Wswitch-enum`-шума.
// Полные (исчерпывающие) switch по ParserToken::Kind пишутся БЕЗ default - см.
// ast/kind_dispatch.hpp и src/ast/nodes/ast_children.cpp.

#pragma once

#if defined(__clang__)
#define TRUST_PARTIAL_ENUM_SWITCH_BEGIN _Pragma("clang diagnostic push") _Pragma("clang diagnostic ignored \"-Wswitch-enum\"")
#define TRUST_PARTIAL_ENUM_SWITCH_END _Pragma("clang diagnostic pop")
#else
#define TRUST_PARTIAL_ENUM_SWITCH_BEGIN
#define TRUST_PARTIAL_ENUM_SWITCH_END
#endif
