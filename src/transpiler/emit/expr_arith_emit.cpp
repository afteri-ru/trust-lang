// src/transpiler/emit/expr_arith_emit.cpp
// ExprEmitter: Арифметика с контролем переполнения (+,-,*,+=,-=,*=), ++/--, append и
// visitors арифметических/битовых/сравнения/логических операторов.

#include "transpiler/emit/expr_emit.hpp"
#include "transpiler/transpiler.hpp"
#include "transpiler/emit/emit_common.hpp"
#include "analysis/runtime_checks.hpp"
#include "ast/ast_nodes.hpp"
#include "attrs/attr_builtin.hpp"
#include "ast/ident_name.hpp"
#include "ast/kind_dispatch.hpp"
#include "ast/token_type.hpp"
#include "session/context.hpp"
#include "diag/registry.hpp"
#include "diag/base_diags.hpp"
#include "syntax/term/term.h"
#include "types/type_system.hpp"
#include "types/call.hpp"
#include "types/runtime_symbols.hpp"
#include "types/intrinsics.hpp"
#include "types/type_id.hpp"
#include "types/names.hpp"
#include "transpiler/emit/diag.hpp"
#include "utils/strings.hpp"

#include "transpiler/emit/expr_emit_util.hpp"
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace trust {

// -- Детекция целочисленного переполнения (опция -foverflow-check, default on) --------
// Классификацию «контролируемая знаковая машинная арифметика» (+,-,* и +=,-=,*=) владеет
// СЕМАНТИКА и кладёт готовый признак в Binary::m_overflowCheck; кодоген признак только читает.
// Диапазон проверки - тип ПРИЁМНИКА: для value-позиции это commonType (тип результата), для
// составного присваивания - тип ячейки (``&lhs``), поэтому узкие Int8/Int16 и inferred-цели
// (расширенные до типа операции) проверяются единообразно. На переполнение - runtime
// trust::IntMinus (ловится try/{-...-}). Детектор - встроенная функция целевого
// компилятора __builtin_*_overflow (GCC/Clang), без нового runtime-заголовка.

bool ExprEmitter::overflowCheckArith(const Binary& b) {
    // Классификацию «контролируемая арифметика» владеет СЕМАНТИКА (Binary::m_overflowCheck);
    // единый предикат классификации — `runtimeCheckOf` (analysis/runtime_checks.hpp).
    if (m_ectx.m_mode.inDefaultArg) {
        return false; // default-аргумент: только обычное C++-выражение, без лямбд/throw
    }
    if (runtimeCheckOf(b) != RuntimeCheckKind::kIntOverflow) {
        return false;
    }
    return m_ectx.m_ctx.opts().is_enabled(transpiler::FlagKind::OverflowCheck);
}

void ExprEmitter::emitCheckedArith(const Binary& b) {
    // trust::IntMinus бросается при переполнении - тип из доверенного runtime interrupt.hpp.
    m_driver.m_type.recordRequiredInclude("@trust/interrupt.hpp");
    m_driver.m_type.recordRequiredInclude("@trust/assert.hpp");
    const std::string op = std::string(binaryOpSpelling(b.m_op));
    const std::string builtin = (b.m_op == BinaryOp::Add || b.m_op == BinaryOp::AddAssign)   ? "add"
                                : (b.m_op == BinaryOp::Sub || b.m_op == BinaryOp::SubAssign) ? "sub"
                                                                                             : "mul";
    const std::optional<std::string> cppT = m_driver.m_type.emitTypeName(b.commonType.getTypeId());
    if (!cppT) {
        // Инвариант: знаковое машинное целое всегда имеет C++-имя. При нарушении фиксируем
        // ошибку кодогенерации (не тихий fallback) и эмитим арифметику без проверки переполнения.
        m_ectx.m_ctx.report(b.range(), diag::DiagId::ParseError, "overflow-check: cannot resolve C++ type of integer arithmetic (internal)");
        emitBinaryOperand(b.m_left.get(), b.lhsType.getTypeId(), b.commonType.getTypeId());
        m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, " " + op + " ");
        if (b.m_right) {
            emitBinaryOperand(b.m_right.get(), b.rhsType.getTypeId(), b.commonType.getTypeId());
        }
        return;
    }
    const std::string T = std::move(*cppT);

    // Унарный минус: 0 - x в типе T; на переполнении (-INT_MIN) - IntMinus.
    if (!b.m_left && b.m_right) {
        m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, "([&]{ " + T + " __a = ");
        emitBinaryOperand(b.m_right.get(), b.rhsType.getTypeId(), b.commonType.getTypeId());
        const SourceLocation loc = sourceLocation(m_ectx.m_ctx.source(), b.range());
        const std::string msgPrefix = (loc.line > 0 && !loc.file.empty())
                                          ? "trust::formatMessage(\"" + utils::escapeCppString(loc.file) + "\", " + std::to_string(loc.line) + ", "
                                          : std::string("std::string(");
        const std::string unaryMsg = msgPrefix + "\"integer overflow in unary '-'\")";
        m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, "; " + T + " __r; if (__builtin_sub_overflow(static_cast<" + T +
                                                                     ">(0), __a, &__r)) throw trust::IntMinus(" + unaryMsg + "); return __r; }())");
        return;
    }

    if (!isCompoundAssignOp(b.m_op)) {
        // Value-позиция: нет lvalue-приёмника, поэтому лямбда-IIFE вводит локальные __a/__b/__r
        // (сугубо кодген-локальные для &out детектора; не семантические временные - те создаёт
        // только lowering). Операнды кастуются в commonType как в обычной арифметике.
        m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, "([&]{ " + T + " __a = ");
        emitBinaryOperand(b.m_left.get(), b.lhsType.getTypeId(), b.commonType.getTypeId());
        m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, "; " + T + " __b = ");
        if (b.m_right) {
            emitBinaryOperand(b.m_right.get(), b.rhsType.getTypeId(), b.commonType.getTypeId());
        }
        const SourceLocation loc = sourceLocation(m_ectx.m_ctx.source(), b.range());
        const std::string msgPrefix = (loc.line > 0 && !loc.file.empty())
                                          ? "trust::formatMessage(\"" + utils::escapeCppString(loc.file) + "\", " + std::to_string(loc.line) + ", "
                                          : std::string("std::string(");
        const std::string valueMsg = msgPrefix + "\"integer overflow in '" + op + "'\")";
        m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, "; " + T + " __r; if (__builtin_" + builtin +
                                                                     "_overflow(__a, __b, &__r)) throw trust::IntMinus(" + valueMsg + "); return __r; }())");
        return;
    }

    // Составное присваивание (+=,-=,*=): приёмник-адрес = lhs, БЕЗ лямбды и временных.
    // Проверяемый диапазон берётся из ТИПА ЯЧЕЙКИ (*res = &lhs): для Int8/Int16 это диапазон
    // цели, для inferred-цели - расширенный тип хранения. lhs эмитится несколько раз - как
    // и в существующей ветке //= (принятое ограничение для простых lvalue-целей).
    m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, "(__builtin_" + builtin + "_overflow(");
    m_driver.emitExpr(b.m_left.get());
    m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, ", ");
    if (b.m_right) {
        emitBinaryOperand(b.m_right.get(), b.rhsType.getTypeId(), b.commonType.getTypeId());
    }
    m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, ", &");
    m_driver.emitExpr(b.m_left.get());
    const SourceLocation loc = sourceLocation(m_ectx.m_ctx.source(), b.range());
    const std::string msgPrefix = (loc.line > 0 && !loc.file.empty())
                                      ? "trust::formatMessage(\"" + utils::escapeCppString(loc.file) + "\", " + std::to_string(loc.line) + ", "
                                      : std::string("std::string(");
    const std::string compoundMsg = msgPrefix + "\"integer overflow in '" + op + "'\")";
    m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, ") ? throw trust::IntMinus(" + compoundMsg + ") : ");
    m_driver.emitExpr(b.m_left.get());
    m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, ")");
}

