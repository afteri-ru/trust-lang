// src/semantic/assign_typer.cpp
// Типизация операторов присваивания и бинарных операций ExprTyper-а (вынесено из
// expr_typer.cpp как отдельная зона ответственности): typeBinaryResult (swap `:=:`, арифметика/
// сравнение с проверками ссылочных типов), checkDoubleCapture и вспомогательный сборщик захватов.
// Публичный API класса не меняется - методы объявлены в include/semantic/expr_typer.hpp.
#include "semantic/expr_typer.hpp"
#include "semantic/name_resolution.hpp"
#include "semantic/analysis_common.hpp"
#include "semantic/type_inference.hpp"
#include "types/overload_resolve.hpp"
#include "semantic/diag.hpp"
#include "ast/binary_op.hpp"
#include "ast/token.hpp"
#include "diag/diag.hpp"
#include "types/promotion.hpp"
#include "types/registry.hpp"
#include "types/type_id.hpp"
#include "types/group.hpp"
#include "types/typekind.hpp"
#include "utils/strings.hpp"
#include <algorithm>
#include <optional>
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
            std::string name(seq.m_body[0]->text());
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
bool isSignedMachineInt(const TypeRegistry& reg, TypeId t) {
    if (t == INVALID_TYPE_ID) {
        return false;
    }
    return getGroup(getKindFromId(reg.getCanonicalTypeId(t))) == Group::kIntegers;
}

} // namespace

