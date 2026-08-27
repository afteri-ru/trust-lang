// Generated: src/semantic/access_resolver.cpp
#include "semantic/access_resolver.hpp"
#include "semantic/name_resolution.hpp"
#include "semantic/analysis_common.hpp"
#include "semantic/format_check.hpp"
#include "semantic/solver.hpp"
#include "semantic/symbol_table.hpp"
#include "semantic/type_inference.hpp"
#include "ast/attr_builtin.hpp"
#include "ast/token.hpp"
#include "diag/diag.hpp"
#include "diag/options.hpp"
#include "diag/registry.hpp"
#include "semantic/diag.hpp"
#include "types/promotion.hpp"
#include "types/registry.hpp"
#include "types/type_id.hpp"
#include "utils/operators.hpp"
#include "utils/strings.hpp"
#include <algorithm>
#include <format>
#include <string>

namespace trust {

// Доступ к элементу словаря. MemberAccess (имя `d.two` или статический индекс `d.1`) и
// ArrayAccess (динамический индекс `d[1]`). Объект анализируется; имя поля справа от '.'
// НЕ резолвится как переменная; статический индекс проверяется по размерности объекта.
void AccessResolver::analyzeAccess(Binary& n) {
    if (n.m_left) {
        m_core.analyzeNode(n.m_left);
    }
    // Вызов метода на объекте: obj.method(args) - MemberAccess(left=obj, right=CallExpr).
    if (n.kind() == ParserToken::Kind::MemberAccess && n.m_right && n.m_right->kind() == ParserToken::Kind::CallExpr) {
        handleMethodCall(n);
        return;
    }
    // Доступ к кортежу `t.name` / `t.0` / `t[idx]`: левый операнд - структурный Tuple-тип.
    {
        const TypeRegistry& treg = m_actx.ctx().types();
        const TypeId leftT = n.m_left ? treg.getCanonicalTypeId(m_actx.resolvedType(*n.m_left)) : INVALID_TYPE_ID;
        if (leftT != INVALID_TYPE_ID && treg.isTypeDataKind(leftT, TypeDataKind::kTuple)) {
            resolveTupleAccess(n, leftT);
            return;
        }
        // Доступ к элементу массива `a[i]` / `a.0`: левый операнд - структурный Array-тип.
        if (leftT != INVALID_TYPE_ID && treg.isArrayType(leftT)) {
            resolveArrayAccess(n, leftT);
            return;
        }
        // Доступ к члену enum через имя типа: `Color.RED` → тип enum. Осознанное решение:
        // члены не несут методов, вся работа идёт через имя типа (см. MEMORY.md).
        if (n.kind() == ParserToken::Kind::MemberAccess && n.m_right && leftT != INVALID_TYPE_ID && isEnumType(leftT, treg)) {
            const auto* ed = treg.getTypeDataAs<EnumTypeData>(leftT);
            if (ed) {
                const std::string mname = std::string(n.m_right->text());
                bool found = false;
                for (const auto& m : ed->members) {
                    if (m.name == mname) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    m_actx.ctx().diag().report(Severity::Error, n.range(), "enum '{}' has no member '{}'", treg.getFullTypeName(leftT), mname);
                    n.resultType = n.commonType = INVALID_TYPE_ID;
                    m_actx.setExprType(&n, INVALID_TYPE_ID);
                } else {
                    n.resultType = n.commonType = leftT;
                    m_actx.setExprType(&n, leftT);
                }
                return;
            }
        }
        // Доступ к члену Variant через имя типа: `Value.RED` → тип ЭТОГО члена (гетерогенный:
        // у каждого члена свой тип). Работа идёт только через имя типа (как и для enum).
        if (n.kind() == ParserToken::Kind::MemberAccess && n.m_right && leftT != INVALID_TYPE_ID && isVariantType(leftT, treg)) {
            const auto* vd = treg.getTypeDataAs<VariantTypeData>(leftT);
            if (vd) {
                const std::string mname = std::string(n.m_right->text());
                TypeId mtype = INVALID_TYPE_ID;
                for (const auto& m : vd->members) {
                    if (m.name == mname) {
                        mtype = m.type;
                        break;
                    }
                }
                if (mtype == INVALID_TYPE_ID) {
                    m_actx.ctx().diag().report(Severity::Error, n.range(), "variant '{}' has no member '{}'", treg.getFullTypeName(leftT), mname);
                    n.resultType = n.commonType = INVALID_TYPE_ID;
                    m_actx.setExprType(&n, INVALID_TYPE_ID);
                } else {
                    n.resultType = n.commonType = mtype;
                    m_actx.setExprType(&n, mtype);
                }
                return;
            }
        }
    }
    if (n.kind() == ParserToken::Kind::ArrayAccess) {
        // Динамический индекс - обычное выражение (резолв/типизация).
        if (n.m_right) {
            m_core.analyzeNode(n.m_right);
        }
    } else {
        // MemberAccess: m_right - имя поля или статический индекс (литерал). Не резолвим.
        if (n.m_right && n.m_right->kind() == ParserToken::Kind::IntLiteral) {
            // Статический индекс `d.1`: проверка по статической размерности объекта.
            const int64_t size = m_core.m_typer.dictSizeOf(n.m_left.get());
            if (size < 0) {
                m_actx.ctx().diag().report(Severity::Error, n.range(), "static dict index requires a compile-time known size (use d[expr] for dynamic access)");
            } else {
                unsigned long long idx = 0;
                if (parseDecimalUInt(n.m_right->text(), idx) && static_cast<int64_t>(idx) >= size) {
                    // Ошибка на доступе к индексу: каретка на всём `d.N` (Clang-style).
                    m_actx.ctx().diag().report(Severity::Error, n.range(), "static dict index {} out of range (size {})", idx, size);
                }
            }
        }
    }
    // Тип поля: конкретный (из Dims литерала: `d.two` → Int8/...) или Any (гетерогенный/
    // неизвестный). Сохраняем на узле (транспилятор/каст читают тип результата).
    const TypeId t = m_core.m_typer.dictFieldTypeOf(n);
    n.resultType = n.commonType = t;
    m_actx.setExprType(&n, t);
}

// -- Доступ к элементу кортежа: t.name / t.0 / t[idx] --
// Левый операнд - структурный Tuple-тип (TupleTypeData). Резолвим имя/статический индекс в
// списке элементов; тип результата = тип элемента. Для динамического индекса `t[expr]`
// (без константы) - статически нерезолвимо (std::get требует константу) → диагностика.
void AccessResolver::resolveTupleAccess(Binary& n, TypeId tupleType) {
    const TypeRegistry& reg = m_actx.ctx().types();
    const auto* td = reg.getTypeDataAs<TupleTypeData>(tupleType);
    EXPECT(td && "resolveTupleAccess: not a structural tuple type");
    int64_t index = -1;
    const bool isMember = n.kind() == ParserToken::Kind::MemberAccess;
    if (n.m_right && n.m_right->kind() == ParserToken::Kind::IntLiteral) {
        unsigned long long v = 0;
        if (parseDecimalUInt(n.m_right->text(), v)) {
            index = static_cast<int64_t>(v);
        }
    } else if (isMember && n.m_right) {
        const std::string name = std::string(n.m_right->text());
        for (size_t i = 0; i < td->elements.size(); ++i) {
            if (td->elements[i].name == name) {
                index = static_cast<int64_t>(i);
                break;
            }
        }
        if (index < 0) {
            m_actx.ctx().diag().report(Severity::Error, n.range(), "tuple has no field '{}'", name);
            m_actx.setExprType(&n, INVALID_TYPE_ID);
            return;
        }
    }
    if (index < 0 || index >= static_cast<int64_t>(td->elements.size())) {
        if (isMember) {
            m_actx.ctx().diag().report(Severity::Error, n.range(), "tuple index {} out of range (size {})", index, td->elements.size());
        } else {
            m_actx.ctx().diag().report(Severity::Error, n.range(), "tuple dynamic index is not supported: std::get requires a compile-time constant index");
        }
        m_actx.setExprType(&n, INVALID_TYPE_ID);
        return;
    }
    const TypeId et = td->elements[static_cast<size_t>(index)].type;
    n.resultType = n.commonType = et;
    n.tupleIndex = index; // транспилятор: std::get<index>(obj)
    m_actx.setExprType(&n, et);
}

// Доступ к элементу массива `a[i]` / `a.0`: левый операнд - структурный Array-тип.
// Тип результата = элементный тип массива (ArrayTypeData::elementType). Статический индекс
// (литерал) проверяется по известной размерности массива. Индекс-выражение анализируется.
void AccessResolver::resolveArrayAccess(Binary& n, TypeId arrayType) {
    const TypeRegistry& reg = m_actx.ctx().types();
    const TypeId et = reg.arrayElementType(arrayType);
    // Индекс - выражение: анализируем/типизируем (для `a[expr]`).
    if (n.m_right) {
        m_core.analyzeNode(n.m_right);
    }
    // Статический индекс: проверка границы по известной размерности (dims.front()).
    const auto& dims = reg.arrayDimensions(arrayType);
    if (!dims.empty() && n.m_right && n.m_right->kind() == ParserToken::Kind::IntLiteral) {
        unsigned long long idx = 0;
        if (parseDecimalUInt(n.m_right->text(), idx) && idx >= dims.front()) {
            m_actx.ctx().diag().report(Severity::Error, n.range(), "array index {} out of range (size {})", idx, dims.front());
            n.resultType = n.commonType = INVALID_TYPE_ID;
            m_actx.setExprType(&n, INVALID_TYPE_ID);
            return;
        }
    }
    n.resultType = n.commonType = et;
    n.lhsType = arrayType; // транспилятор: определяет, что это доступ к массиву (std::vector::at)
    m_actx.setExprType(&n, et);
}

// -- Вызов метода на объекте: obj.method(args) --
// По типу объекта ищет метод в реестре типов (TypeRegistry::findMethod), проверяет наличие и
// количество аргументов по сигнатуре, типизирует результат возвращаемым типом. Метод - это
// функциональный тип (метод и функция - одно и то же), поэтому проверка аргументов идёт по
// FunctionTypeData::paramTypes единым путём с функциями. Проверка происходит ДО генерации C++.
void AccessResolver::handleMethodCall(Binary& n) {
    const auto& call = static_cast<const CallExpr&>(*n.m_right);
    const std::string mname = call.m_callee ? std::string(call.m_callee->text()) : std::string();
    // Не-const: instantiateRangeMethod интернирует функциональный тип (мутирует реестр).
    TypeRegistry& reg = m_actx.ctx().types();
    const TypeId objType = n.m_left ? reg.getCanonicalTypeId(m_actx.resolvedType(*n.m_left)) : INVALID_TYPE_ID;
    if (objType == INVALID_TYPE_ID) {
        // Тип объекта неизвестен (напр. Any) - не можем проверить метод; типизируем как Any.
        n.resultType = n.commonType = m_actx.ctx().types().getType(type_generic::Any);
        m_actx.setExprType(&n, n.resultType);
        return;
    }
    // Тип объекта сохраняем в lhsType (для кодгена: нативное имя метода через findMethodInfo и
    // const_cast<const T&>). Кодген не может восстановить его сам (скоуп-стек сброшен к глобальному).
    n.lhsType = objType;
    const auto methodInfo = reg.findMethodInfo(objType, mname);
    if (!methodInfo) {
        const std::string tname = std::string(reg.getFullTypeName(objType));
        m_actx.ctx().diag().report(Severity::Error, n.range(), "type '{}' has no method '{}'", tname, mname);
        m_actx.setExprType(&n, INVALID_TYPE_ID);
        return;
    }
    // Интернированная сигнатура метода (TypeId). Нативность/константность для кодгена - из
    // methodInfo->key (полный ключ с '%'/'^'); const-вызов (`obj.method^()`) кодген определяет по
    // attr::ReadOnly на вызове (см. convertAttrsToNode/CallExpr).
    TypeId funcType = methodInfo->funcType;
    // Параметризованный Range<Elem> (и абстрактный `:Range`): методы объявлены на `:Range` с
    // типовым параметром T (Group::kTemplateParam); подставляем T→Elem, чтобы `$a.at(0)` и
    // `$a.start()` возвращали ЭЛЕМЕНТНЫЙ тип (Int64/Rational/...), а не типовой параметр.
    if (reg.isRangeType(objType) || reg.getCanonicalTypeId(objType) == reg.getType(type_category::Range)) {
        funcType = reg.instantiateRangeMethod(objType, funcType);
    }
    // Параметризованный Array<Elem>: методы объявлены на `:Array` с T; подставляем T→Elem,
    // чтобы `a.at(0)`/`a.first()` возвращали ЭЛЕМЕНТНЫЙ тип (как instantiateRangeMethod).
    if (reg.isArrayType(objType) || reg.getCanonicalTypeId(objType) == reg.getType(type::Array)) {
        funcType = reg.instantiateArrayMethod(objType, funcType);
    }
    const auto* fd = reg.getTypeDataAs<FunctionTypeData>(funcType);
    EXPECT(fd && "handleMethodCall: method signature is not a function type");
    const size_t nargs = call.m_args ? call.m_args->size() : 0;
    if (nargs != fd->paramTypes.size()) {
        m_actx.ctx().diag().report(Severity::Error, call.range(), "method '{}' of type '{}' expects {} argument(s), got {}", mname,
                                   reg.getFullTypeName(objType), fd->paramTypes.size(), nargs);
        m_actx.setExprType(&n, INVALID_TYPE_ID);
        return;
    }
    n.resultType = n.commonType = fd->returnType;
    m_actx.setExprType(&n, fd->returnType);
}
} // namespace trust