// Инкремент/декремент `++`/`--`. Префикс (`++x`/`--x`) возвращает НОВОЕ значение (эквивалент
// `x += 1`/`x -= 1`); постфикс (`x++`/`x--`) - СТАРОЕ, которое материализует низкоуровневая
// C++-лямбда (как интринсик `x :=: _`; это НЕ языковая временная - семантических временных
// транспилятор не создаёт). Цель - lvalue: постфикс хранит её в m_left, префикс - в m_right.
// Контроль переполнения (знаковая машинная арифметика, Binary::m_overflowCheck ±1) - как у
// `+=`/`-=`: __builtin_*_overflow, на переполнении throw trust::IntMinus.
void ExprEmitter::emitIncDec(const Binary& b) {
    const AstNodeBase* target = b.m_left ? b.m_left.get() : b.m_right.get();
    const bool postfix = isPostfixIncDecOp(b.m_op);
    const bool inc = isIncrementOp(b.m_op);
    const std::string builtin = inc ? "add" : "sub";
    const std::string asg = inc ? " += 1" : " -= 1";
    // Пользовательский operator++/operator-- (semantic выставил lhsType/rhsType типа-операнда
    // и m_overflowCheck=false): эмитим обычный C++ ++/--, без арифметики +1 и overflow-обёртки.
    const TypeId operandType = (b.lhsType.getTypeId() != INVALID_TYPE_ID) ? b.lhsType.getTypeId() : b.rhsType.getTypeId();
    if (operandType != INVALID_TYPE_ID) {
        const auto& reg = m_ectx.m_ctx.readTypes();
        const TypeId canonical = reg.store().desugaredTypeId(operandType);
        const bool userType = canonical != INVALID_TYPE_ID &&
                              (reg.store().isRecordType(canonical) || reg.store().isNativeClassType(canonical) || reg.store().isNativeTemplateType(canonical));
        if (userType) {
            if (postfix) {
                m_driver.emitExpr(target);
                m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, inc ? "++" : "--");
            } else {
                m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, inc ? "++" : "--");
                m_driver.emitExpr(target);
            }
            return;
        }
    }
    // Признак контролируемой арифметики владеет СЕМАНТИКА (typeBinaryResult -> m_overflowCheck).
    EXPECT(b.m_overflowCheck.has_value() && "overflow-check flag must be resolved by semantic");
    const bool checked = !m_ectx.m_mode.inDefaultArg && m_ectx.m_ctx.opts().is_enabled(transpiler::FlagKind::OverflowCheck) && *b.m_overflowCheck;

    if (!checked) {
        if (!postfix) {
            m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, "(");
            m_driver.emitExpr(target);
            m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, asg + ")");
        } else {
            m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, "([&]{ auto __t = ");
            m_driver.emitExpr(target);
            m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, "; ");
            m_driver.emitExpr(target);
            m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, asg + "; return __t; }())");
        }
        return;
    }

    m_driver.m_type.recordRequiredInclude("@trust/interrupt.hpp");
    m_driver.m_type.recordRequiredInclude("@trust/assert.hpp");
    const SourceLocation loc = sourceLocation(m_ectx.m_ctx.source(), b.range());
    const std::string msgPrefix = (loc.line > 0 && !loc.file.empty())
                                      ? "trust::formatMessage(\"" + utils::escapeCppString(loc.file) + "\", " + std::to_string(loc.line) + ", "
                                      : std::string("std::string(");
    const std::string msg = msgPrefix + "\"integer overflow in '" + std::string(binaryOpSpelling(b.m_op)) + "'\")";
    if (!postfix) {
        m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, "(__builtin_" + builtin + "_overflow(");
        m_driver.emitExpr(target);
        m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, ", 1, &");
        m_driver.emitExpr(target);
        m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, ") ? throw trust::IntMinus(" + msg + ") : ");
        m_driver.emitExpr(target);
        m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, ")");
    } else {
        m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, "([&]{ auto __t = ");
        m_driver.emitExpr(target);
        m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, "; if (__builtin_" + builtin + "_overflow(");
        m_driver.emitExpr(target);
        m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, ", 1, &");
        m_driver.emitExpr(target);
        m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, ")) throw trust::IntMinus(" + msg + "); return __t; }())");
    }
}