void ExprTyper::checkDoubleCapture(const Binary& b) {
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

TypeId ExprTyper::typeBinaryResult(Binary& b) {
    // Унарный минус (MathOp "-" без левого операнда): тип результата - продвинутый тип операнда
    // (знаковый Int -> Int32/Int64, unsigned -> UInt32/UInt64, float - как есть). Не-числовой
    // операнд - ошибка (раньше унарный минус давал std::any).
    if (!b.m_left && b.m_right && b.m_op == BinaryOp::Sub) {
        const TypeId rt = m_actx.exprType(*b.m_right);
        const TypeRegistry& reg = m_actx.ctx().types();
        const TypeId promo = (rt == INVALID_TYPE_ID) ? INVALID_TYPE_ID : promoteSingleNumeric(reg, rt);
        if (promo == INVALID_TYPE_ID) {
            m_actx.ctx().diag().report(Severity::Error, b.range(), "unary '-' requires a numeric operand");
            b.lhsType = b.rhsType = b.resultType = b.commonType = INVALID_TYPE_ID;
            m_actx.setExprType(&b, INVALID_TYPE_ID);
            return INVALID_TYPE_ID;
        }
        b.lhsType = rt;
        b.rhsType = rt;
        b.resultType = b.commonType = promo;
        // Унарный минус: детекция переполнения (-INT_MIN) возможна только для знакового машинного
        // результата. unsigned (wrap), BigInteger/Rational, float - не контролируются.
        b.m_overflowCheck = isSignedMachineInt(reg, promo);
        m_actx.setExprType(&b, promo);
        return promo;
    }
    if (!b.m_left || !b.m_right) {
        b.lhsType = b.rhsType = b.resultType = b.commonType = INVALID_TYPE_ID;
        m_actx.setExprType(&b, INVALID_TYPE_ID);
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
        if (const Symbol* s = m_actx.symbols().resolve(b.m_left->text())) {
            lt = clearFlag(s->type, SymbolFlag::Uninit);
        }
    } else {
        lt = m_actx.exprType(*b.m_left);
    }
    const TypeId rt = m_actx.exprType(*b.m_right);
    const TypeRegistry& reg = m_actx.ctx().types();

    // Операторы сравнения типов (`<~`/`~~`/`~~~`) - отдельная зона: результат Bool,
    // статическая свёртка по реестру (наследование/поля), без арифметики/enum/ref-правил.
    if (isTypeCheckOp(b.m_op)) {
        return typeCheckBinaryResult(b, lt, rt);
    }

    // Swap `:=:` - интринсик обмена/перемещения. `a :=: b` → std::swap(a, b) и возвращает &a
    // (типы должны быть совместимы, не обязательно ссылки); `var :=: _` → std::move(var)
    // (перемещение значения в discard). Результат - тип левого операнда.
    if (isSwapOp(b.m_op)) {
        // Форма `var :=: _`: правая часть - идентификатор `_` (None) → std::move(var).
        const bool isMoveDiscard = b.m_right && b.m_right->kind() == ParserToken::Kind::Ident && b.m_right->text() == "_";
        if (!isMoveDiscard) {
            const TypeId lc = (lt != INVALID_TYPE_ID) ? reg.getCanonicalTypeId(lt) : INVALID_TYPE_ID;
            const TypeId rc = (rt != INVALID_TYPE_ID) ? reg.getCanonicalTypeId(rt) : INVALID_TYPE_ID;
            if (lc == INVALID_TYPE_ID || rc == INVALID_TYPE_ID || lc != rc) {
                // getFullTypeName даёт имя С ВИДОМ ссылки (`unique<Int32>`, `shared<Int32>`) -
                // различает unique/shared/weak/value в диагностике (`a :=: b` для unique и shared).
                const auto typeDisplay = [&](TypeId tid) {
                    if (tid == INVALID_TYPE_ID) {
                        return std::string("?");
                    }
                    return std::string(reg.getFullTypeName(tid));
                };
                m_actx.ctx().diag().report(Severity::Error, b.range(),
                                           "swap ':=:' requires operands of compatible types, got '{}' and '{}' (values can be swapped via '*lhs :=: *rhs')",
                                           typeDisplay(lt), typeDisplay(rt));
            }
        }
        b.lhsType = lt;
        b.rhsType = rt;
        b.resultType = b.commonType = lt;
        m_actx.setExprType(&b, lt);
        return lt;
    }

    // Операторы СРАВНЕНИЯ над пользовательскими типами (record/native class): требуется ОБЪЯВЛЕННЫЙ
    // оператор, тип результата = его возвращаемый тип. Без этой проверки `a == b` молча типизировалось
    // Bool и падало уже в C++ (нарушение «нет fallback для невалидных данных», AGENTS п.5).
    if (b.kind() == ParserToken::Kind::CompareOp) {
        const auto userType = [&reg](TypeId t) {
            if (t == INVALID_TYPE_ID) {
                return false;
            }
            const TypeId c = reg.getCanonicalTypeId(t);
            return c != INVALID_TYPE_ID && (reg.isRecordType(c) || reg.isNativeClassType(c));
        };
        if (userType(lt) || userType(rt)) {
            const std::string sym(b.text());
            const TypeId lc = (lt != INVALID_TYPE_ID) ? reg.getCanonicalTypeId(lt) : INVALID_TYPE_ID;
            std::optional<TypeRegistry::MethodRef> member;
            if (lc != INVALID_TYPE_ID) {
                member = reg.findMethodInfo(lc, sym);
            }
            // Свободный оператор - из ТАБЛИЦЫ СИМВОЛОВ (модульный/namespace-скоуп): member-операторы
            // в скоуп НЕ попадают (регистрируются как методы типа), поэтому resolve() не спутает
            // свободный оператор с member-оператором LHS - конфликт member+free детектируется верно.
            const Symbol* freeSym = m_actx.symbols().resolve(sym);
            const bool hasFree = freeSym != nullptr;
            if (member.has_value() && hasFree) {
                m_actx.ctx().diag().report(Severity::Error, b.range(), "operator '{}' is ambiguous: declared both as a member of '{}' and as a free function",
                                           sym, reg.getFullTypeName(lc));
                b.lhsType = b.rhsType = b.resultType = b.commonType = INVALID_TYPE_ID;
                m_actx.setExprType(&b, INVALID_TYPE_ID);
                return INVALID_TYPE_ID;
            }
            // СВОБОДНЫЙ оператор (TrustLang-функция): выбирает АНАЛИЗАТОР ВСЕГДА (в т.ч. при одной
            // сигнатуре) по типам операндов - конверсии/выбор не делегируются C++.
            TypeId freeFt = INVALID_TYPE_ID;
            if (hasFree) {
                const std::vector<TypeId> sigs = freeSym->overloads.empty() ? std::vector<TypeId>{structuralType(freeSym->type)} : freeSym->overloads;
                const std::vector<TypeId> opArgs{lt, rt};
                freeFt = resolveOverload(reg, sigs, opArgs).chosen;
            }
            // Member-оператор: нативный - C++-путь; TrustLang - резолвер ВСЕГДА.
            TypeId memberFt = INVALID_TYPE_ID;
            if (member.has_value()) {
                if (member->signatures.size() == 1 && utils::is_native_name(member->key)) {
                    memberFt = member->signatures.front();
                } else {
                    // Member-оператор: LHS - неявный `this`, параметры сигнатуры - только RHS.
                    const std::vector<TypeId> opArgs{rt};
                    memberFt = resolveOverload(reg, member->signatures, opArgs).chosen;
                }
            }
            const TypeId ft = member.has_value() ? memberFt : freeFt;
            if (ft == INVALID_TYPE_ID) {
                m_actx.ctx().diag().report(Severity::Error, b.range(), "no matching operator '{}' for types '{}' and '{}'", sym, reg.getFullTypeName(lt),
                                           reg.getFullTypeName(rt));
                b.lhsType = b.rhsType = b.resultType = b.commonType = INVALID_TYPE_ID;
                m_actx.setExprType(&b, INVALID_TYPE_ID);
                return INVALID_TYPE_ID;
            }
            const auto* fd = reg.getTypeDataAs<FunctionTypeData>(ft);
            EXPECT(fd != nullptr && "comparison operator: declared signature must be a function type");
            b.lhsType = lt;
            b.rhsType = rt;
            b.resultType = b.commonType = fd->returnType;
            m_actx.setExprType(&b, fd->returnType);
            return fd->returnType;
        }
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
                opname, b.text(), bad ? bad->text() : "");
            if (entry != nullptr && bad && !bad->range().isInvalid()) {
                m_actx.ctx().diag().fixit(entry, bad->range(), "*" + std::string(bad->text()));
            }
            b.lhsType = lt;
            b.rhsType = rt;
            b.resultType = b.commonType = INVALID_TYPE_ID;
            m_actx.setExprType(&b, INVALID_TYPE_ID);
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
            const TypeId lc = (lt != INVALID_TYPE_ID) ? reg.getCanonicalTypeId(lt) : INVALID_TYPE_ID;
            const TypeId rc = (rt != INVALID_TYPE_ID) ? reg.getCanonicalTypeId(rt) : INVALID_TYPE_ID;
            if (!(lEnum && rEnum && lc == rc)) {
                m_actx.ctx().diag().report(Severity::Error, b.range(), "type-safe enum comparison requires both operands to be the same enum type");
                b.resultType = b.commonType = INVALID_TYPE_ID;
                m_actx.setExprType(&b, INVALID_TYPE_ID);
                return INVALID_TYPE_ID;
            }
        }
    }

    // Продвижение auto-Bool в арифметике: Bool НЕтипизированной переменной (напр. из
    // `b := 1 :Bool` / сравнения) продвигается по правилам C++ bool→int → Int32. Буквальные
    // 0/1 выводятся как Int8 (не Bool). Явный Bool (:Bool, результат сравнения/логики)
    // в арифметике - ошибка компиляции (нельзя привести к числу). Compare/Logical и простое
    // присваивание '=' не затрагиваются.
    TypeId elt = lt, ert = rt;
    const bool arithmetic = !(b.kind() == ParserToken::Kind::CompareOp || b.kind() == ParserToken::Kind::LogicalOp) &&
                            !(b.kind() == ParserToken::Kind::AssignOp && isPlainAssignOp(b.m_op));
    if (arithmetic) {
        const TypeId boolT = reg.getCanonicalTypeId(reg.getType(type::Bool));
        auto promoteBool = [&](const AstNodeBase* operand, TypeId t, TypeId& out) {
            if (t == INVALID_TYPE_ID || reg.getCanonicalTypeId(t) != boolT) {
                return;
            }
            if (testFlag(t, SymbolFlag::Inferred)) {
                out = reg.getType(type::Int32); // bool→int (C++ promotion)
            } else {
                m_actx.ctx().diag().report(Severity::Error, operand->range(),
                                           "cannot use Bool value in arithmetic '{}'; use an explicit integer type or a cast", b.text());
            }
        };
        promoteBool(b.m_left.get(), lt, elt);
        promoteBool(b.m_right.get(), rt, ert);
    }

    // BigInteger/Rational (kArbitraryPrecision): смешивание с float (Numbers) или
    // целочисленное деление `//` - ошибки (нужен явный каст / не поддерживается).
    if (arithmetic) {
        const TypeId lc = (lt != INVALID_TYPE_ID) ? reg.getCanonicalTypeId(lt) : INVALID_TYPE_ID;
        const TypeId rc = (rt != INVALID_TYPE_ID) ? reg.getCanonicalTypeId(rt) : INVALID_TYPE_ID;
        const Group lg = (lc != INVALID_TYPE_ID) ? getGroup(getKindFromId(lc)) : Group::kAny;
        const Group rg = (rc != INVALID_TYPE_ID) ? getGroup(getKindFromId(rc)) : Group::kAny;
        const bool lAP = lg == Group::kArbitraryPrecision;
        const bool rAP = rg == Group::kArbitraryPrecision;
        if (lAP || rAP) {
            if (isIntDivOp(b.m_op)) {
                m_actx.ctx().diag().report(Severity::Error, b.range(), "integer division '{}' is not supported for BigInteger/Rational", b.text());
            } else if ((lAP && rg == Group::kNumbers) || (rAP && lg == Group::kNumbers)) {
                m_actx.ctx().diag().report(Severity::Error, b.range(), "cannot mix BigInteger/Rational with a floating-point number without an explicit cast");
            }
        }
    }

    // Простое присвоение "=" → тип RHS; составное/арифметика → тип результата (lhs op rhs).
    const TypeId result = (b.kind() == ParserToken::Kind::AssignOp && isPlainAssignOp(b.m_op)) ? rt : resultTypeBinary(b.kind(), b.m_op, elt, ert, reg);
    b.lhsType = lt;
    b.rhsType = rt;
    b.resultType = result;
    b.commonType = result;
    // Классификация контролируемой арифметики (+,-,* и +=,-=,*=): знаковые машинные операнды и
    // результат. Семантика здесь РЕШАЕТ, нужна ли детекция переполнения (-foverflow-check), и
    // кладёт готовый признак в Binary::m_overflowCheck; кодоген его только читает. unsigned (wrap),
    // BigInteger/Rational, не-целые и Bool-продвижение → false; вне класса операций → nullopt.
    if (isOverflowCheckableOp(b.m_op)) {
        b.m_overflowCheck = isSignedMachineInt(reg, lt) && isSignedMachineInt(reg, rt) && isSignedMachineInt(reg, result);
    }
    // Присваивание в переменную доверенного типа: помечаем узел ссылкой на декларацию типа
    // (источник trust-условий для проверки после присваивания; переживает таблицу символов).
    if (b.kind() == ParserToken::Kind::AssignOp) {
        b.m_typeDecl = m_core.trustTypeDeclOf(lt);
    }
    // Общий тип операндов для any_cast: арифметика → result; Compare/Logical (результат Bool)
    // → продвинутый конкретный операнд, если ровно один операнд std::any.
    if (b.kind() == ParserToken::Kind::CompareOp || b.kind() == ParserToken::Kind::LogicalOp) {
        if (isAnyType(lt, reg)) {
            b.commonType = promoteSingleNumeric(reg, rt);
        } else if (isAnyType(rt, reg)) {
            b.commonType = promoteSingleNumeric(reg, lt);
        } else {
            b.commonType = INVALID_TYPE_ID; // оба конкретные - any_cast не нужен
        }
    }
    m_actx.setExprType(&b, result);
    return result;
}

