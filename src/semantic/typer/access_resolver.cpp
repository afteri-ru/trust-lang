#include "types/type_read.hpp"
#include "semantic/typer/access_resolver.hpp"
#include "semantic/typer/member_access.hpp"
#include "semantic/core/name_resolution.hpp"
#include "semantic/typer/ellipsis.hpp"
#include "semantic/typer/overload_call.hpp"
#include "semantic/typer/call_binding.hpp"
#include "types/call.hpp"
#include "semantic/verify/format_check.hpp"
#include "semantic/solver.hpp"
#include "symbols/symbol_table.hpp"
#include "semantic/typer/type_inference.hpp"
#include "attrs/attr_builtin.hpp"
#include "ast/token.hpp"
#include "diag/diag.hpp"
#include "diag/options.hpp"
#include "diag/registry.hpp"
#include "semantic/diag.hpp"
#include "types/type_system.hpp"
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
    // Тензорные методы `sum()/matmul(t)/reshape(dims...)`: сигнатура зависит от самого тензора,
    // поэтому обрабатываются ДО общего реестра методов.
    if (n.kind() == ParserToken::Kind::MemberAccess && n.m_right && n.m_right->kind() == ParserToken::Kind::CallExpr && n.m_left) {
        const TypeId leftTensor = m_actx.ctx().readTypes().store().desugaredTypeId(m_actx.facts().exprType(*n.m_left));
        if (m_actx.ctx().readTypes().store().isTensorType(leftTensor) && resolveTensorMethodCall(n, leftTensor)) {
            return;
        }
    }
    // Вызов метода на объекте: obj.method(args) - MemberAccess(left=obj, right=CallExpr).
    if (n.kind() == ParserToken::Kind::MemberAccess && n.m_right && n.m_right->kind() == ParserToken::Kind::CallExpr) {
        handleMethodCall(n);
        return;
    }
    const TypeReadModel& reg = m_actx.registry();
    const TypeId leftT = n.m_left ? reg.store().desugaredTypeId(m_actx.facts().exprType(*n.m_left)) : INVALID_TYPE_ID;
    if (resolveObjectAccess(n, leftT)) {
        return;
    }
    resolveDictAccess(n);
}