void ExprEmitter::visit_AssignOp(const Binary& n) {
    emitBinaryStmtOrExpr(n);
}

// AppendStmt (`X []= v`) - append к контейнеру: `X.push_back(v)`. X - простой контейнер
// (Ident; вложенный LHS отклонён семантикой как «не реализовано»). Ветка кодогенерации
// выбирается по каноническому TypeId контейнера (не по C++-имени): алиасы резолвятся явно
// (String → StrChar, Dictionary → Dict). trust::Dict → push_back(TypedValue), строка →
// push_back/append. Значение RHS - точно таким типом, как хранит контейнер.
void ExprEmitter::visit_AppendStmt(const Binary& n) {
    if (!n.m_left || !n.m_right) {
        m_driver.emitPlaceholderExpr(m_ectx.m_output.out);
        return;
    }
    // emitTypeName резолвит C++-имя и записывает инклуды контейнера (сайд-эффект); сам
    // результат не нужен - ветка кодогенерации выбирается по каноническому TypeId ниже.
    if (!m_driver.m_type.emitTypeName(n.lhsType.getTypeId())) {
        m_ectx.m_ctx.report(n.range(), diag::DiagId::ParseError, "unable to resolve container type for append '[]='");
        return;
    }
    const TypeReadModel& reg = m_ectx.m_ctx.readTypes();
    const TypeId cid = reg.store().desugaredTypeId(n.lhsType.getTypeId());
    const TypeId dictId = reg.getType(type::Dict);
    const TypeId strCharId = reg.getType(type::StrChar);
    const TypeId strWideId = reg.getType(type::StrWide);

    m_driver.emitExpr(n.m_left.get());
    if (cid == dictId) {
        if (n.m_right && n.m_right->kind() == ParserToken::Kind::Ellipsis) {
            // Spread-merge `X []= ... dict` → `X.extend(dict)`: добор ВСЕХ элементов словаря-
            // операнда (аналог extend/update). Операнд - m_body[0] узла Ellipsis (литерал
            // → trust::Dict{...}, переменная → c_d2, выражение → его значение).
            m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, ".extend(");
            const auto& ell = static_cast<const Sequence&>(*n.m_right);
            if (!ell.m_body.empty() && ell.m_body[0]) {
                m_driver.emitExpr(ell.m_body[0].get());
            } else {
                m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, "{}");
            }
            m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, ")");
            return;
        }
        // Dict: только push_back(name, value); безымянный append - пустое имя (позиционный элемент).
        m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, ".push_back(\"\", ");
        emitTypedDictValue(n.m_right.get(), n.resultType);
        m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, ")");
    } else if (cid == strCharId || cid == strWideId) {
        const bool wide = (cid == strWideId);
        m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, ".append(");
        if (wide && n.m_right && n.m_right->kind() == ParserToken::Kind::StrChar) {
            // Узкий строковый литерал "c" в wide-контейнер → wide-литерал L"c" (char→wchar).
            // Источник - StrChar (двойные кавычки): сырая " невозможна, экранируем страховочно
            // (см. visit_StrChar).
            m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, std::format("L\"{}\"", escapeCppStringBody(requiredText(*n.m_right))));
        } else {
            m_driver.emitExpr(n.m_right.get());
        }
        m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, ")");
    } else {
        m_ectx.m_ctx.report(n.range(), diag::DiagId::ParseError, "append '[]=' is not supported for container type '{}'", reg.printer().getTypeName(cid));
    }
}

void ExprEmitter::visit_MathOp(const Binary& n) {
    emitBinaryStmtOrExpr(n);
}

void ExprEmitter::visit_BitwiseOp(const Binary& n) {
    emitBinaryStmtOrExpr(n);
}

void ExprEmitter::visit_CompareOp(const Binary& n) {
    emitBinaryStmtOrExpr(n);
}

void ExprEmitter::visit_LogicalOp(const Binary& n) {
    emitBinaryStmtOrExpr(n);
}

void ExprEmitter::visit_IncDecOp(const Binary& n) {
    emitBinaryStmtOrExpr(n);
}

} // namespace trust
