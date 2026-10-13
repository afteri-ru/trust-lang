// src/semantic/typer/binary_typer.cpp
// Типизация бинарных узлов, append-присваивания и trust-check выражений; фасад - `ExprTyper`.
#include "types/type_read.hpp"
#include "semantic/core/type_predicates.hpp"
#include "semantic/typer/binary_typer.hpp"
#include "semantic/core/name_resolution.hpp"
#include "semantic/typer/type_inference.hpp"
#include "semantic/typer/operator_resolve.hpp"
#include "semantic/diag.hpp"
#include "ast/binary_op.hpp"
#include "ast/token.hpp"
#include "symbols/symbol_table.hpp"
#include "diag/diag.hpp"
#include "types/group.hpp"
#include "types/call.hpp"
#include "types/type_system.hpp"
#include "types/type_id.hpp"
#include "types/names.hpp"
#include "types/typekind.hpp"
#include "utils/strings.hpp"
#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace trust {

namespace {

/// Рекурсивно собирает bare-имена операндов захватов `*ref` (RefTakeExpr) в поддереве выражения.
void collectCaptureNames(const AstNodeBase* node, std::vector<std::string>& out) {
    if (node == nullptr) {
        return;
    }
    if (node->kind() == ParserToken::Kind::RefTakeExpr) {
        const auto& seq = static_cast<const Sequence&>(*node);
        if (!seq.m_body.empty() && seq.m_body[0] != nullptr && seq.m_body[0]->kind() == ParserToken::Kind::Ident) {
            std::string name(requiredText(*seq.m_body[0]));
            if (!name.empty() && name.front() == '$') {
                name.erase(0, 1);
            }
            out.push_back(std::move(name));
        }
    }
    for (const auto& child : node->children()) {
        collectCaptureNames(child.get(), out);
    }
}

/// Знаковое машинное целое по КАНОНИЧЕСКОМУ типу (Int8/16/32/64) - единственный класс, для которого
/// возможна детекция переполнения (-foverflow-check). Результат классификации семантика пишет в
/// `Binary::m_overflowCheck`; кодоген признак только читает.
bool isSignedMachineInt(const TypeReadModel& reg, TypeId t) {
    if (t == INVALID_TYPE_ID) {
        return false;
    }
    return getGroup(getTypeKind(reg.store().desugaredTypeId(t))) == Group::kIntegers;
}

} // namespace

void BinaryTyper::checkDoubleCapture(const Binary& b) {
    // Один и тот же объект захвачен `*ref` несколько раз в ОДНОМ выражении. Для синхронизированных
    // ссылок повторный ЭКСКЛЮЗИВНЫЙ захват того же объекта - самоблокировка (shared_timed_mutex
    // не рекурсивен). Однократный захват на несколько действий - через `with`.
    std::vector<std::string> names;
    for (const auto& child : b.children()) {
        collectCaptureNames(child.get(), names);
    }
    for (size_t i = 0; i < names.size(); ++i) {
        for (size_t j = i + 1; j < names.size(); ++j) {
            if (names[i] == names[j]) {
                m_actx.ctx().report(b.range(), semantic::DiagId::DoubleCapture,
                                    "reference '{}' is captured ('*{}') more than once in one expression; for synchronized references this is a self-deadlock",
                                    names[i], names[i]);
                return;
            }
        }
    }
}

