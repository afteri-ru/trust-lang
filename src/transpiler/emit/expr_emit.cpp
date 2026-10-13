// src/transpiler/emit/expr_emit.cpp
// ExprEmitter: Базовые бинарные операции: диспетчеризация, приведение операндов, переполняемая
// арифметика верхнего уровня, type-check-операторы.

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

#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace trust {

void ExprEmitter::emitCheckedDiv(const Binary& binary_node, bool compoundAssign) {
    // Целочисленное деление/остаток: проверка нулевого делителя и `INT_MIN / -1` (UB) -> IntMinus.
    // Проверка безусловна (не зависит от -foverflow-check): деление на ноль не «wrap-арифметика».
    const bool remainder = binary_node.m_op == BinaryOp::Mod || binary_node.m_op == BinaryOp::ModAssign;
    const std::string langOp = remainder ? (compoundAssign ? "%=" : "%") : (compoundAssign ? "//=" : "//");
    m_driver.m_type.recordRequiredInclude("@trust/interrupt.hpp");
    m_driver.m_type.recordRequiredInclude("@trust/assert.hpp");
    if (compoundAssign) {
        m_driver.emitExpr(binary_node.m_left.get());
        m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, " = ([&]{ int64_t __l = static_cast<int64_t>(");
    } else {
        m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, "([&]{ int64_t __l = static_cast<int64_t>(");
    }
    m_driver.emitExpr(binary_node.m_left.get());
    m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, "); int64_t __r = static_cast<int64_t>(");
    m_driver.emitExpr(binary_node.m_right.get());
    const SourceLocation loc = sourceLocation(m_ectx.m_ctx.source(), binary_node.range());
    const std::string msgPrefix = (loc.line > 0 && !loc.file.empty())
                                      ? "trust::formatMessage(\"" + utils::escapeCppString(loc.file) + "\", " + std::to_string(loc.line) + ", "
                                      : std::string("std::string(");
    const std::string zeroMsg = msgPrefix + (remainder ? "\"integer remainder by zero\")" : "\"integer division by zero\")");
    const std::string ovMsg = msgPrefix + "\"integer overflow in '" + langOp + "'\")";
    m_ectx.m_ctx.source().output_append(
        m_ectx.m_output.out, "); if (__r == 0) throw trust::IntMinus(" + zeroMsg +
                                 "); "
                                 "if (__r == -1) { int64_t __q = 0; if (__builtin_sub_overflow(static_cast<int64_t>(0), __l, &__q)) throw trust::IntMinus(" +
                                 ovMsg + "); return " + (remainder ? std::string("static_cast<int64_t>(0)") : std::string("__q")) + "; } return __l " +
                                 (remainder ? "%" : "/") + " __r; }())");
}

