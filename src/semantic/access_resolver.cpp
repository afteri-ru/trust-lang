// Generated: src/semantic/access_resolver.cpp
#include "semantic/access_resolver.hpp"
#include "semantic/name_resolution.hpp"
#include "semantic/analysis_common.hpp"
#include "semantic/ellipsis.hpp"
#include "semantic/overload_call.hpp"
#include "types/overload_resolve.hpp"
#include "semantic/format_check.hpp"
#include "semantic/solver.hpp"
#include "analysis/symbol_table.hpp"
#include "semantic/type_inference.hpp"
#include "attrs/attr_builtin.hpp"
#include "ast/token.hpp"
#include "diag/diag.hpp"
#include "diag/options.hpp"
#include "diag/registry.hpp"
#include "semantic/diag.hpp"
#include "types/promotion.hpp"
#include "types/registry.hpp"
#include "types/type_id.hpp"
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
        const TypeId leftT = n.m_left ? treg.getCanonicalTypeId(m_actx.exprType(*n.m_left)) : INVALID_TYPE_ID;
        if (leftT != INVALID_TYPE_ID && treg.isTypeDataKind(leftT, TypeDataKind::kTuple)) {
            resolveTupleAccess(n, leftT);
            return;
        }
        // Доступ к элементу массива `a[i]` / `a.0`: левый операнд - структурный Array-тип.
        if (leftT != INVALID_TYPE_ID && treg.isArrayType(leftT)) {
            resolveArrayAccess(n, leftT);
            return;
        }
        // Индексация пользовательского типа объявленным оператором `[]`: `a[i]` → C++ `(obj)[idx]`.
        // Проверка идёт ПО РЕЕСТРУ (тип объявил `[]`), а не по тексту/типу-контейнеру: у record/
        // native-класса контейнерной семантики нет, поэтому без этой ветки `a[i]` уходил бы в словарь.
        if (n.kind() == ParserToken::Kind::ArrayAccess && leftT != INVALID_TYPE_ID && (treg.isRecordType(leftT) || treg.isNativeClassType(leftT)) &&
            treg.findMethodInfo(leftT, "[]").has_value()) {
            resolveSubscriptOperatorAccess(n, leftT);
            return;
        }
        // record/native-класс БЕЗ объявленного `[]`: контейнерного доступа нет - явная ошибка
        // (не уходим в словарный путь `.at()`, который для такого типа невалиден).
        if (n.kind() == ParserToken::Kind::ArrayAccess && leftT != INVALID_TYPE_ID && (treg.isRecordType(leftT) || treg.isNativeClassType(leftT))) {
            m_actx.ctx().diag().report(Severity::Error, n.range(), "type '{}' has no operator '[]'", treg.getFullTypeName(leftT));
            n.resultType = n.commonType = INVALID_TYPE_ID;
            m_actx.setExprType(&n, INVALID_TYPE_ID);
            if (n.m_right) {
                m_core.analyzeNode(n.m_right);
            }
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
        // Тип-уровневый доступ к СТАТИЧЕСКОМУ члену `Cls.field`: левый операнд - ИМЯ ТИПА (нативный
        // класс), а не переменная (различаем через findType(left->text())). Член - статический
        // (зарегистрированный ключ содержит '::'). Доступ как к полю (`Cls.field`) - диагностика
        // -Wstatic-member-as-field; `Cls::name` (namespace-стиль) сюда не попадает (квалифицированное имя).
        if (n.kind() == ParserToken::Kind::MemberAccess && n.m_left && n.m_left->kind() == ParserToken::Kind::Ident && leftT != INVALID_TYPE_ID &&
            treg.isNativeClassType(leftT)) {
            const auto typeOf = treg.findType(n.m_left->text());
            if (typeOf && treg.isNativeClassType(*typeOf)) {
                const std::string name = n.m_right ? std::string(n.m_right->text()) : std::string();
                const std::vector<TypeId> statics = treg.findStaticMethod(leftT, name);
                // Статический ЧЛЕН как поле: ровно одна сигнатура (returnType = тип поля).
                // Перегруженное имя как поле неоднозначно - оставляем диагностику «нет поля».
                if (statics.size() == 1) {
                    const auto* sfd = treg.getTypeDataAs<FunctionTypeData>(statics.front());
                    const TypeId rtype = sfd ? sfd->returnType : INVALID_TYPE_ID;
                    if (rtype != INVALID_TYPE_ID) {
                        const std::string tname = std::string(treg.getFullTypeName(leftT));
                        m_actx.ctx().report(n.range(), semantic::DiagId::StaticMemberAsField, "static member '{}' accessed as an instance field; use '{}::{}'",
                                            name, tname, name);
                        n.lhsType = leftT;
                        n.resultType = n.commonType = rtype;
                        m_actx.setExprType(&n, rtype);
                        return;
                    }
                }
            }
        }
        // Доступ к ПОЛЮ нативного класса `s.%field` (правый операнд - имя, НЕ вызов): объект -
        // нативный класс (Group::kNativeClass). lhsType нужен кодгену, чтобы эмитить `(obj).field`
        // (нативное имя без '%'), а не словарный `.at(...)`. Тип поля - из интерфейса (если поле
        // зарегистрировано как член) или Any.
        if (n.kind() == ParserToken::Kind::MemberAccess && n.m_right && leftT != INVALID_TYPE_ID && treg.isNativeClassType(leftT)) {
            n.lhsType = leftT;
            TypeId ftype = INVALID_TYPE_ID;
            // Поле как член типа: ключ `%field` (как у метода); тип поля - returnType члена.
            // Поле однозначно: ровно одна сигнатура (перегруженное имя полем не является).
            if (const auto mi = treg.findMethodInfo(leftT, n.m_right->text()); mi && mi->signatures.size() == 1) {
                if (const auto* fd = treg.getTypeDataAs<FunctionTypeData>(mi->signatures.front())) {
                    ftype = fd->returnType;
                }
            }
            // У нативного поля тип не может быть неопределённым: не найден член или тип INVALID -
            // явная ошибка, а НЕ тихий `Any` (см. правило «нет fallback для невалидных данных»).
            if (ftype == INVALID_TYPE_ID) {
                const std::string tname = std::string(treg.getFullTypeName(leftT));
                m_actx.ctx().diag().report(Severity::Error, n.range(), "type '{}' has no field '{}'", tname, n.m_right->text());
                n.resultType = n.commonType = INVALID_TYPE_ID;
                m_actx.setExprType(&n, INVALID_TYPE_ID);
                return;
            }
            n.resultType = n.commonType = ftype;
            m_actx.setExprType(&n, ftype);
            return;
        }
        // Доступ к ПОЛЮ пользовательского Record-типа (Struct/Class): `obj.field` → `(obj).c_field`.
        // Тип поля - из RecordTypeData с обходом базовых классов (наследование). Тип объекта
        // сохраняем в lhsType (кодген эмитит прямое `.`-обращение к члену struct). Не найдено - ошибка.
        if (n.kind() == ParserToken::Kind::MemberAccess && n.m_right && leftT != INVALID_TYPE_ID && treg.isRecordType(leftT)) {
            n.lhsType = leftT;
            const TypeId ftype = treg.findField(leftT, n.m_right->text());
            if (ftype == INVALID_TYPE_ID) {
                const std::string tname = std::string(treg.getFullTypeName(leftT));
                m_actx.ctx().diag().report(Severity::Error, n.range(), "type '{}' has no field '{}'", tname, n.m_right->text());
                n.resultType = n.commonType = INVALID_TYPE_ID;
                m_actx.setExprType(&n, INVALID_TYPE_ID);
                return;
            }
            n.resultType = n.commonType = ftype;
            m_actx.setExprType(&n, ftype);
            return;
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

// Индексация пользовательского типа объявленным оператором `[]` (member-only): `a[i]` → C++
// `(obj)[idx]`. Тип результата = возвращаемый тип оператора (из его функционального типа);
// lhsType = тип объекта (кодген по нему отличает операторную индексацию от контейнерной).
void AccessResolver::resolveSubscriptOperatorAccess(Binary& n, TypeId objType) {
    if (n.m_right) {
        m_core.analyzeNode(n.m_right);
    }
    TypeRegistry& reg = m_actx.ctx().types();
    const auto method = reg.findMethodInfo(objType, "[]");
    EXPECT(method.has_value() && "resolveSubscriptOperatorAccess: '[]' must be declared on the type");
    // `[]`: НАТИВНЫЙ оператор - C++-путь; TrustLang - резолвер ВСЕГДА (выбор по типу индекса).
    TypeId funcType = INVALID_TYPE_ID;
    if (method->signatures.size() == 1 && utils::is_native_name(method->key)) {
        funcType = method->signatures.front();
    } else {
        std::vector<TypeId> argTypes;
        if (n.m_right) {
            argTypes.push_back(m_actx.exprType(*n.m_right));
        }
        const OverloadResolution r = resolveOverload(reg, method->signatures, argTypes);
        if (r.chosen == INVALID_TYPE_ID) {
            m_actx.ctx().diag().report(Severity::Error, n.range(), "no matching overload for operator '[]' of '{}'", reg.getFullTypeName(objType));
            n.resultType = n.commonType = INVALID_TYPE_ID;
            m_actx.setExprType(&n, INVALID_TYPE_ID);
            return;
        }
        funcType = r.chosen;
    }
    const auto* fd = reg.getTypeDataAs<FunctionTypeData>(funcType);
    EXPECT(fd != nullptr && "resolveSubscriptOperatorAccess: '[]' must have a function type");
    const TypeId ret = fd->returnType;
    if (ret == INVALID_TYPE_ID) {
        // void operator[] не даёт значения в выражении - явная ошибка (без тихого Any).
        m_actx.ctx().diag().report(Severity::Error, n.range(), "operator '[]' of '{}' must return a value", reg.getFullTypeName(objType));
        n.resultType = n.commonType = INVALID_TYPE_ID;
        m_actx.setExprType(&n, INVALID_TYPE_ID);
        return;
    }
    n.lhsType = objType; // транспилятор: (obj)[idx] - вызов C++ operator[]
    n.resultType = n.commonType = ret;
    m_actx.setExprType(&n, ret);
}

// -- Вызов метода на объекте: obj.method(args) --
// По типу объекта ищет метод в реестре типов (TypeRegistry::findMethod), проверяет наличие и
// количество аргументов по сигнатуре, типизирует результат возвращаемым типом. Метод - это
// функциональный тип (метод и функция - одно и то же), поэтому проверка аргументов идёт по
// FunctionTypeData::paramTypes единым путём с функциями. Проверка происходит ДО генерации C++.
void AccessResolver::handleMethodCall(Binary& n) {
    auto& call = static_cast<CallExpr&>(*n.m_right);
    const std::string mname = call.m_callee ? std::string(call.m_callee->text()) : std::string();
    // Аргументы вызова МЕТОДА анализируем ЗДЕСЬ: общий обход детей для MemberAccess не выполняется
    // (analyzeNode для MemberAccess/ArrayAccess делает ранний return в AccessResolver), поэтому без
    // этого шага аргументы метода не получали бы ни резолва имён, ни типов (ни диагностик).
    // Порядок: сначала аргументы (нужны типы для проверок), затем сигнатура метода.
    if (call.m_args) {
        for (auto& arg : *call.m_args) {
            if (arg) {
                m_core.analyzeNode(arg);
            }
        }
    }
    // Многоточие `obj.m(a, ... expr ...)`/`obj.m(a, ...)`: разбор ЕДИНЫМ примитивом
    // (semantic/ellipsis), как у вызовов функций; арность проверяется с учётом числа позиций.
    const EllipsisInfo einfo = scanCallEllipsis(call);
    // Не-const: instantiateRangeMethod интернирует функциональный тип (мутирует реестр).
    TypeRegistry& reg = m_actx.ctx().types();
    const TypeId objType = n.m_left ? reg.getCanonicalTypeId(m_actx.exprType(*n.m_left)) : INVALID_TYPE_ID;
    if (objType == INVALID_TYPE_ID) {
        // Тип объекта неизвестен (напр. Any) - сигнатуру метода получить нельзя; многоточие не может
        // быть раскрыто (кодогенерация не должна получить нераскрытое многоточие).
        if (einfo.form != EllipsisForm::None) {
            m_actx.ctx().diag().report(Severity::Error, call.range(), "аргументы вызова: число параметров вызываемого метода неизвестно");
            discardEllipsisElements(*call.m_args, false);
            m_actx.setExprType(&n, INVALID_TYPE_ID);
            return;
        }
        // Не можем проверить метод; типизируем как Any.
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
        if (call.m_args) {
            discardEllipsisElements(*call.m_args, false);
        }
        m_actx.setExprType(&n, INVALID_TYPE_ID);
        return;
    }
    // МЕТОД - НАБОР сигнатур (перегрузки). Подстановка T→Elem для Range/Array применяется к каждой.
    const bool rangeLike = reg.isRangeType(objType) || reg.getCanonicalTypeId(objType) == reg.getType(type_category::Range);
    const bool arrayLike = reg.isArrayType(objType) || reg.getCanonicalTypeId(objType) == reg.getType(type::Array);
    std::vector<TypeId> sigs;
    sigs.reserve(methodInfo->signatures.size());
    for (const TypeId s : methodInfo->signatures) {
        TypeId ft = s;
        if (rangeLike) {
            ft = reg.instantiateRangeMethod(objType, ft);
        }
        if (arrayLike) {
            ft = reg.instantiateArrayMethod(objType, ft);
        }
        sigs.push_back(ft);
    }

    // Выбор сигнатуры. НАТИВНЫЙ метод (ключ с '%') - перегрузку/конверсии разрешает C++-слой;
    // TrustLang (Record) метод - разрешает АНАЛИЗАТОР ВСЕГДА (в т.ч. при единственной сигнатуре).
    // Многоточие (материализация аргументов) допустимо только при единственной сигнатуре.
    const bool nativeMethod = utils::is_native_name(methodInfo->key);
    const bool hasEllipsis = (einfo.form != EllipsisForm::None);
    TypeId funcType = INVALID_TYPE_ID;
    if (sigs.size() == 1 && (nativeMethod || hasEllipsis)) {
        funcType = sigs.front();
    } else {
        if (hasEllipsis) {
            m_actx.ctx().diag().report(Severity::Error, call.range(), "method '{}' of type '{}' is overloaded; ellipsis in arguments is not supported", mname,
                                       reg.getFullTypeName(objType));
            discardEllipsisElements(*call.m_args, false);
            m_actx.setExprType(&n, INVALID_TYPE_ID);
            return;
        }
        const std::vector<TypeId> argTypes = callArgTypes(m_actx, call);
        const TypeId chosen = resolveCallOverload(m_actx, sigs, argTypes, call.range(),
                                                  std::format("method '{}' of type '{}'", mname, reg.getFullTypeName(objType)));
        if (chosen == INVALID_TYPE_ID) {
            m_actx.setExprType(&n, INVALID_TYPE_ID);
            return;
        }
        funcType = chosen;
        // Перегруженные пользовательские Record-методы манглируются (`c_<name>`): нужен уникальный
        // суффикс (иначе C++ выберет не ту). При одной сигнатуре C++-имя уникально и без суффикса.
        // Нативные имена фиксированы внешней библиотекой - суффикс не добавляется (резолв C++).
        if (sigs.size() > 1 && reg.isRecordType(reg.getCanonicalTypeId(objType))) {
            n.resolvedMethodSuffix = overloadCppSuffix(reg, chosen);
        }
    }

    const auto* fd = reg.getTypeDataAs<FunctionTypeData>(funcType);
    EXPECT(fd && "handleMethodCall: method signature is not a function type");
    const size_t nargs = call.m_args ? call.m_args->size() : 0;
    if (einfo.form != EllipsisForm::None) {
        // ЕДИНЫЙ примитив (как у вызовов функций): структурные правила + capacity/типы +
        // МАТЕРИАЛИЗАЦИЯ списка аргументов (кодоген многоточия не видит).
        if (!validateEllipsis(einfo, call, m_actx, "аргументы вызова")) {
            discardEllipsisElements(*call.m_args, false);
            m_actx.setExprType(&n, INVALID_TYPE_ID);
            return;
        }
        if (!expandCallEllipsis(call, m_actx, einfo, fd->paramTypes, fd->variadicType != INVALID_TYPE_ID)) {
            m_actx.setExprType(&n, INVALID_TYPE_ID);
            return;
        }
    } else if (sigs.size() == 1 && nargs != fd->paramTypes.size()) {
        // Для набора из 1 - прежняя проверка арности; для перегрузки арность уже проверена резолвером.
        m_actx.ctx().diag().report(Severity::Error, call.range(), "method '{}' of type '{}' expects {} argument(s), got {}", mname,
                                   reg.getFullTypeName(objType), fd->paramTypes.size(), nargs);
        m_actx.setExprType(&n, INVALID_TYPE_ID);
        return;
    }
    n.resultType = n.commonType = fd->returnType;
    m_actx.setExprType(&n, fd->returnType);
}
} // namespace trust