TypeId BinaryTyper::typeBinaryResult(Binary& b) {
    // Унарный минус (MathOp "-" без левого операнда): тип результата - продвинутый тип операнда
    // (знаковый Int -> Int32/Int64, unsigned -> UInt32/UInt64, float - как есть). Не-числовой
    // операнд - ошибка.
    if (!b.m_left && b.m_right && b.m_op == BinaryOp::Sub) {
        return typeUnaryMinus(b);
    }
    // Инкремент/декремент `++`/`--` (унарный): операнд в m_left (постфикс) ИЛИ m_right (префикс).
    // Результат - тип операнда (цель мутируется на ±1); контроль переполнения - как у `+=`/`-=`.
    if (isIncDecOp(b.m_op)) {
        return typeIncDec(b);
    }
    if (!b.m_left || !b.m_right) {
        b.lhsType = b.rhsType = b.resultType = b.commonType = QualType{};
        m_actx.facts().setExprType(&b, INVALID_TYPE_ID);
        return INVALID_TYPE_ID;
    }
    // Чистая запись `x = <expr>` (AssignOp "=" с LHS-Ident): LHS - ЦЕЛЬ перезаписи, текущее
    // значение НЕ читается → тип цели берём из символа напрямую (НЕ через exprType, чтобы не
    // дать ложного «чтения неинициализированной»: `x := _; x = 5;` валидно - цель перезаписывается).
    // Составные `+=`/`op=` и остальные бинарные операции LHS читают (нужно текущее значение) →
    // exprType (если цель неинициализирована - это ошибка чтения до инициализации).
    TypeId lt = INVALID_TYPE_ID;
    const bool plainIdentAssign =
        (b.kind() == ParserToken::Kind::AssignOp && isPlainAssignOp(b.m_op) && b.m_left && b.m_left->kind() == ParserToken::Kind::Ident);
    if (plainIdentAssign) {
        if (const Symbol* s = m_actx.symbols().resolve(requiredText(*b.m_left))) {
            lt = s->type.getTypeId();
        }
    } else {
        lt = m_actx.facts().exprType(*b.m_left);
    }
    const TypeId rt = m_actx.facts().exprType(*b.m_right);
    const TypeReadModel& reg = m_actx.ctx().types();

    // Операторы сравнения типов (`<~`/`~~`/`~~~`) - отдельная зона: результат Bool,
    // статическая свёртка по реестру (наследование/поля), без арифметики/enum/ref-правил.
    if (isTypeCheckOp(b.m_op)) {
        return typeCheckBinaryResult(b, lt, rt);
    }

    // Swap `:=:` - интринсик обмена/перемещения. `a :=: b` → std::swap(a, b) и возвращает &a
    // (типы должны быть совместимы, не обязательно ссылки); `var :=: _` → std::move(var)
    // (перемещение значения в discard). Результат - тип левого операнда.
    if (isSwapOp(b.m_op)) {
        return typeSwap(b, lt, rt, reg);
    }

    // Операторы СРАВНЕНИЯ над пользовательскими типами (record/native class): требуется ОБЪЯВЛЕННЫЙ
    // оператор, тип результата = его возвращаемый тип. Без этой проверки `a == b` молча типизировалось
    // Bool и падало уже в C++ (нарушение «нет fallback для невалидных данных», AGENTS п.5).
    if (b.kind() == ParserToken::Kind::CompareOp && (semantic::isUserOperatorType(reg, lt) || semantic::isUserOperatorType(reg, rt))) {
        return typeUserComparison(b, lt, rt);
    }

    // Ссылочные типы НЕ участвуют в арифметике и сравнении (в языке отсутствует класс ссылочной
    // арифметики; сырые указатели - только для интеграции с C++). Использование незахваченной
    // ссылки в выражении как значения - нарушение контракта: ошибка + fixit `*<name>` (локер).
    if (b.kind() == ParserToken::Kind::MathOp || b.kind() == ParserToken::Kind::CompareOp) {
        checkDoubleCapture(b);
        const bool lRef = isRefTypeId(lt);
        const bool rRef = isRefTypeId(rt);
        if (lRef || rRef) {
            const AstNodeBase* bad = lRef ? b.m_left.get() : b.m_right.get();
            const char* opname = (b.kind() == ParserToken::Kind::MathOp) ? "arithmetic" : "comparison";
            auto* entry = m_actx.ctx().diag().report(
                Severity::Error, b.range(), "{} '{}' on a reference is not allowed (no pointer/reference arithmetic); use '*{}' (locker) to access the value",
                opname, binaryOpSpelling(b.m_op), bad ? requiredText(*bad) : "");
            if (entry != nullptr && bad && !bad->range().isInvalid()) {
                m_actx.ctx().diag().fixit(entry, bad->range(), "*" + std::string(requiredText(*bad)));
            }
            b.lhsType = QualType::fromTypeId(lt);
            b.rhsType = QualType::fromTypeId(rt);
            b.resultType = b.commonType = QualType{};
            m_actx.facts().setExprType(&b, INVALID_TYPE_ID);
            return INVALID_TYPE_ID;
        }
    }

    // Типобезопасность enum: сравнение (<,>,<=,>=,==,!=) допустимо только между однотипными
    // enum; неявное приведение enum к его типу значений или иному типу запрещено (работа
    // с enum идёт ТОЛЬКО через имя типа - осознанное решение, см. MEMORY.md).
    if (b.kind() == ParserToken::Kind::CompareOp) {
        const bool lEnum = isEnumType(lt, reg);
        const bool rEnum = isEnumType(rt, reg);
        if (lEnum || rEnum) {
            const TypeId lc = (lt != INVALID_TYPE_ID) ? reg.store().desugaredTypeId(lt) : INVALID_TYPE_ID;
            const TypeId rc = (rt != INVALID_TYPE_ID) ? reg.store().desugaredTypeId(rt) : INVALID_TYPE_ID;
            if (!(lEnum && rEnum && lc == rc)) {
                m_actx.ctx().diag().report(Severity::Error, b.range(), "type-safe enum comparison requires both operands to be the same enum type");
                b.resultType = b.commonType = QualType{};
                m_actx.facts().setExprType(&b, INVALID_TYPE_ID);
                return INVALID_TYPE_ID;
            }
        }
    }

    // Тензорная арифметика/присваивание - отдельный обработчик. Все виды операций с тензорным
    // операндом разрешаются здесь; общий путь для них недостижим.
    if (reg.store().isTensorType(lt) || reg.store().isTensorType(rt)) {
        return typeTensorBinary(b, lt, rt, reg);
    }

    // Bool в арифметике запрещён: продвижения/расширения Bool НЕТ. Bool возникает только из
    // явного `:Bool` (в т.ч. DSL `true`/`false` = `1:Bool`/`0:Bool`), поэтому `+ - *` и составные
    // `+= …`/`++`/`--` с Bool-операндом - ошибка компиляции (нельзя привести к числу). Буквальные
    // 0/1 - Int8 (не Bool). Compare/Logical и простое присваивание '=' не затрагиваются.
    const bool arithmetic = !(b.kind() == ParserToken::Kind::CompareOp || b.kind() == ParserToken::Kind::LogicalOp) &&
                            !(b.kind() == ParserToken::Kind::AssignOp && isPlainAssignOp(b.m_op));
    if (arithmetic) {
        const TypeId boolT = reg.store().desugaredTypeId(reg.getType(type::Bool));
        auto rejectBool = [&](const AstNodeBase* operand, TypeId t) {
            if (operand != nullptr && t != INVALID_TYPE_ID && reg.store().desugaredTypeId(t) == boolT) {
                m_actx.ctx().diag().report(Severity::Error, operand->range(),
                                           "cannot use Bool value in arithmetic '{}'; use an explicit integer type or a cast", binaryOpSpelling(b.m_op));
            }
        };
        rejectBool(b.m_left.get(), lt);
        rejectBool(b.m_right.get(), rt);
    }

    // BigInteger/Rational (kArbitraryPrecision): смешивание с float (Numbers) или
    // целочисленное деление `//` - ошибки (нужен явный каст / не поддерживается).
    if (arithmetic) {
        const TypeId lc = (lt != INVALID_TYPE_ID) ? reg.store().desugaredTypeId(lt) : INVALID_TYPE_ID;
        const TypeId rc = (rt != INVALID_TYPE_ID) ? reg.store().desugaredTypeId(rt) : INVALID_TYPE_ID;
        const Group lg = (lc != INVALID_TYPE_ID) ? getGroup(getTypeKind(lc)) : Group::kAny;
        const Group rg = (rc != INVALID_TYPE_ID) ? getGroup(getTypeKind(rc)) : Group::kAny;
        const bool lAP = lg == Group::kArbitraryPrecision;
        const bool rAP = rg == Group::kArbitraryPrecision;
        if (lAP || rAP) {
            if (isIntDivOp(b.m_op)) {
                m_actx.ctx().diag().report(Severity::Error, b.range(), "integer division '{}' is not supported for BigInteger/Rational",
                                           binaryOpSpelling(b.m_op));
            } else if ((lAP && rg == Group::kNumbers) || (rAP && lg == Group::kNumbers)) {
                m_actx.ctx().diag().report(Severity::Error, b.range(), "cannot mix BigInteger/Rational with a floating-point number without an explicit cast");
            }
        }
    }

    // Простое присвоение "=" → тип RHS; составное/арифметика → тип результата (lhs op rhs).
    const TypeId result = (b.kind() == ParserToken::Kind::AssignOp && isPlainAssignOp(b.m_op)) ? rt : resultTypeBinary(b.kind(), b.m_op, lt, rt, reg);
    b.lhsType = QualType::fromTypeId(lt);
    b.rhsType = QualType::fromTypeId(rt);
    b.resultType = QualType::fromTypeId(result);
    b.commonType = QualType::fromTypeId(result);
    // Классификация контролируемой арифметики (+,-,* и +=,-=,*=): знаковые машинные операнды и
    // результат. Семантика здесь РЕШАЕТ, нужна ли детекция переполнения (-foverflow-check), и
    // кладёт готовый признак в Binary::m_overflowCheck; кодоген его только читает. unsigned (wrap),
    // BigInteger/Rational, не-целые → false; вне класса операций → nullopt.
    if (isOverflowCheckableOp(b.m_op)) {
        b.m_overflowCheck = isSignedMachineInt(reg, lt) && isSignedMachineInt(reg, rt) && isSignedMachineInt(reg, result);
    }
    // Присваивание в переменную доверенного типа: помечаем узел ссылкой на декларацию типа
    // (источник trust-условий для проверки после присваивания; переживает таблицу символов).
    if (b.kind() == ParserToken::Kind::AssignOp) {
        b.m_typeDecl = m_actx.types().trustTypeDeclOf(lt);
    }
    // Общий тип операндов для any_cast: арифметика → result; Compare/Logical (результат Bool)
    // → продвинутый конкретный операнд, если ровно один операнд std::any.
    if (b.kind() == ParserToken::Kind::CompareOp || b.kind() == ParserToken::Kind::LogicalOp) {
        if (isAnyType(lt, reg)) {
            b.commonType = QualType::fromTypeId(promoteSingleNumeric(reg, rt));
        } else if (isAnyType(rt, reg)) {
            b.commonType = QualType::fromTypeId(promoteSingleNumeric(reg, lt));
        } else {
            b.commonType = QualType{}; // оба конкретные - any_cast не нужен
        }
    }
    m_actx.facts().setExprType(&b, result);
    return result;
}