// Кортеж/массив/тензор/enum/native-поле/record-поле: true — узел обработан.
bool AccessResolver::resolveObjectAccess(Binary& n, TypeId leftT) {
    const TypeReadModel& treg = m_actx.registry();
    if (leftT != INVALID_TYPE_ID && treg.store().isTypeDataKind(leftT, TypeDataKind::kTuple)) {
        resolveTupleAccess(n, leftT);
        return true;
    }
    // Доступ к элементу массива `a[i]` / `a.0`: левый операнд - структурный Array-тип.
    if (leftT != INVALID_TYPE_ID && treg.store().isArrayType(leftT)) {
        resolveArrayAccess(n, leftT);
        return true;
    }
    // Тензор: `t[i]`, `t[i,j]` (multi-index в n.m_indices), `t[1..3]` (срез).
    if (leftT != INVALID_TYPE_ID && treg.store().isTensorType(leftT)) {
        resolveTensorAccess(n, leftT);
        return true;
    }
    // Индексация пользовательского типа объявленным оператором `[]`: `a[i]` → C++ `(obj)[idx]`.
    // Проверка идёт ПО РЕЕСТРУ (тип объявил `[]`), а не по тексту/типу-контейнеру: у record/
    // native-класса контейнерной семантики нет, поэтому без этой ветки `a[i]` уходил бы в словарь.
    if (n.kind() == ParserToken::Kind::ArrayAccess && leftT != INVALID_TYPE_ID &&
        (treg.store().isRecordType(leftT) || treg.store().isNativeClassType(leftT) || treg.store().isNativeTemplateType(leftT)) &&
        treg.methods().findMethodInfo(leftT, op::indexFunctionName()).has_value()) {
        resolveSubscriptOperatorAccess(n, leftT);
        return true;
    }
    // record/native-класс БЕЗ объявленного `[]`: контейнерного доступа нет - явная ошибка
    // (не уходим в словарный путь `.at()`, который для такого типа невалиден).
    if (n.kind() == ParserToken::Kind::ArrayAccess && leftT != INVALID_TYPE_ID &&
        (treg.store().isRecordType(leftT) || treg.store().isNativeClassType(leftT) || treg.store().isNativeTemplateType(leftT))) {
        m_actx.ctx().diag().report(Severity::Error, n.range(), "type '{}' has no operator '[]'", treg.printer().typeNameOf(leftT));
        n.resultType = n.commonType = QualType{};
        m_actx.facts().setExprType(&n, INVALID_TYPE_ID);
        if (n.m_right) {
            m_core.analyzeNode(n.m_right);
        }
        return true;
    }
    // Доступ к члену enum через имя типа: `Color.RED` → тип enum. Осознанное решение:
    // члены не несут методов, вся работа идёт через имя типа (см. MEMORY.md).
    if (n.kind() == ParserToken::Kind::MemberAccess && n.m_right && leftT != INVALID_TYPE_ID && isEnumType(leftT, treg)) {
        const auto* ed = treg.store().getTypeDataAs<EnumTypeData>(leftT);
        if (ed) {
            // Тип, обозначенный левым именем (IdentName::resolvedTypeId), материализован при
            // резолве имени; кодоген опирается на него.
            const std::string mname = std::string(requiredText(*n.m_right));
            bool found = false;
            for (const auto& m : ed->members) {
                if (m.name == mname) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                m_actx.ctx().diag().report(Severity::Error, n.range(), "enum '{}' has no member '{}'", treg.printer().typeNameOf(leftT), mname);
                n.resultType = n.commonType = QualType{};
                m_actx.facts().setExprType(&n, INVALID_TYPE_ID);
            } else {
                n.resultType = n.commonType = QualType::fromTypeId(leftT);
                m_actx.facts().setExprType(&n, leftT);
            }
            return true;
        }
    }
    // Доступ к члену Variant через имя типа: `Value.RED` → тип ЭТОГО члена (гетерогенный:
    // у каждого члена свой тип). Работа идёт только через имя типа (как и для enum).
    if (n.kind() == ParserToken::Kind::MemberAccess && n.m_right && leftT != INVALID_TYPE_ID && isVariantType(leftT, treg)) {
        const auto* vd = treg.store().getTypeDataAs<VariantTypeData>(leftT);
        if (vd) {
            // Тип, обозначенный левым именем (не тип члена), материализован при резолве имени.
            const std::string mname = std::string(requiredText(*n.m_right));
            TypeId mtype = INVALID_TYPE_ID;
            for (const auto& m : vd->members) {
                if (m.name == mname) {
                    mtype = m.type;
                    break;
                }
            }
            if (mtype == INVALID_TYPE_ID) {
                m_actx.ctx().diag().report(Severity::Error, n.range(), "variant '{}' has no member '{}'", treg.printer().typeNameOf(leftT), mname);
                n.resultType = n.commonType = QualType{};
                m_actx.facts().setExprType(&n, INVALID_TYPE_ID);
            } else {
                n.resultType = n.commonType = QualType::fromTypeId(mtype);
                m_actx.facts().setExprType(&n, mtype);
            }
            return true;
        }
    }
    // Тип-уровневый доступ к СТАТИЧЕСКОМУ члену `Cls.field`: левый операнд - ИМЯ ТИПА (нативный
    // класс), а не переменная: различаем по материализованному `IdentName::resolvedTypeId`.
    // Член - статический (зарегистрированный ключ содержит '::'). Доступ как к полю
    // (`Cls.field`) - диагностика -Wstatic-member-as-field; `Cls::name` сюда не попадает.
    if (n.kind() == ParserToken::Kind::MemberAccess && n.m_left && n.m_left->kind() == ParserToken::Kind::Ident) {
        const auto* leftIdent = n.m_left->as<IdentName>();
        const TypeId declaredType = leftIdent->isTypeName() ? leftIdent->resolvedTypeId : INVALID_TYPE_ID;
        if (declaredType != INVALID_TYPE_ID && treg.store().isNativeClassType(declaredType)) {
            const std::string name = n.m_right ? std::string(requiredText(*n.m_right)) : std::string();
            const std::vector<TypeId> statics = treg.methods().findStaticMethod(declaredType, name);
            // Статический ЧЛЕН как поле: ровно одна сигнатура (returnType = тип поля).
            // Перегруженное имя как поле неоднозначно - оставляем диагностику «нет поля».
            if (statics.size() == 1) {
                const auto* sfd = treg.store().getTypeDataAs<FunctionTypeData>(statics.front());
                const TypeId rtype = sfd ? sfd->returnType : INVALID_TYPE_ID;
                if (rtype != INVALID_TYPE_ID) {
                    const std::string tname = std::string(treg.printer().typeNameOf(declaredType));
                    m_actx.ctx().report(n.range(), semantic::DiagId::StaticMemberAsField, "static member '{}' accessed as an instance field; use '{}::{}'",
                                        name, tname, name);
                    n.resultType = n.commonType = QualType::fromTypeId(rtype);
                    m_actx.facts().setExprType(&n, rtype);
                    return true;
                }
            }
        }
    }
    // Доступ к ПОЛЮ нативного класса `s.%field` (правый операнд - имя, НЕ вызов): объект -
    // нативный класс (Group::kNativeClass). lhsType нужен кодгену, чтобы эмитить `(obj).field`
    // (нативное имя без '%'), а не словарный `.at(...)`. Тип поля - из интерфейса (если поле
    // зарегистрировано как член) или Any.
    if (n.kind() == ParserToken::Kind::MemberAccess && n.m_right && leftT != INVALID_TYPE_ID &&
        (treg.store().isNativeClassType(leftT) || treg.store().isNativeTemplateType(leftT))) {
        n.lhsType = QualType::fromTypeId(leftT);
        TypeId ftype = INVALID_TYPE_ID;
        // Поле как член типа: ключ `%field` (как у метода); тип поля - returnType члена.
        // Поле однозначно: ровно одна сигнатура (перегруженное имя полем не является).
        if (const auto mi = treg.methods().findMethodInfo(leftT, requiredText(*n.m_right)); mi && mi->entries.size() == 1) {
            checkMemberAccess(m_actx, n.range(), mi->owner, leftT, mi->entries.front().access, requiredText(*n.m_right));
            const std::vector<TypeId> memberSigs = effectiveMethodSignatures(m_actx.registry(), leftT, *mi);
            const TypeId memberSig = memberSigs.front();
            if (const auto* fd = treg.store().getTypeDataAs<FunctionTypeData>(memberSig)) {
                ftype = fd->returnType;
            }
        }
        // У нативного поля тип не может быть неопределённым: не найден член или тип INVALID -
        // явная ошибка, а НЕ тихий `Any` (см. правило «нет fallback для невалидных данных»).
        if (ftype == INVALID_TYPE_ID) {
            const std::string tname = std::string(treg.printer().typeNameOf(leftT));
            m_actx.ctx().diag().report(Severity::Error, n.range(), "type '{}' has no field '{}'", tname, requiredText(*n.m_right));
            n.resultType = n.commonType = QualType{};
            m_actx.facts().setExprType(&n, INVALID_TYPE_ID);
            return true;
        }
        n.resultType = n.commonType = QualType::fromTypeId(ftype);
        m_actx.facts().setExprType(&n, ftype);
        return true;
    }
    // Доступ к ПОЛЮ пользовательского Record-типа (Struct/Class): `obj.field` → `(obj).c_field`.
    // Тип поля даёт аксессор формы владельца (обход базовых классов — внутри `types`). Тип объекта
    // сохраняем в lhsType (кодген эмитит прямое `.`-обращение к члену struct). Не найдено - ошибка.
    if (n.kind() == ParserToken::Kind::MemberAccess && n.m_right && leftT != INVALID_TYPE_ID && treg.store().isRecordType(leftT)) {
        n.lhsType = QualType::fromTypeId(leftT);
        const QualType ftype = treg.store().findField(leftT, requiredText(*n.m_right));
        if (!ftype.isValid()) {
            const std::string tname = std::string(treg.printer().typeNameOf(leftT));
            m_actx.ctx().diag().report(Severity::Error, n.range(), "type '{}' has no field '{}'", tname, requiredText(*n.m_right));
            n.resultType = n.commonType = QualType{};
            m_actx.facts().setExprType(&n, INVALID_TYPE_ID);
            return true;
        }
        n.resultType = n.commonType = ftype;
        m_actx.facts().setExprState(&n, ftype);
        // C++-доступ: private/protected член доступен только из тела владельца (protected — и его
        // наследников). Уровень берётся из реестра (поставлен семантикой из объявления поля).
        if (const auto acc = treg.store().findFieldAccess(leftT, requiredText(*n.m_right))) {
            checkMemberAccess(m_actx, n.range(), acc->owner, leftT, acc->access, requiredText(*n.m_right));
        }
        return true;
    }
    return false;
}