void ExprEmitter::emitBinaryOpRaw(const Binary& binary_node) {
    const auto op = binaryOpSpelling(binary_node.m_op);
    // Инкремент/декремент (`++`/`--`) - унарная мутация-выражение: своя эмиссия.
    if (isIncDecOp(binary_node.m_op)) {
        emitIncDec(binary_node);
        return;
    }
    // Оператор сравнения типов (`<~`/`~~`/`~~~`): статически свёрнут семантикой в константу.
    if (isTypeCheckOp(binary_node.m_op)) {
        emitTypeCheckOp(binary_node);
        return;
    }
    // Перегруженный пользовательский оператор либо СВОБОДНЫЙ оператор модуля: семантика выбрала
    // конкретную сигнатуру/объявление - эмитим explicit member/free вызов с приведением операндов
    // (C++ не должен выбирать; свободный оператор к тому же живёт в namespace модуля).
    const bool isolatedOperator = binary_node.resolvedOperatorModule.isValid() && !m_ectx.moduleNamespace(binary_node.resolvedOperatorModule).empty();
    if (binary_node.resolvedOperatorSignature != INVALID_TYPE_ID || isolatedOperator) {
        emitOperatorDispatch(binary_node);
        return;
    }
    // Swap `x :=: y` / `var :=: _`: интринсик обмена/перемещения.
    //   `a :=: b` → std::swap(c_a, c_b) (совместимые типы, не обязательно ссылки);
    //   `var :=: _` → std::move(c_var) (перемещение значения в discard).
    if (isSwapOp(binary_node.m_op)) {
        const bool moveDiscard = binary_node.m_right && binary_node.m_right->kind() == ParserToken::Kind::Ident && requiredText(*binary_node.m_right) == "_";
        if (moveDiscard) {
            // `x :=: _` - перемещение значения в discard. МАТЕРИАЛИЗУЕМ временное: move-ctor
            // вызывается РЕАЛЬНО, временное разрушается в конце блока. Для владеющих ссылок
            // (Unique/Shared) это освобождает ресурс, x остаётся moved-from; для тривиальных
            // типов результат по значению не меняется. Блок - немедленное разрушение (не конец
            // функции); имя уникально (__trust_discard_N). Форма `std::move(x);` без
            // материализации - no-op (отбрасываемый xvalue не вызывает move).
            const std::string tmp = "__trust_discard_" + std::to_string(m_discardCounter++);
            m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, "{ auto " + tmp + " = std::move(");
            m_driver.emitExpr(binary_node.m_left.get());
            m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, "); (void)" + tmp + "; }");
        } else {
            m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, "std::swap(");
            m_driver.emitExpr(binary_node.m_left.get());
            m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, ", ");
            m_driver.emitExpr(binary_node.m_right.get());
            m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, ")");
        }
        return;
    }
    // Детекция целочисленного переполнения (опция -foverflow-check, default on): признак
    // контролируемой арифметики решён семантикой (Binary::m_overflowCheck). На переполнение -
    // runtime trust::IntMinus (ловится try/{-...-}).
    if (overflowCheckArith(binary_node)) {
        emitCheckedArith(binary_node);
        return;
    }
    // Целочисленное деление/остаток — проверка нулевого делителя (единый классификатор).
    // `//=`/`%=` — только statement (присваивание); как вложенное выражение не используется.
    if (runtimeCheckOf(binary_node) == RuntimeCheckKind::kDivZero && (!isCompoundAssignOp(binary_node.m_op) || m_ectx.m_mode.exprDepth == 0)) {
        emitCheckedDiv(binary_node, /*compoundAssign=*/isCompoundAssignOp(binary_node.m_op));
    } else {
        // LHS: для простого присвоения "=" - это адрес хранения (any_cast неприменим);
        // иначе (арифметика/составные) - значение, может требовать std::any_cast.
        const bool plainAssign = isPlainAssignOp(binary_node.m_op);
        if (binary_node.m_left) {
            if (plainAssign) {
                m_driver.emitExpr(binary_node.m_left.get());
            } else {
                emitBinaryOperand(binary_node.m_left.get(), binary_node.lhsType.getTypeId(), binary_node.commonType.getTypeId());
            }
        }
        m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, " " + std::string(op) + " ");
        if (binary_node.m_right) {
            emitBinaryOperand(binary_node.m_right.get(), binary_node.rhsType.getTypeId(), binary_node.commonType.getTypeId());
        }
    }
}

void ExprEmitter::emitBinaryOperand(const AstNodeBase* operand, TypeId operandType, TypeId castType) {
    // std::any-операнд: привести к конкретному типу (commonType). Элемент словаря возвращает
    // trust::TypedValue → типизированный доступ .getAs<Cpp>() (fast-path variant); переменная
    // типа std::any → std::any_cast<Cpp>.
    if (operand && operandType != INVALID_TYPE_ID && castType != INVALID_TYPE_ID) {
        if (isAnyType(operandType, m_ectx.m_ctx.readTypes())) {
            auto cpp = m_driver.m_type.emitTypeName(castType);
            if (cpp) {
                const bool dictElement = operand->kind() == ParserToken::Kind::MemberAccess || operand->kind() == ParserToken::Kind::ArrayAccess;
                if (dictElement) {
                    m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, "(");
                    m_driver.emitExpr(operand);
                    m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, ").getAs<" + *cpp + ">()");
                } else {
                    // std::any → универсальный runtime-конвертер anyToInt64. Точный
                    // `std::any_cast<Cpp>` ломался на гетерогенных значениях словаря (bool, int8,
                    // int64...), а конвертер принимает любую числовую/bool-категорию (dict.hpp).
                    // anyToInt64 требует <any> - подключаем по-типу (тип операнда Any).
                    m_driver.m_type.recordUsedType(operandType);
                    m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, "trust::detail::anyToInt64(");
                    m_driver.emitExpr(operand);
                    m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, ")");
                }
                return;
            }
        }
    }
    m_driver.emitExpr(operand);
}