// -- Обработчики семейств typeBinaryResult ------------------------------------

// Сравнение с объявленным operator==/... для record/native class. Guard (наличие
// пользовательского операнда) - в диспетчере; разрешение объявленного оператора и
// диагностика - в semantic::resolveUserOperator. Здесь только применение к узлу.
TypeId BinaryTyper::typeUserComparison(Binary& b, TypeId lt, TypeId rt) {
    const std::array<TypeId, 2> operands{lt, rt};
    const auto resolved = semantic::resolveUserOperator(m_actx, binaryOpSpelling(b.m_op), operands, /*postfix=*/false, b.range());
    if (!resolved.has_value()) {
        b.lhsType = b.rhsType = b.resultType = b.commonType = QualType{};
        m_actx.facts().setExprType(&b, INVALID_TYPE_ID);
        return INVALID_TYPE_ID;
    }
    if (resolved->overloaded) {
        b.resolvedOperatorSignature = resolved->signature;
        b.resolvedOperatorIsMember = resolved->member;
    }
    b.resolvedOperatorModule = resolved->module;
    b.resolvedOperatorNamespace = resolved->namespacePath;
    b.lhsType = QualType::fromTypeId(lt);
    b.rhsType = QualType::fromTypeId(rt);
    b.resultType = b.commonType = QualType::fromTypeId(resolved->returnType);
    m_actx.facts().setExprType(&b, resolved->returnType);
    return resolved->returnType;
}