void AccessResolver::resolveDictAccess(Binary& n) {
    if (n.kind() == ParserToken::Kind::ArrayAccess) {
        // Динамический индекс - обычное выражение (резолв/типизация).
        if (n.m_right) {
            m_core.analyzeNode(n.m_right);
        }
    } else {
        // MemberAccess: m_right - имя поля или статический индекс (литерал). Не резолвим.
        if (n.m_right && n.m_right->kind() == ParserToken::Kind::IntLiteral) {
            // Статический индекс `d.1`: проверка по статической размерности объекта.
            const int64_t size = m_core.typer().dictSizeOf(n.m_left.get());
            if (size < 0) {
                m_actx.ctx().diag().report(Severity::Error, n.range(), "static dict index requires a compile-time known size (use d[expr] for dynamic access)");
            } else {
                unsigned long long idx = 0;
                if (parseDecimalUInt(requiredText(*n.m_right), idx) && static_cast<int64_t>(idx) >= size) {
                    // Ошибка на доступе к индексу: каретка на всём `d.N` (Clang-style).
                    m_actx.ctx().diag().report(Severity::Error, n.range(), "static dict index {} out of range (size {})", idx, size);
                }
            }
        }
    }
    // Тип поля: конкретный (из Dims литерала: `d.two` → Int8/...) или Any (гетерогенный/
    // неизвестный). Сохраняем на узле (транспилятор/каст читают тип результата).
    const TypeId t = m_core.typer().dictFieldTypeOf(n);
    n.resultType = n.commonType = QualType::fromTypeId(t);
    m_actx.facts().setExprType(&n, t);
}