// -- Операторы сравнения типов (<~ / ~~ / ~~~) --------------------------------
// Транзитивное «подтип-или-равно» по базовым классам реестра (с защитой от циклов).
// Абстрактная группа-цель (Data==0, кроме Any) сравнивается по совпадению группы.
bool isSubtypeOrEqual(const TypeRegistry& reg, TypeId type, TypeId target) {
    const TypeId t = (type == INVALID_TYPE_ID) ? INVALID_TYPE_ID : reg.getCanonicalTypeId(structuralType(type));
    const TypeId g = (target == INVALID_TYPE_ID) ? INVALID_TYPE_ID : reg.getCanonicalTypeId(structuralType(target));
    if (t == INVALID_TYPE_ID || g == INVALID_TYPE_ID) {
        return false;
    }
    if (t == g) {
        return true;
    }
    const TypeKind tk = getKindFromId(g);
    if (getData(tk) == 0 && getGroup(tk) != Group::kAny) {
        return getGroup(getKindFromId(t)) == getGroup(tk);
    }
    std::vector<TypeId> stack(reg.baseClasses(t).begin(), reg.baseClasses(t).end());
    std::vector<TypeId> seen;
    while (!stack.empty()) {
        const TypeId b = stack.back();
        stack.pop_back();
        const TypeId bc = (b == INVALID_TYPE_ID) ? INVALID_TYPE_ID : reg.getCanonicalTypeId(structuralType(b));
        if (bc == INVALID_TYPE_ID || std::find(seen.begin(), seen.end(), bc) != seen.end()) {
            continue;
        }
        if (bc == g) {
            return true;
        }
        seen.push_back(bc);
        for (const TypeId nb : reg.baseClasses(bc)) {
            stack.push_back(nb);
        }
    }
    return false;
}