// Тензорная арифметика/присваивание. Вызывается только при тензорном операнде.
// tensor ⊗ tensor (одинаковый dtype, broadcast-совместимая форма) и tensor ⊗ скаляр
// (числовой тип): результат - тип тензорного операнда, overflow-check не применяется
// (бэкенд считает сам). Сравнения тензоров не реализованы - явная диагностика.
TypeId BinaryTyper::typeTensorBinary(Binary& b, TypeId lt, TypeId rt, const TypeReadModel& reg) {
    const bool lTensor = reg.store().isTensorType(lt);
    const bool rTensor = reg.store().isTensorType(rt);
    EXPECT((lTensor || rTensor) && "typeTensorBinary requires a tensor operand");
    if (b.kind() == ParserToken::Kind::CompareOp || b.kind() == ParserToken::Kind::LogicalOp) {
        m_actx.ctx().diag().report(Severity::Error, b.range(), "tensor comparison/logical operations are not implemented");
        b.lhsType = b.rhsType = b.resultType = b.commonType = QualType{};
        m_actx.facts().setExprType(&b, INVALID_TYPE_ID);
        return INVALID_TYPE_ID;
    }
    const TypeId tensorType = lTensor ? lt : rt;
    if (b.kind() == ParserToken::Kind::MathOp || (b.kind() == ParserToken::Kind::AssignOp && !isPlainAssignOp(b.m_op))) {
        if (b.kind() == ParserToken::Kind::MathOp) {
            if (lTensor && rTensor) {
                const TypeId le = reg.store().tensorElementType(lt);
                const TypeId re = reg.store().tensorElementType(rt);
                if (reg.store().desugaredTypeId(le) != reg.store().desugaredTypeId(re)) {
                    m_actx.ctx().diag().report(Severity::Error, b.range(), "tensor operands must have the same element type");
                    b.lhsType = b.rhsType = b.resultType = b.commonType = QualType{};
                    m_actx.facts().setExprType(&b, INVALID_TYPE_ID);
                    return INVALID_TYPE_ID;
                }
                const auto& ls = reg.store().tensorDimensions(lt);
                const auto& rs = reg.store().tensorDimensions(rt);
                if (!ls.empty() && !rs.empty() && ls != rs) {
                    m_actx.ctx().diag().report(Severity::Error, b.range(),
                                               "tensor operands must have the same static shape (broadcasting with known shapes is not implemented)");
                    b.lhsType = b.rhsType = b.resultType = b.commonType = QualType{};
                    m_actx.facts().setExprType(&b, INVALID_TYPE_ID);
                    return INVALID_TYPE_ID;
                }
            } else {
                const TypeId scalar = lTensor ? rt : lt;
                const Group sg = getGroup(getTypeKind(reg.store().desugaredTypeId(scalar)));
                if (scalar == INVALID_TYPE_ID || (!isArithmeticGroup(sg) && sg != Group::kArbitraryPrecision)) {
                    m_actx.ctx().diag().report(Severity::Error, b.range(), "tensor arithmetic requires a numeric scalar operand");
                    b.lhsType = b.rhsType = b.resultType = b.commonType = QualType{};
                    m_actx.facts().setExprType(&b, INVALID_TYPE_ID);
                    return INVALID_TYPE_ID;
                }
            }
        }
        b.lhsType = QualType::fromTypeId(lt);
        b.rhsType = QualType::fromTypeId(rt);
        b.resultType = b.commonType = QualType::fromTypeId(tensorType);
        b.m_overflowCheck = false;
        m_actx.facts().setExprType(&b, tensorType);
        return tensorType;
    }
    // Простое присваивание `t = expr`: тип RHS (проверка совместимости - общая).
    if (b.kind() == ParserToken::Kind::AssignOp && isPlainAssignOp(b.m_op)) {
        b.lhsType = QualType::fromTypeId(lt);
        b.rhsType = QualType::fromTypeId(rt);
        b.resultType = b.commonType = QualType::fromTypeId(rt);
        b.m_overflowCheck = false;
        m_actx.facts().setExprType(&b, rt);
        return rt;
    }
    EXPECT(false && "unhandled binary operation with tensor operands");
    return INVALID_TYPE_ID;
}

// -- Операторы сравнения типов (<~ / ~~ / ~~~) --------------------------------
// Транзитивное «подтип-или-равно» по базовым классам реестра (с защитой от циклов).
// Абстрактная группа-цель (Data==0, кроме Any) сравнивается по совпадению группы.
bool isSubtypeOrEqual(const TypeReadModel& reg, TypeId type, TypeId target) {
    const TypeId t = (type == INVALID_TYPE_ID) ? INVALID_TYPE_ID : reg.store().identityOf(type, TypeIdentityScope::kCanonical);
    const TypeId g = (target == INVALID_TYPE_ID) ? INVALID_TYPE_ID : reg.store().identityOf(target, TypeIdentityScope::kCanonical);
    if (t == INVALID_TYPE_ID || g == INVALID_TYPE_ID) {
        return false;
    }
    if (t == g) {
        return true;
    }
    const TypeKind tk = getTypeKind(g);
    if (getData(tk) == 0 && getGroup(tk) != Group::kAny) {
        return getGroup(getTypeKind(t)) == getGroup(tk);
    }
    std::vector<TypeId> stack(reg.store().baseClasses(t).begin(), reg.store().baseClasses(t).end());
    std::vector<TypeId> seen;
    while (!stack.empty()) {
        const TypeId b = stack.back();
        stack.pop_back();
        const TypeId bc = (b == INVALID_TYPE_ID) ? INVALID_TYPE_ID : reg.store().identityOf(b, TypeIdentityScope::kCanonical);
        if (bc == INVALID_TYPE_ID || std::find(seen.begin(), seen.end(), bc) != seen.end()) {
            continue;
        }
        if (bc == g) {
            return true;
        }
        seen.push_back(bc);
        for (const TypeId nb : reg.store().baseClasses(bc)) {
            stack.push_back(nb);
        }
    }
    return false;
}