// Левый операнд - структурный Tuple-тип (TupleTypeData). Резолвим имя/статический индекс в
// списке элементов; тип результата = тип элемента. Для динамического индекса `t[expr]`
// (без константы) - статически нерезолвимо (std::get требует константу) → диагностика.
void AccessResolver::resolveTupleAccess(Binary& n, TypeId tupleType) {
    const TypeReadModel& reg = m_actx.registry();
    const auto* td = reg.store().getTypeDataAs<TupleTypeData>(tupleType);
    EXPECT(td && "resolveTupleAccess: not a structural tuple type");
    int64_t index = -1;
    const bool isMember = n.kind() == ParserToken::Kind::MemberAccess;
    if (n.m_right && n.m_right->kind() == ParserToken::Kind::IntLiteral) {
        unsigned long long v = 0;
        if (parseDecimalUInt(requiredText(*n.m_right), v)) {
            index = static_cast<int64_t>(v);
        }
    } else if (isMember && n.m_right) {
        const std::string name = std::string(requiredText(*n.m_right));
        for (size_t i = 0; i < td->elements.size(); ++i) {
            if (td->elements[i].name == name) {
                index = static_cast<int64_t>(i);
                break;
            }
        }
        if (index < 0) {
            m_actx.ctx().diag().report(Severity::Error, n.range(), "tuple has no field '{}'", name);
            m_actx.facts().setExprType(&n, INVALID_TYPE_ID);
            return;
        }
    }
    if (index < 0 || index >= static_cast<int64_t>(td->elements.size())) {
        if (isMember) {
            m_actx.ctx().diag().report(Severity::Error, n.range(), "tuple index {} out of range (size {})", index, td->elements.size());
        } else {
            m_actx.ctx().diag().report(Severity::Error, n.range(), "tuple dynamic index is not supported: std::get requires a compile-time constant index");
        }
        m_actx.facts().setExprType(&n, INVALID_TYPE_ID);
        return;
    }
    const QualType et = td->elements[static_cast<size_t>(index)].type;
    n.resultType = n.commonType = et;
    n.tupleIndex = index; // транспилятор: std::get<index>(obj)
    m_actx.facts().setExprState(&n, et);
}