// Суммарное число полей record-типа с учётом наследования (для строгого образца).
size_t countRecordFields(const TypeRegistry& reg, TypeId type) {
    const TypeId t = (type == INVALID_TYPE_ID) ? INVALID_TYPE_ID : reg.getCanonicalTypeId(structuralType(type));
    if (t == INVALID_TYPE_ID) {
        return 0;
    }
    size_t n = 0;
    if (const auto* rd = reg.getTypeDataAs<RecordTypeData>(t)) {
        n += rd->fields.size();
    }
    for (const TypeId base : reg.baseClasses(t)) {
        const TypeId bc = (base == INVALID_TYPE_ID) ? INVALID_TYPE_ID : reg.getCanonicalTypeId(structuralType(base));
        if (bc != INVALID_TYPE_ID && bc != t) {
            n += countRecordFields(reg, bc);
        }
    }
    return n;
}

TypeId ExprTyper::typeCheckBinaryResult(Binary& b, TypeId lt, TypeId rt) {
    const TypeRegistry& reg = m_actx.ctx().types();
    const TypeId boolT = reg.getType(type::Bool);
    b.lhsType = lt;
    b.rhsType = rt;
    b.resultType = boolT;
    b.commonType = INVALID_TYPE_ID;
    b.m_typeCheckConst.reset();

    const AstNodeBase* rhs = b.m_right.get();
    // Тип ЛЕВОГО операнда: для типового операнда (`:T`) - обозначаемый тип, иначе - тип значения.
    TypeId lhsType = lt;
    if (b.m_left && b.m_left->kind() == ParserToken::Kind::TypeName) {
        if (auto r = m_actx.resolveTypeRef(*b.m_left)) {
            lhsType = *r;
        }
    }
    b.lhsType = lhsType;
    const bool rhsTypeName = rhs != nullptr && rhs->kind() == ParserToken::Kind::TypeName;
    const bool rhsStringLit = rhs != nullptr && (rhs->kind() == ParserToken::Kind::StrWide || rhs->kind() == ParserToken::Kind::StrChar);
    const bool rhsPattern = rhs != nullptr && rhs->kind() == ParserToken::Kind::DictLiteral;
    const bool rhsIdent = rhs != nullptr && rhs->kind() == ParserToken::Kind::Ident;

    // -- Разрешение типа-цели (RHS). --
    TypeId target = INVALID_TYPE_ID;
    if (rhsTypeName) {
        if (auto r = m_actx.resolveTypeRef(*rhs)) {
            target = *r;
        } else {
            m_actx.ctx().diag().report(Severity::Error, b.range(), "unknown type name '{}' in type check", std::string(rhs->text()));
        }
    } else if (rhsStringLit) {
        if (auto f = reg.findType(rhs->text())) {
            target = *f;
        } else {
            m_actx.ctx().diag().report(Severity::Error, b.range(), "unknown type name '{}' in type check", std::string(rhs->text()));
        }
    } else if (rhsIdent) {
        // Имя типа известно только в рантайме (строковая переменная) - не реализовано.
        m_actx.ctx().diag().report(Severity::Error, b.range(), "type name from a runtime variable is not implemented; use a type (':T') or a string literal");
    } else if (!rhsPattern) {
        m_actx.ctx().diag().report(Severity::Error, b.range(), "type-check operator '{}' requires a type name or a dict pattern on the right", b.text());
    }

    // -- Статика по типу-цели. --
    if (target != INVALID_TYPE_ID) {
        const bool lhsKnown = lhsType != INVALID_TYPE_ID && !isAnyType(lhsType, reg);
        if (!lhsKnown) {
            // Тип LHS не выведен/стёрт (Any): рантайм-проверка типа не реализована.
            m_actx.ctx().diag().report(Severity::Error, b.range(), "dynamic type check on a type-erased value is not implemented");
            m_actx.setExprType(&b, boolT);
            return boolT;
        }
        bool result = false;
        if (b.m_op == BinaryOp::TypeStrict) {
            const TypeId lc = (lhsType != INVALID_TYPE_ID) ? reg.getCanonicalTypeId(structuralType(lhsType)) : INVALID_TYPE_ID;
            result = (lc != INVALID_TYPE_ID) && (lc == reg.getCanonicalTypeId(structuralType(target)));
        } else {
            result = isSubtypeOrEqual(reg, lhsType, target);
        }
        b.m_typeCheckConst = result;
        m_actx.setExprType(&b, boolT);
        return boolT;
    }

    // -- Статика по образцу (Dict-паттерн) для record-LHS. --
    if (rhsPattern) {
        if (lhsType == INVALID_TYPE_ID || isAnyType(lhsType, reg)) {
            // Тип LHS не выведен/стёрт: динамическая структурная проверка не реализована.
            m_actx.ctx().diag().report(Severity::Error, b.range(), "dynamic structural check for type-erased values is not implemented");
            m_actx.setExprType(&b, boolT);
            return boolT;
        }
        const TypeId lc = reg.getCanonicalTypeId(structuralType(lhsType));
        // Собираем имена полей образца (элемент - ArgNode/Ident или Binary(AssignOp)).
        std::vector<std::string_view> patternNames;
        for (const auto& el : static_cast<const DictLiteralNode&>(*rhs).m_body) {
            if (!el) {
                continue;
            }
            std::string_view name;
            if (el->kind() == ParserToken::Kind::ArgNode || el->kind() == ParserToken::Kind::Ident) {
                name = el->text();
            } else if (el->kind() == ParserToken::Kind::AssignOp) {
                const auto& assign = static_cast<const Binary&>(*el);
                if (assign.m_left && assign.m_left->kind() == ParserToken::Kind::Ident) {
                    name = assign.m_left->text();
                }
            }
            if (!name.empty()) {
                patternNames.push_back(name);
            }
        }
        const auto* rd = reg.getTypeDataAs<RecordTypeData>(lc);
        if (rd == nullptr) {
            // LHS - не record (нет полей). Пустой образец `(,)` истинен только для словаря;
            // непустой образец полей у не-record не находит → ложь.
            const bool isDict = getGroup(getKindFromId(lc)) == Group::kDicts;
            b.m_typeCheckConst = patternNames.empty() && isDict;
            m_actx.setExprType(&b, boolT);
            return boolT;
        }
        bool ok = true;
        for (const std::string_view name : patternNames) {
            if (reg.findField(lc, name) == INVALID_TYPE_ID) {
                ok = false;
                break;
            }
        }
        if (ok && b.m_op == BinaryOp::TypeStrict && patternNames.size() != countRecordFields(reg, lc)) {
            ok = false; // строгая проверка - точный набор полей
        }
        b.m_typeCheckConst = ok;
    }
    m_actx.setExprType(&b, boolT);
    return boolT;
}

} // namespace trust