// Суммарное число полей record-типа с учётом наследования (для строгого образца).
size_t countRecordFields(const TypeReadModel& reg, TypeId type) {
    const TypeId t = (type == INVALID_TYPE_ID) ? INVALID_TYPE_ID : reg.store().identityOf(type, TypeIdentityScope::kCanonical);
    if (t == INVALID_TYPE_ID) {
        return 0;
    }
    size_t n = 0;
    if (const auto* rd = reg.store().getTypeDataAs<RecordTypeData>(t)) {
        n += rd->fields.size();
    }
    for (const TypeId base : reg.store().baseClasses(t)) {
        const TypeId bc = (base == INVALID_TYPE_ID) ? INVALID_TYPE_ID : reg.store().identityOf(base, TypeIdentityScope::kCanonical);
        if (bc != INVALID_TYPE_ID && bc != t) {
            n += countRecordFields(reg, bc);
        }
    }
    return n;
}

TypeId BinaryTyper::typeCheckBinaryResult(Binary& b, TypeId lt, TypeId rt) {
    const TypeReadModel& reg = m_actx.ctx().types();
    const TypeId boolT = reg.getType(type::Bool);
    b.lhsType = QualType::fromTypeId(lt);
    b.rhsType = QualType::fromTypeId(rt);
    b.resultType = QualType::fromTypeId(boolT);
    b.commonType = QualType{};
    b.m_typeCheckConst.reset();

    const AstNodeBase* rhs = b.m_right.get();
    // Тип ЛЕВОГО операнда: для типового операнда (`:T`) - обозначаемый тип, иначе - тип значения.
    TypeId lhsType = lt;
    if (b.m_left && b.m_left->kind() == ParserToken::Kind::TypeName) {
        if (auto r = m_actx.types().resolveTypeRef(*b.m_left)) {
            lhsType = *r;
        }
    }
    b.lhsType = QualType::fromTypeId(lhsType);
    const bool rhsTypeName = rhs != nullptr && rhs->kind() == ParserToken::Kind::TypeName;
    const bool rhsStringLit = rhs != nullptr && (rhs->kind() == ParserToken::Kind::StrWide || rhs->kind() == ParserToken::Kind::StrChar);
    const bool rhsPattern = rhs != nullptr && rhs->kind() == ParserToken::Kind::DictLiteral;
    const bool rhsIdent = rhs != nullptr && rhs->kind() == ParserToken::Kind::Ident;

    // -- Разрешение типа-цели (RHS). --
    TypeId target = INVALID_TYPE_ID;
    if (rhsTypeName) {
        if (auto r = m_actx.types().resolveTypeRef(*rhs)) {
            target = *r;
        } else {
            m_actx.ctx().diag().report(Severity::Error, b.range(), "unknown type name '{}' in type check", std::string(requiredText(*rhs)));
        }
    } else if (rhsStringLit) {
        if (auto f = m_actx.types().lookupTypeName(requiredText(*rhs))) {
            target = *f;
        } else {
            m_actx.ctx().diag().report(Severity::Error, b.range(), "unknown type name '{}' in type check", std::string(requiredText(*rhs)));
        }
    } else if (rhsIdent) {
        // Имя типа известно только в рантайме (строковая переменная) - не реализовано.
        m_actx.ctx().diag().report(Severity::Error, b.range(), "type name from a runtime variable is not implemented; use a type (':T') or a string literal");
    } else if (!rhsPattern) {
        m_actx.ctx().diag().report(Severity::Error, b.range(), "type-check operator '{}' requires a type name or a dict pattern on the right",
                                   binaryOpSpelling(b.m_op));
    }

    // -- Статика по типу-цели. --
    if (target != INVALID_TYPE_ID) {
        const bool lhsKnown = lhsType != INVALID_TYPE_ID && !isAnyType(lhsType, reg);
        if (!lhsKnown) {
            // Тип LHS не выведен/стёрт (Any): рантайм-проверка типа не реализована.
            m_actx.ctx().diag().report(Severity::Error, b.range(), "dynamic type check on a type-erased value is not implemented");
            m_actx.facts().setExprType(&b, boolT);
            return boolT;
        }
        bool result = false;
        if (b.m_op == BinaryOp::TypeStrict) {
            const TypeId lc = (lhsType != INVALID_TYPE_ID) ? reg.store().identityOf(lhsType, TypeIdentityScope::kCanonical) : INVALID_TYPE_ID;
            result = (lc != INVALID_TYPE_ID) && (lc == reg.store().identityOf(target, TypeIdentityScope::kCanonical));
        } else {
            result = isSubtypeOrEqual(reg, lhsType, target);
        }
        b.m_typeCheckConst = result;
        m_actx.facts().setExprType(&b, boolT);
        return boolT;
    }

    // -- Статика по образцу (Dict-паттерн) для record-LHS. --
    if (rhsPattern) {
        if (lhsType == INVALID_TYPE_ID || isAnyType(lhsType, reg)) {
            // Тип LHS не выведен/стёрт: динамическая структурная проверка не реализована.
            m_actx.ctx().diag().report(Severity::Error, b.range(), "dynamic structural check for type-erased values is not implemented");
            m_actx.facts().setExprType(&b, boolT);
            return boolT;
        }
        const TypeId lc = reg.store().identityOf(lhsType, TypeIdentityScope::kCanonical);
        // Собираем имена полей образца (элемент - ArgNode/Ident или Binary(AssignOp)).
        std::vector<std::string_view> patternNames;
        for (const auto& el : static_cast<const DictLiteralNode&>(*rhs).m_body) {
            if (!el) {
                continue;
            }
            std::string_view name;
            if (el->kind() == ParserToken::Kind::ArgNode || el->kind() == ParserToken::Kind::Ident) {
                name = requiredText(*el);
            } else if (el->kind() == ParserToken::Kind::AssignOp) {
                const auto& assign = static_cast<const Binary&>(*el);
                if (assign.m_left && assign.m_left->kind() == ParserToken::Kind::Ident) {
                    name = requiredText(*assign.m_left);
                }
            }
            if (!name.empty()) {
                patternNames.push_back(name);
            }
        }
        const auto* rd = reg.store().getTypeDataAs<RecordTypeData>(lc);
        if (rd == nullptr) {
            // LHS - не record (нет полей). Пустой образец `(,)` истинен только для словаря;
            // непустой образец полей у не-record не находит → ложь.
            const bool isDict = getGroup(getTypeKind(lc)) == Group::kDicts;
            b.m_typeCheckConst = patternNames.empty() && isDict;
            m_actx.facts().setExprType(&b, boolT);
            return boolT;
        }
        bool ok = true;
        for (const std::string_view name : patternNames) {
            if (!reg.store().findField(lc, name).isValid()) {
                ok = false;
                break;
            }
        }
        if (ok && b.m_op == BinaryOp::TypeStrict && patternNames.size() != countRecordFields(reg, lc)) {
            ok = false; // строгая проверка - точный набор полей
        }
        b.m_typeCheckConst = ok;
    }
    m_actx.facts().setExprType(&b, boolT);
    return boolT;
}