// Доступ к элементу массива `a[i]` / `a.0`: левый операнд - структурный Array-тип.
// Тип результата = элементный тип массива (ArrayTypeData::elementType). Статический индекс
// (литерал) проверяется по известной размерности массива. Индекс-выражение анализируется.
void AccessResolver::resolveArrayAccess(Binary& n, TypeId arrayType) {
    const TypeReadModel& reg = m_actx.registry();
    const TypeId et = reg.store().arrayElementType(arrayType);
    // Индекс - выражение: анализируем/типизируем (для `a[expr]`).
    if (n.m_right) {
        m_core.analyzeNode(n.m_right);
    }
    // Статический индекс: проверка границы по известной размерности (dims.front()).
    const auto& dims = reg.store().arrayDimensions(arrayType);
    if (!dims.empty() && n.m_right && n.m_right->kind() == ParserToken::Kind::IntLiteral) {
        unsigned long long idx = 0;
        if (parseDecimalUInt(requiredText(*n.m_right), idx) && idx >= dims.front()) {
            m_actx.ctx().diag().report(Severity::Error, n.range(), "array index {} out of range (size {})", idx, dims.front());
            n.resultType = n.commonType = QualType{};
            m_actx.facts().setExprType(&n, INVALID_TYPE_ID);
            return;
        }
    }
    n.resultType = n.commonType = QualType::fromTypeId(et);
    n.lhsType = QualType::fromTypeId(arrayType); // транспилятор: определяет, что это доступ к массиву (std::vector::at)
    m_actx.facts().setExprType(&n, et);
}

// Индексация тензора: `t[i]`, `t[i,j]`, `t[1..3]`. Полный список индексов - n.m_indices
// (для одиночного - m_right). Результат: элемент (все индексы) либо тензор-срез (Range).
void AccessResolver::resolveTensorAccess(Binary& n, TypeId tensorType) {
    TypeSystem& reg = m_actx.registry();
    const TypeId elem = reg.read().store().tensorElementType(tensorType);
    const auto& dims = reg.read().store().tensorDimensions(tensorType);
    std::vector<AstNodePtr> indices = n.m_indices;
    if (indices.empty() && n.m_right) {
        indices.push_back(n.m_right);
    }
    for (auto& idx : indices) {
        if (idx) {
            m_core.analyzeNode(idx);
        }
    }
    n.lhsType = QualType::fromTypeId(tensorType);
    if (indices.empty()) {
        m_actx.ctx().diag().report(Severity::Error, n.range(), "tensor indexing requires at least one index");
        n.resultType = n.commonType = QualType{};
        m_actx.facts().setExprType(&n, INVALID_TYPE_ID);
        return;
    }
    // Срез `t[a..b]` (только rank-1): результат - Tensor<Elem, [size]> (size выводится для
    // литеральных границ; иначе форма динамическая).
    if (indices.size() == 1 && indices.front() && indices.front()->kind() == ParserToken::Kind::RangeExpr) {
        if (dims.size() != 1) {
            m_actx.ctx().diag().report(Severity::Error, n.range(), "tensor slice requires a one-dimensional tensor");
            n.resultType = n.commonType = QualType{};
            m_actx.facts().setExprType(&n, INVALID_TYPE_ID);
            return;
        }
        std::vector<uint64_t> resultShape;
        const auto& range = static_cast<const RangeExpr&>(*indices.front());
        const AstNodeBase* start = range.start().get();
        const AstNodeBase* stop = range.stop().get();
        const AstNodeBase* step = range.hasStep() ? range.step().get() : nullptr;
        auto intOf = [](const AstNodeBase* node, int64_t& out) {
            if (node == nullptr || node->kind() != ParserToken::Kind::IntLiteral) {
                return false;
            }
            unsigned long long v = 0;
            if (!parseDecimalUInt(requiredText(*node), v)) {
                return false;
            }
            out = static_cast<int64_t>(v);
            return true;
        };
        int64_t s = 0;
        int64_t e = -1;
        int64_t st = 1;
        if (intOf(start, s) && intOf(stop, e) && (!step || intOf(step, st)) && st != 0 && e >= s) {
            resultShape.push_back(static_cast<uint64_t>((e - s + st - 1) / st));
        }
        const TypeId sliceType = reg.interner().getOrCreateTensorType(elem, resultShape, reg.read().store().isSparseTensorType(tensorType));
        n.resultType = n.commonType = QualType::fromTypeId(sliceType);
        m_actx.facts().setExprType(&n, sliceType);
        return;
    }
    if (static_cast<int64_t>(indices.size()) != static_cast<int64_t>(dims.size()) && !dims.empty()) {
        m_actx.ctx().diag().report(Severity::Error, n.range(), "tensor requires {} index(es), but {} provided", dims.size(), indices.size());
        n.resultType = n.commonType = QualType{};
        m_actx.facts().setExprType(&n, INVALID_TYPE_ID);
        return;
    }
    // Статическая проверка постоянных неотрицательных индексов по известной форме.
    for (size_t axis = 0; axis < indices.size() && axis < dims.size(); ++axis) {
        const AstNodeBase* idxNode = indices[axis].get();
        if (idxNode == nullptr || idxNode->kind() != ParserToken::Kind::IntLiteral) {
            continue;
        }
        unsigned long long idx = 0;
        if (parseDecimalUInt(requiredText(*idxNode), idx) && idx >= dims[axis]) {
            m_actx.ctx().diag().report(Severity::Error, idxNode->range(), "tensor index {} out of range (size {})", idx, dims[axis]);
            n.resultType = n.commonType = QualType{};
            m_actx.facts().setExprType(&n, INVALID_TYPE_ID);
            return;
        }
    }
    n.resultType = n.commonType = QualType::fromTypeId(elem);
    m_actx.facts().setExprType(&n, elem);
}

