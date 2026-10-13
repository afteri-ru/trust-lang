#pragma once

// include/ir/mir/mir.hpp
// MIR-инструкция: ОДНО наблюдаемое действие над местом (place).
//
// Контракт:
//   * Инструкция описывает семантический порядок вычисления; CFG-адаптер отвечает за
//     линеаризацию (short-circuit разворачивается в блоки).
//   * `place` — узел объявления места (ключ PlaceTable в MIR-адаптере). Для Read/Write/Move
//     это resolvedDecl ссылки; для Declare — сам объявляемый узел.
//   * `site` — узел-источник диагностики (ссылка/оператор/скоуп).
//   * `assigned` — для Declare: инициализировано ли место; для Write: новое состояние.
//   * `finalWrite` — запись делает место константным (readonly-цель).
//   * `scope` — для Declare: скоуп объявления; для ScopeExit: выходящий скоуп.
//   * `projection` — для Write/Move: lvalue-узел с полем/индексом; `place` при этом — КОРЕНЬ.
//
// MIR — ЕДИНСТВЕННЫЙ вход flow-анализов; C++-бэкенд читает HIR.

#include "utils/error.hpp"

#include <cstdint>
#include <string_view>

namespace trust {

class AstNodeBase;

// Единый источник: X-макрос генерирует и enum, и строковое имя операции (mirOpName ниже).
#define TRUST_MIR_OP_NAMES(O)                                                        \
    /* объявление места (VarDecl / цель деструктуризации / catch-binding) */         \
    O(Declare, "declare")                                                            \
    /* чтение места */                                                               \
    O(Read, "read")                                                                  \
    /* запись места (assign/incdec/append/деструктуризация) */                       \
    O(Write, "write")                                                                \
    /* перемещение/сброс значения места (swap, `= _`) */                             \
    O(Move, "move")                                                                  \
    /* вызов: site — CallExpr, place — разрешённый callee (может быть nullptr) */    \
    O(Call, "call")                                                                  \
    /* конец области видимости (scope — выходящий скоуп) */                          \
    O(ScopeExit, "scope-exit")                                                       \
    /* утверждение/посторонний контракт: site — TrustContract (kind != Invariant) */ \
    O(Assert, "assert")                                                              \
    /* инвариант цикла: site — TrustContract с PropertyKind::Invariant */            \
    O(Invariant, "invariant")                                                        \
    /* источник терминатора (только BasicBlockT::terminatorNode) */                  \
    O(Terminator, "terminator")

enum class MirOp : std::uint8_t {
#define TRUST_MIR_OP_ENUM(name, text) name,
    TRUST_MIR_OP_NAMES(TRUST_MIR_OP_ENUM)
#undef TRUST_MIR_OP_ENUM
};

struct MirInstruction {
    MirOp kind = MirOp::Read;
    const AstNodeBase* place = nullptr;      ///< место (ключ PlaceTable) для Declare/Read/Write/Move
    const AstNodeBase* site = nullptr;       ///< источник диагностики/вызова
    bool assigned = true;                    ///< Declare: инициализировано; Write: новое состояние
    bool finalWrite = false;                 ///< Write: место становится константным
    const AstNodeBase* scope = nullptr;      ///< Declare: скоуп; ScopeExit: выходящий скоуп
    const AstNodeBase* projection = nullptr; ///< Write/Move: lvalue с полем/индексом (place = корень)
    /// Invariant: цикл (`WhileStmt`/`DoWhileStmt`), к которому lowering привязал инвариант.
    /// Структурный факт MIR: потребители (верификатор) не выводят связь повторно.
    const AstNodeBase* loopNode = nullptr;
};

/// Строковое имя операции. Недопустимое значение enum — внутренняя ошибка (FAULT).
[[nodiscard]] inline std::string_view mirOpName(MirOp kind) {
    switch (kind) {
#define TRUST_MIR_OP_CASE(name, text) \
    case MirOp::name:                 \
        return text;
        TRUST_MIR_OP_NAMES(TRUST_MIR_OP_CASE)
#undef TRUST_MIR_OP_CASE
    }
    FAULT("unknown MirOp value {}", static_cast<int>(kind));
}

} // namespace trust