// AppendStmt (`X []= v`) - append к контейнеру, не обычное бинарное выражение: типы ложатся
// специально (lhsType=тип контейнера, rhsType/resultType=тип значения), сужение/расширение
// целевой переменной не применяются (append не меняет тип цели).
void BinaryTyper::typeAppendStmt(Binary& b) {
    const TypeId lt = b.m_left ? m_actx.facts().exprType(*b.m_left) : INVALID_TYPE_ID;
    const TypeId rt = b.m_right ? m_actx.facts().exprType(*b.m_right) : INVALID_TYPE_ID;
    // Spread-merge `X []= ... dict`: правая часть - маркер распаковки (Ellipsis),
    // единичным элементом НЕ является, тип результата не выводится (INVALID).
    const bool spread = b.m_right && b.m_right->kind() == ParserToken::Kind::Ellipsis;
    b.lhsType = QualType::fromTypeId(lt);
    b.rhsType = QualType::fromTypeId(spread ? INVALID_TYPE_ID : rt);
    b.resultType = QualType::fromTypeId(spread ? INVALID_TYPE_ID : rt);
    b.commonType = QualType::fromTypeId(spread ? INVALID_TYPE_ID : rt);
    // Строковые контейнеры: ширина RHS должна быть совместима с единичным элементом
    // контейнера (StrChar ↔ std::string, StrWide ↔ std::wstring). Потерянное сужение
    // (широкая строка в узкий контейнер) - ошибка; обратное (char→wide) кодогенерация
    // сама расширяет узкий литерал в wide.
    const TypeReadModel& reg = m_actx.ctx().types();
    const TypeId strChar = reg.getType(type::StrChar);
    const TypeId strWide = reg.getType(type::StrWide);
    const TypeId ltC = (lt != INVALID_TYPE_ID) ? reg.store().desugaredTypeId(lt) : INVALID_TYPE_ID;
    const TypeId rtC = (rt != INVALID_TYPE_ID) ? reg.store().desugaredTypeId(rt) : INVALID_TYPE_ID;
    if (spread) {
        // Распаковка `[]= ...` допустима только для контейнера-словаря (merge/extend).
        if (ltC != INVALID_TYPE_ID && ltC != reg.getType(type::Dict)) {
            m_actx.ctx().diag().report(Severity::Error, b.range(), "spread append '[]= ...' is only supported for a dictionary container");
        }
        // Вложенный LHS (`d['x'] []= ...`, `d[0] []= ...`, `d.field []= ...`) - отложено.
        if (b.m_left && (b.m_left->kind() == ParserToken::Kind::ArrayAccess || b.m_left->kind() == ParserToken::Kind::MemberAccess)) {
            m_actx.ctx().diag().report(Severity::Error, b.m_left->range(),
                                       "вложенный append '[]=' пока не реализован: append допустим только к простому контейнеру");
        }
        m_actx.facts().setExprType(&b, INVALID_TYPE_ID);
        return;
    }

    if (ltC == strChar && rtC == strWide) {
        m_actx.ctx().diag().report(
            Severity::Error, b.range(),
            "append '[]=': wide string cannot be appended to a narrow string container; use matching quotes (narrow '...' or wide \"...\")");
    }
    // Вложенный LHS (`d['x'] []= v`, `d[0] []= v`, `d.field []= v`) - отложено.
    if (b.m_left && (b.m_left->kind() == ParserToken::Kind::ArrayAccess || b.m_left->kind() == ParserToken::Kind::MemberAccess)) {
        m_actx.ctx().diag().report(Severity::Error, b.m_left->range(),
                                   "вложенный append '[]=' пока не реализован: append допустим только к простому контейнеру");
    }
    m_actx.facts().setExprType(&b, rt);
    return;
}