// Тензорные методы, сигнатура которых зависит от самого тензора:
//   `sum()` → Tensor<Elem> (0-dim), `matmul(t)` → Tensor<Elem, [m,n]>, `reshape(dims...)`.
// Методы `rank/size/shape/at` объявлены в реестре и идут общим путём.
bool AccessResolver::resolveTensorMethodCall(Binary& n, TypeId tensorType) {
    auto& call = static_cast<CallExpr&>(*n.m_right);
    std::string name = call.m_callee ? std::string(requiredText(*call.m_callee)) : std::string();
    const bool isSum = name == "sum";
    const bool isMatmul = name == "matmul";
    const bool isReshape = name == "reshape";
    if (!isSum && !isMatmul && !isReshape) {
        return false;
    }
    const size_t argc = call.m_args ? call.m_args->size() : 0;
    if (call.m_args) {
        for (auto& arg : *call.m_args) {
            if (arg) {
                m_core.analyzeNode(arg);
            }
        }
    }
    TypeSystem& reg = m_actx.registry();
    const TypeId elem = reg.read().store().tensorElementType(tensorType);
    n.lhsType = QualType::fromTypeId(tensorType);
    auto finish = [&](TypeId result) {
        n.resultType = n.commonType = QualType::fromTypeId(result);
        m_actx.facts().setExprType(&n, result);
        return true;
    };
    auto fail = [&](const MapperRange& range, std::string_view msg) {
        m_actx.ctx().diag().report(Severity::Error, range, "{}", msg);
        n.resultType = n.commonType = QualType{};
        m_actx.facts().setExprType(&n, INVALID_TYPE_ID);
        return true;
    };
    if (isSum) {
        if (argc != 0) {
            return fail(call.range(), "sum() takes no arguments");
        }
        if (reg.read().store().desugaredTypeId(elem) == reg.read().store().desugaredTypeId(reg.read().getType(type::Bool))) {
            return fail(n.range(), "sum() is not defined for Bool tensors");
        }
        return finish(reg.interner().getOrCreateTensorType(elem, {}, reg.read().store().isSparseTensorType(tensorType)));
    }
    if (isMatmul) {
        if (argc != 1 || !call.m_args || !call.m_args->front()) {
            return fail(call.range(), "matmul() requires exactly one tensor argument");
        }
        const TypeId other = m_actx.facts().exprType(*call.m_args->front());
        if (!reg.read().store().isTensorType(other)) {
            return fail(call.range(), "matmul() argument must be a tensor");
        }
        if (reg.read().store().desugaredTypeId(reg.read().store().tensorElementType(other)) != reg.read().store().desugaredTypeId(elem)) {
            return fail(call.range(), "matmul() requires tensors with the same element type");
        }
        const auto& ls = reg.read().store().tensorDimensions(tensorType);
        const auto& rs = reg.read().store().tensorDimensions(other);
        std::vector<uint64_t> shape;
        if (ls.size() == 1 && rs.size() == 1) {
            if (ls[0] != rs[0]) {
                return fail(call.range(), "matmul() vector lengths do not match");
            }
            // vector·vector → 0-dim tensor (scalar)
        } else if (ls.size() == 2 && rs.size() == 2) {
            if (ls[1] != rs[0]) {
                return fail(call.range(), "matmul() inner dimensions do not match");
            }
            shape = {ls[0], rs[1]};
        } else if (!ls.empty() && !rs.empty()) {
            return fail(call.range(), "matmul() supports 1-D and 2-D tensors only");
        }
        return finish(reg.interner().getOrCreateTensorType(elem, shape, reg.read().store().isSparseTensorType(tensorType)));
    }
    // reshape(d1, d2, ...): форма обязана быть статически известной.
    if (argc == 0) {
        return fail(call.range(), "reshape() requires at least one dimension");
    }
    std::vector<uint64_t> shape;
    for (const auto& arg : *call.m_args) {
        if (!arg || arg->kind() != ParserToken::Kind::IntLiteral) {
            return fail(arg ? arg->range() : call.range(), "reshape() dimensions must be integer literals");
        }
        unsigned long long v = 0;
        if (!parseDecimalUInt(requiredText(*arg), v)) {
            return fail(arg->range(), std::format("reshape() dimension '{}' is out of range", std::string(requiredText(*arg))));
        }
        shape.push_back(v);
    }
    const auto& src = reg.read().store().tensorDimensions(tensorType);
    if (!src.empty()) {
        uint64_t srcCount = 1;
        uint64_t dstCount = 1;
        for (const uint64_t d : src) {
            srcCount *= d;
        }
        for (const uint64_t d : shape) {
            dstCount *= d;
        }
        if (srcCount != dstCount) {
            return fail(call.range(), "reshape() changes the number of elements");
        }
    }
    return finish(reg.interner().getOrCreateTensorType(elem, shape, reg.read().store().isSparseTensorType(tensorType)));
}