void ExprEmitter::emitCastOperand(const AstNodeBase* operand, TypeId paramType) {
    EXPECT(operand != nullptr && "operator dispatch: missing operand");
    auto cpp = m_driver.m_type.emitTypeName(paramType);
    EXPECT(cpp && !cpp->empty() && "operator dispatch: cannot emit the declared parameter type");
    m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, "static_cast<" + *cpp + ">(");
    m_driver.emitExpr(operand);
    m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, ")");
}

void ExprEmitter::emitOperatorDispatch(const Binary& binary_node) {
    const std::string_view symbol = binaryOpSpelling(binary_node.m_op);
    // Цели приведений материализованы СЕМАНТИКОЙ (argCasts): форма member - 1 (правый
    // операнд), free - 2 (левый, правый). Неперегруженный свободный оператор кастов не несёт:
    // в его namespace ровно один кандидат, приведение не требуется.
    if (binary_node.resolvedOperatorIsMember) {
        const std::string cppOp = op::operatorCppName(symbol);
        EXPECT(!cppOp.empty() && "operator dispatch: unknown operator symbol");
        EXPECT(binary_node.argCasts.size() == 1 && "member operator dispatch: expected one materialized operand cast");
        m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, "((");
        m_driver.emitExpr(binary_node.m_left.get());
        m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, ")." + cppOp + "(");
        emitCastOperand(binary_node.m_right.get(), binary_node.argCasts[0]);
        m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, "))");
        return;
    }
    const std::string cppOp = m_ectx.operatorCppName(symbol, binary_node.resolvedOperatorNamespace, binary_node.resolvedOperatorModule);
    EXPECT(!cppOp.empty() && "operator dispatch: unknown operator symbol");
    m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, "(" + cppOp + "(");
    if (binary_node.argCasts.size() == 2) {
        emitCastOperand(binary_node.m_left.get(), binary_node.argCasts[0]);
        m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, ", ");
        emitCastOperand(binary_node.m_right.get(), binary_node.argCasts[1]);
    } else {
        EXPECT(binary_node.argCasts.empty() && "free operator dispatch: expected 0 or 2 materialized operand casts");
        m_driver.emitExpr(binary_node.m_left.get());
        m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, ", ");
        m_driver.emitExpr(binary_node.m_right.get());
    }
    m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, "))");
}

void ExprEmitter::emitBinaryStmtOrExpr(const Binary& binary_node) {
    // statement-root (m_ectx.m_mode.exprDepth==0, как ребёнок SemicolonStmt): текст без внешних скобок;
    // маппинг и ';' добавляет SemicolonStmt. Вложенное выражение (m_ectx.m_mode.exprDepth>0): '(lhs op rhs)'.
    const bool stmt = (m_ectx.m_mode.exprDepth == 0);
    if (stmt) {
        emitBinaryOpRaw(binary_node);
    } else {
        m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, "(");
        emitBinaryOpRaw(binary_node);
        m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, ")");
    }
}

// Оператор сравнения типов (`<~`/`~~`/`~~~`): результат свёрнут семантикой в константу
// (is-a/структура/тождество по реестру). При ошибке статической проверки семантика заменила
// узел на ErrorExpr (analyzeNodeTail) — сюда он не доходит; на всякий случай ничего не эмитим
// (диагностика НЕ дублируется).
void ExprEmitter::emitTypeCheckOp(const Binary& n) {
    if (n.m_typeCheckConst.has_value()) {
        m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, *n.m_typeCheckConst ? "true" : "false");
    }
}

void ExprEmitter::visit_Attr(const Sequence& node) {
    // Атрибут привязан к узлу-владельцу и эмитится его хелперами; автономного вывода нет.
    skipNode(node);
}

void ExprEmitter::visit_ArgNode(const ArgNode& node) {
    // Значения аргументов эмитит emitCallArgValues; автономный ArgNode не выводится.
    skipNode(node);
}

} // namespace trust