// Типизация бинарных выражений (арифметика/сравнение/присваивание) без ветки AppendStmt.
void BinaryTyper::typeBinaryNode(Binary& b) {
    // AppendStmt (`X []= v`) - append к контейнеру, отдельная ветка.
    if (b.kind() == ParserToken::Kind::AppendStmt) {
        typeAppendStmt(b);
        return;
    }
    const TypeId result = typeBinaryResult(b);
    const bool isAssignOp = (b.kind() == ParserToken::Kind::AssignOp);
    // Составное присваивание ("+=", "//=" - диапазон AddAssign..XorAssign по BinaryOp);
    // простые операторы ("//", "+") не расширяют целевую переменную. Оператор - по m_op,
    // НЕ по text() (см. include/ast/MEMORY.md).
    const bool compound = isCompoundAssignOp(b.m_op);
    // Инкремент/декремент ("++"/"--") - тоже унарная ЗАПИСЬ в цель (±1): цель - единственный
    // операнд (постфикс `x++` → m_left, префикс `++x` → m_right). Общие правила записи ниже
    // (константность/инициализация/расширение выводимого типа) применяются к `target`.
    const bool incdec = isIncDecOp(b.m_op);
    const bool writes = isAssignOp || compound || incdec;
    AstNodeBase* target = incdec ? (b.m_left ? b.m_left.get() : b.m_right.get()) : b.m_left.get();
    // Сужение в ЯВНО-типизированную цель (inferred-цели расширяются ниже). У inc/dec нет
    // RHS-значения - сужать нечего.
    if (!incdec && b.m_left && b.m_left->kind() == ParserToken::Kind::Ident) {
        if (Symbol* s = m_actx.symbols().resolveMutable(requiredText(*b.m_left))) {
            // Конструктор record-шаблона в RHS присваивания (`b = Box()`): тип-цель = тип LHS.
            if (isPlainAssignOp(b.m_op)) {
                m_actx.types().coerceTemplateCtor(b.m_right.get(), s->type.getTypeId());
            }
            // Ссылочная ЦЕЛЬ проверяется ВСЕГДА (в т.ч. inferred): контракт value-vs-reference и
            // запрет копирования unique не зависят от «выведенности» типа. Числовые inferred-цели
            // расширяет widenInferredTarget. Swap `:=:` - НЕ присваивание
            // (обмен/перемещение владения), проверяется отдельно в ветке swap.
            const bool targetIsRef = s->type.getTypeId() != INVALID_TYPE_ID && refAxisOf(getRefType(getTypeKind(s->type.getTypeId()))) != RefAxis::Value;
            if (!isSwapOp(b.m_op) && (!s->type.isInferred() || (isPlainAssignOp(b.m_op) && targetIsRef)) && (isAssignOp || compound)) {
                const TypeId assigned = isPlainAssignOp(b.m_op) ? b.rhsType.getTypeId() : result;
                checkAssignmentNarrowing(b.m_right.get(), assigned, s->type.getTypeId(), s->name);
            }
        }
    }
    // Константность (kConstFlag): запись в константную переменную - ошибка; LHS с `^`
    // (attr::ReadOnly на узле Ident) - финальная запись, делающая переменную константой
    // (became-const: `x := 42; x^ += 1;` → x неизменяема со значением 44).
    if (target != nullptr && target->kind() == ParserToken::Kind::Ident && writes) {
        if (Symbol* s = m_actx.symbols().resolveMutable(requiredText(*target))) {
            const bool makeConst = target->as_attr() && target->as_attr()->has_attr(m_actx.ctx().attrs(), attr::ReadOnly);
            if (s->type.isConst() && !makeConst) {
                // Статическая const-переменная (объявлена с '^'/readonly). Became-const
                // (`x^ = ...`) отслеживает CFG/dataflow-проход definite_assignment.
                m_actx.ctx().diag().report(Severity::Error, b.range(), "cannot assign to constant variable '{}'", s->name);
            }
            // Definite-assignment и became-const — flow-факты CFG/dataflow-прохода,
            // в QualType не хранятся.
        }
    }
    // Запись в const-ПОЛЕ (`obj.field =/+=/++ ...`): квалификатор объявленного поля сохранён
    // в QualType поля, поэтому доступ типизируется как const. Ошибка на уровне Trust (а не
    // только в C++).
    if (target != nullptr && writes && target->kind() == ParserToken::Kind::MemberAccess && m_actx.facts().exprState(*target).isConst()) {
        const auto* ma = target->as<Binary>();
        const std::string field = (ma != nullptr && ma->m_right) ? std::string(requiredText(*ma->m_right)) : std::string{};
        m_actx.ctx().diag().report(Severity::Error, b.range(), "cannot assign to constant field '{}'", field);
    }
    // Расширение выводимой цели по истории присвоений - для любых операций ЗАПИСИ
    // (AssignOp "=", составные "+=", а также "++"/"--"). Bool сюда не попадает: он в
    // числовой арифметике запрещён (см. binary_typer.cpp), поэтому `result` для него INVALID.
    if (writes) {
        widenInferredTarget(target, result);
    }
}

TypeId BinaryTyper::typeUnaryMinus(Binary& b) {
    const TypeId rt = m_actx.facts().exprType(*b.m_right);
    const TypeReadModel& reg = m_actx.ctx().types();
    const TypeId promo = (rt == INVALID_TYPE_ID) ? INVALID_TYPE_ID : promoteSingleNumeric(reg, rt);
    if (promo == INVALID_TYPE_ID) {
        m_actx.ctx().diag().report(Severity::Error, b.range(), "unary '-' requires a numeric operand");
        b.lhsType = b.rhsType = b.resultType = b.commonType = QualType{};
        m_actx.facts().setExprType(&b, INVALID_TYPE_ID);
        return INVALID_TYPE_ID;
    }
    b.lhsType = QualType::fromTypeId(rt);
    b.rhsType = QualType::fromTypeId(rt);
    b.resultType = b.commonType = QualType::fromTypeId(promo);
    // Унарный минус: детекция переполнения (-INT_MIN) возможна только для знакового машинного
    // результата. unsigned (wrap), BigInteger/Rational, float - не контролируются.
    b.m_overflowCheck = isSignedMachineInt(reg, promo);
    m_actx.facts().setExprType(&b, promo);
    return promo;
}