// `(obj)[idx]`. Тип результата = возвращаемый тип оператора (из его функционального типа);
// lhsType = тип объекта (кодген по нему отличает операторную индексацию от контейнерной).
void AccessResolver::resolveSubscriptOperatorAccess(Binary& n, TypeId objType) {
    if (n.m_right) {
        m_core.analyzeNode(n.m_right);
    }
    TypeSystem& reg = m_actx.registry();
    const auto method = reg.read().methods().findMethodInfo(objType, op::indexFunctionName());
    EXPECT(method.has_value() && "resolveSubscriptOperatorAccess: '__getitem__' must be declared on the type");
    // `[]`: ВСЕГДА через единый конвейер (включая native single-signature): арность/тип индекса
    // проверяет анализатор, C++-делегации без проверки нет. Разрешение - общий вход
    // resolveCallablePositional, тексты неуспеха принадлежат форме `[]`.
    std::vector<TypeId> argTypes;
    if (n.m_right) {
        argTypes.push_back(m_actx.facts().exprType(*n.m_right));
    }
    const std::vector<TypeId> sigs = effectiveMethodSignatures(reg, objType, *method);
    const CallFailureText failure{std::format("call to overloaded operator '[]' of '{}' is ambiguous", reg.read().printer().typeNameOf(objType)),
                                  std::format("no matching overload for operator '[]' of '{}'", reg.read().printer().typeNameOf(objType)),
                                  std::format("operator '[]' of native template '{}' cannot be instantiated: multiple type parameters are not supported",
                                              reg.read().printer().typeNameOf(objType))};
    const auto resolved = resolveCallablePositional(m_actx, sigs, argTypes, n.range(), failure);
    if (!resolved.has_value()) {
        n.resultType = n.commonType = QualType{};
        m_actx.facts().setExprType(&n, INVALID_TYPE_ID);
        return;
    }
    const TypeId funcType = *resolved;
    const auto* fd = reg.read().store().getTypeDataAs<FunctionTypeData>(funcType);
    EXPECT(fd != nullptr && "resolveSubscriptOperatorAccess: '[]' must have a function type");
    const TypeId ret = fd->returnType;
    if (ret == INVALID_TYPE_ID) {
        // void operator[] не даёт значения в выражении - явная ошибка (без тихого Any).
        m_actx.ctx().diag().report(Severity::Error, n.range(), "operator '[]' of '{}' must return a value", reg.read().printer().typeNameOf(objType));
        n.resultType = n.commonType = QualType{};
        m_actx.facts().setExprType(&n, INVALID_TYPE_ID);
        return;
    }
    n.lhsType = QualType::fromTypeId(objType); // транспилятор: (obj)[idx] - вызов C++ operator[]
    if (method->entries.size() > 1) {
        n.resolvedOperatorSignature = getUnqualifiedType(funcType);
    }
    n.resultType = n.commonType = QualType::fromTypeId(ret);
    m_actx.facts().setExprType(&n, ret);
}

void AccessResolver::handleMethodCall(Binary& n) {
    m_core.typer().typeMethodCall(n);
}

} // namespace trust