TypeId BinaryTyper::typeIncDec(Binary& b) {
    const AstNodeBase* opnd = b.m_left ? b.m_left.get() : b.m_right.get();
    const TypeReadModel& reg = m_actx.ctx().types();
    const TypeId ot = m_actx.facts().exprType(*opnd);
    if (ot == INVALID_TYPE_ID) {
        b.lhsType = b.rhsType = b.resultType = b.commonType = QualType{};
        m_actx.facts().setExprType(&b, INVALID_TYPE_ID);
        return INVALID_TYPE_ID;
    }
    // Пользовательский operator++/operator-- (record/native/native-template): результат -
    // возвращаемый тип объявленного оператора; numeric-проверки/overflow к нему не применяются.
    if (semantic::isUserOperatorType(reg, ot)) {
        const std::string_view sym = isIncrementOp(b.m_op) ? std::string_view("++") : std::string_view("--");
        const std::array<TypeId, 1> operands{ot};
        const auto resolved = semantic::resolveUserOperator(m_actx, sym, operands, isPostfixIncDecOp(b.m_op), b.range());
        if (!resolved.has_value()) {
            b.lhsType = b.rhsType = b.resultType = b.commonType = QualType{};
            m_actx.facts().setExprType(&b, INVALID_TYPE_ID);
            return INVALID_TYPE_ID;
        }
        b.lhsType = QualType::fromTypeId(ot);
        b.rhsType = QualType::fromTypeId(ot);
        b.resultType = b.commonType = QualType::fromTypeId(resolved->returnType);
        b.m_overflowCheck = false;
        if (resolved->overloaded) {
            b.resolvedOperatorSignature = resolved->signature;
            b.resolvedOperatorIsMember = resolved->member;
        }
        b.resolvedOperatorModule = resolved->module;
        b.resolvedOperatorNamespace = resolved->namespacePath;
        m_actx.facts().setExprType(&b, resolved->returnType);
        return resolved->returnType;
    }
    // Bool в арифметике запрещён (как и у `+=`/`-=`): `++`/`--` над Bool - ошибка компиляции.
    const bool isBool = (reg.store().desugaredTypeId(ot) == reg.store().desugaredTypeId(reg.getType(type::Bool)));
    const Group g = getGroup(getTypeKind(reg.store().desugaredTypeId(ot)));
    const bool numeric = isArithmeticGroup(g) || g == Group::kArbitraryPrecision;
    if (isBool || !numeric) {
        if (isBool) {
            m_actx.ctx().diag().report(Severity::Error, b.range(), "cannot use Bool value in arithmetic '{}'", binaryOpSpelling(b.m_op));
        } else {
            m_actx.ctx().diag().report(Severity::Error, b.range(), "operator '{}' requires a numeric operand", binaryOpSpelling(b.m_op));
        }
        b.lhsType = b.rhsType = b.resultType = b.commonType = QualType{};
        m_actx.facts().setExprType(&b, INVALID_TYPE_ID);
        return INVALID_TYPE_ID;
    }
    const TypeId result = (g == Group::kArbitraryPrecision) ? ot : promoteSingleNumeric(reg, ot);
    b.lhsType = QualType::fromTypeId(ot);
    b.rhsType = QualType::fromTypeId(ot);
    b.resultType = b.commonType = QualType::fromTypeId(result);
    // Контролируемая знаковая машинная арифметика (±1) - как у `+=`/`-=`.
    b.m_overflowCheck = isSignedMachineInt(reg, ot);
    m_actx.facts().setExprType(&b, result);
    return result;
}

TypeId BinaryTyper::typeSwap(Binary& b, TypeId lt, TypeId rt, const TypeReadModel& reg) {
    // Swap/move пишет в оба операнда: const-поле целью быть не может.
    for (const AstNodeBase* op : {b.m_left.get(), b.m_right.get()}) {
        if (op != nullptr && op->kind() == ParserToken::Kind::MemberAccess && m_actx.facts().exprState(*op).isConst()) {
            const auto* ma = op->as<Binary>();
            const std::string field = (ma != nullptr && ma->m_right) ? std::string(requiredText(*ma->m_right)) : std::string{};
            m_actx.ctx().diag().report(Severity::Error, b.range(), "cannot swap constant field '{}'", field);
        }
    }
    // Форма `var :=: _`: правая часть - идентификатор `_` (None) → std::move(var).
    const bool isMoveDiscard = b.m_right && b.m_right->kind() == ParserToken::Kind::Ident && requiredText(*b.m_right) == "_";
    if (!isMoveDiscard) {
        const TypeId lc = (lt != INVALID_TYPE_ID) ? reg.store().desugaredTypeId(lt) : INVALID_TYPE_ID;
        const TypeId rc = (rt != INVALID_TYPE_ID) ? reg.store().desugaredTypeId(rt) : INVALID_TYPE_ID;
        if (lc == INVALID_TYPE_ID || rc == INVALID_TYPE_ID || lc != rc) {
            // getTypeName даёт имя С ВИДОМ ссылки (`unique<Int32>`, `shared<Int32>`) -
            // различает unique/shared/weak/value в диагностике (`a :=: b` для unique и shared).
            const auto typeDisplay = [&](TypeId tid) {
                if (tid == INVALID_TYPE_ID) {
                    return std::string("?");
                }
                return std::string(reg.printer().getTypeName(tid));
            };
            m_actx.ctx().diag().report(Severity::Error, b.range(),
                                       "swap ':=:' requires operands of compatible types, got '{}' and '{}' (values can be swapped via '*lhs :=: *rhs')",
                                       typeDisplay(lt), typeDisplay(rt));
        }
    }
    b.lhsType = QualType::fromTypeId(lt);
    b.rhsType = QualType::fromTypeId(rt);
    b.resultType = b.commonType = QualType::fromTypeId(lt);
    m_actx.facts().setExprType(&b, lt);
    return lt;
}

} // namespace trust
