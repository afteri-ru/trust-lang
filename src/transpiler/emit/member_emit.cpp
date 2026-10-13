#include "transpiler/emit/expr_emit.hpp"
#include "transpiler/transpiler.hpp"
#include "transpiler/emit/emit_common.hpp"
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
#include "types/group.hpp"
#include "types/type_id.hpp"
#include "types/typekind.hpp"
#include "types/names.hpp"
#include "transpiler/emit/diag.hpp"
#include "utils/strings.hpp"
#include <format>
#include <memory>

namespace trust {

// Доступ к элементу словаря: имя/статический индекс (MemberAccess) или динамический индекс
// (ArrayAccess). Для конкретного типа поля - obj.at(key).getAs<Cpp>() (типизированный доступ
// к значению: fast-path variant / std::any); для Any/неизвестного - obj.at(key) (TypedValue,
// дальше any_to в касте).
bool ExprEmitter::emitDictElementAccess(const Binary& n) {
    // Заголовки Dict-типа записаны при объявлении/создании объекта (emitTypeName/resolveCppTypeId);
    // здесь - только тип поля через emitTypeName (единая точка сбора).
    const TypeId rt = n.resultType.getTypeId();
    const bool concrete = (rt != INVALID_TYPE_ID && !isAnyType(rt, m_ectx.m_ctx.readTypes()));
    std::string concreteCpp;
    if (concrete) {
        if (auto cpp = m_driver.m_type.emitTypeName(rt)) {
            concreteCpp = std::move(*cpp);
        }
    }
    // объект
    if (n.m_left) {
        m_driver.emitExpr(n.m_left.get());
    } else {
        m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, "{}");
    }
    m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, ".at(");
    // ключ
    if (n.kind() == ParserToken::Kind::MemberAccess && n.m_right && n.m_right->kind() == ParserToken::Kind::IntLiteral) {
        m_driver.emitExpr(n.m_right.get()); // статический индекс: d.1 → at(1)
    } else if (n.kind() == ParserToken::Kind::MemberAccess && n.m_right) {
        // Имя поля: d.two → at("two").
        m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, "\"" + utils::escapeCppString(requiredText(*n.m_right)) + "\"");
    } else if (n.m_right) {
        m_driver.emitExpr(n.m_right.get()); // динамический индекс: d[expr] → at(expr)
    } else {
        m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, "0");
    }
    m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, ")");
    if (concrete && !concreteCpp.empty()) {
        // Типизированный доступ к значению по C++-типу (fast-path variant / std::any).
        m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, ".getAs<" + concreteCpp + ">()");
    }
    return concrete;
}

// `<typeCpp>::<memberCpp>` (+ `(args)` при вызове). Единая точка эмиссии тип-уровневого доступа
// к члену; ветки различаются только способом получить C++-имя типа и C++-имя члена.
void ExprEmitter::emitTypeQualifiedMember(const std::string& typeCpp, std::string memberCpp, const CallExpr* call) {
    m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, typeCpp);
    m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, "::");
    m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, memberCpp);
    if (call != nullptr) {
        m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, "(");
        emitCallArgs(*call);
    }
}

// Статический член нативного класса: C++-имя печатается по типу-владельцу (`m_nativeOwner`), а не
// манглингом trust-имени; `emitTypeName` добавляет типовые аргументы и инклуд владельца (on-use).
void ExprEmitter::emitNativeStaticMember(TypeId owner, std::string_view trustName) {
    const auto typeCpp = m_driver.m_type.emitTypeName(owner);
    if (!typeCpp) {
        return; // диагностика уже выдана единым принтером типа
    }
    emitTypeQualifiedMember(*typeCpp, std::string(utils::shortNameOf(utils::bareName(trustName))), nullptr);
}

// Доступ к элементу словаря по имени или статическому индексу: d.two / d.1.
// Вызов метода на объекте (obj.method(args)) - нативный член C++-объекта, вставляется как есть.
void ExprEmitter::visit_MemberAccess(const Binary& n) {
    // Тип, обозначенный левым операндом-именем (enum/variant/native class), материализован
    // семантикой в `IdentName::resolvedTypeId`; кодоген НЕ резолвит имя типа в реестре.
    const auto* leftIdent = (n.m_left && n.m_left->kind() == ParserToken::Kind::Ident) ? n.m_left->as<IdentName>() : nullptr;
    const TypeId typeNameId = (leftIdent != nullptr && leftIdent->isTypeName()) ? leftIdent->resolvedTypeId : INVALID_TYPE_ID;
    // Доступ к enum через имя типа: Color.RED → c_Color::RED; Color.count()/fromName(...) →
    // c_Color::count()/... (тип-уровневые методы; члены и методы - статические члены структуры).
    if (n.m_left && n.m_left->kind() == ParserToken::Kind::Ident) {
        if (typeNameId != INVALID_TYPE_ID) {
            if (isEnumType(typeNameId, m_ectx.m_ctx.readTypes())) {
                auto enumCpp = m_driver.m_type.emitTypeName(typeNameId);
                if (!enumCpp) {
                    return; // диагностика уже выдана единым принтером типа
                }
                if (n.m_right && n.m_right->kind() == ParserToken::Kind::CallExpr) {
                    const auto& call = static_cast<const CallExpr&>(*n.m_right);
                    std::string mname = call.m_callee ? std::string(requiredText(*call.m_callee)) : std::string();
                    if (!mname.empty() && mname.front() == '%') {
                        mname.erase(0, 1);
                    }
                    emitTypeQualifiedMember(*enumCpp, std::move(mname), &call);
                    return;
                }
                // Член enum: Color.RED → c_Color::RED.
                emitTypeQualifiedMember(*enumCpp, utils::cppIdent(requiredText(*n.m_right)), nullptr);
                return;
            }
            if (isVariantType(typeNameId, m_ectx.m_ctx.readTypes())) {
                auto varCpp = m_driver.m_type.emitTypeName(typeNameId);
                if (!varCpp) {
                    return;
                }
                if (n.m_right && n.m_right->kind() == ParserToken::Kind::CallExpr) {
                    const auto& call = static_cast<const CallExpr&>(*n.m_right);
                    std::string mname = call.m_callee ? std::string(requiredText(*call.m_callee)) : std::string();
                    if (!mname.empty() && mname.front() == '%') {
                        mname.erase(0, 1);
                    }
                    emitTypeQualifiedMember(*varCpp, std::move(mname), &call);
                    return;
                }
                // Член variant: Value.RED → c_Value::c_RED (тип члена).
                emitTypeQualifiedMember(*varCpp, utils::cppIdent(requiredText(*n.m_right)), nullptr);
                return;
            }
        }
        // Доступ к СТАТИЧЕСКОМУ члену нативного класса `Cls.field` / `Cls.st(...)`: левый операнд -
        // ИМЯ ТИПА (нативный класс) → эмитим `<C++-имя типа>::<name>` (инклуд on-use) тем же
        // принтером типа, что и остальные тип-уровневые доступы.
        // Инстанс-член через имя типа семантика отклоняет до кодогена, поэтому здесь только статика.
        if (typeNameId != INVALID_TYPE_ID && m_ectx.m_ctx.readTypes().store().isNativeClassType(typeNameId)) {
            std::string mname = n.m_right ? std::string(requiredText(*n.m_right)) : std::string();
            const CallExpr* call = nullptr;
            if (n.m_right && n.m_right->kind() == ParserToken::Kind::CallExpr) {
                call = static_cast<const CallExpr*>(n.m_right.get());
                mname = call->m_callee ? std::string(requiredText(*call->m_callee)) : mname;
            }
            const auto typeCpp = m_driver.m_type.emitTypeName(typeNameId);
            if (!typeCpp) {
                return;
            }
            emitTypeQualifiedMember(*typeCpp, std::string(utils::shortNameOf(utils::bareName(mname))), call);
            return;
        }
    }
    if (n.m_right && n.m_right->kind() == ParserToken::Kind::CallExpr) {
        const auto& call = static_cast<const CallExpr&>(*n.m_right);
        if (call.m_callee) {
            // Метод на объекте: (объект).<нативный_член>(args). Нативность/константность метода -
            // из полного ключа (findMethodInfo: '%' нативный, '^' константный); нативное имя - из
            // ключа (срез '%'/'^'). const-вызов `obj.method^()` - attr::ReadOnly на ВЫЗОВЕ
            // (convertAttrsToNode/CallExpr) → const_cast<const T&>(obj) (гарантированно const-перегрузка).
            // const_cast-тип T - из TypeId объекта (n.lhsType.getTypeId(), сохранён семантикой; кодген не может
            // восстановить его для локальной переменной - скоуп-стек сброшен).
            // Became-const (flow-факт DA) здесь не участвует: const_cast выбирает const-перегрузку
            // явного `obj.method^()`, а не эмулирует константность переменной.
            TypeId objType =
                (n.lhsType.getTypeId() != INVALID_TYPE_ID) ? m_ectx.m_ctx.readTypes().store().desugaredTypeId(n.lhsType.getTypeId()) : INVALID_TYPE_ID;
            // Нативное имя: из полного ключа совпавшего метода (алиас → ключ цели); иначе - как есть.
            std::string mname(requiredText(*call.m_callee));
            std::string native;
            if (objType != INVALID_TYPE_ID) {
                if (auto mi = m_ectx.m_ctx.readTypes().methods().findMethodInfo(objType, mname)) {
                    native = utils::bareName(mi->key); // срез '%'/'^' → нативное имя (count/size/...)
                }
            }
            if (native.empty()) {
                native = mname;
                if (!native.empty() && native.front() == '%') {
                    native.erase(0, 1);
                }
            }
            // Пользовательский Record-тип (Struct/Class): метод эмитится как обычный член struct
            // с манглингом `c_` (единый манлинг с эмиссией метода в emitRecordDecl). Системные
            // имена операторов (`__eq__`, `__getitem__`, ...) эмитятся как `operator<sym>` и
            // НИКОГДА не манглируются (C++-идентификаторы с `__` зарезервированы).
            if (op::isOperatorFunctionName(native)) {
                native = op::operatorCppNameByFunction(native);
            } else if (objType != INVALID_TYPE_ID && m_ectx.m_ctx.readTypes().store().isRecordType(objType) && !native.empty()) {
                native = utils::nameToCpp(native);
            }
            // Перегруженный пользовательский метод: уникальный суффикс (выбор сделан СЕМАНТИКОЙ;
            // кодоген не пере-резолвит). Совпадает с суффиксом объявления метода в struct.
            if (!n.resolvedMethodSuffix.empty()) {
                native += n.resolvedMethodSuffix;
            }
            // const-вызов `obj.method^()` - attr::ReadOnly на вызове.
            const bool constCall = call.as_attr() && call.as_attr()->has_attr(m_ectx.m_ctx.attrs(), attr::ReadOnly);
            if (constCall) {
                m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, "const_cast<const ");
                if (auto ct = m_driver.m_type.resolveCppTypeId(objType)) {
                    m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, ct->first);
                } else {
                    m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, "std::any");
                }
                m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, "&>(");
                m_driver.emitExpr(n.m_left.get());
                m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, ")");
            } else {
                m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, "(");
                m_driver.emitExpr(n.m_left.get());
                m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, ")");
            }
            m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, ".");
            m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, native);
            m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, "(");
            emitCallArgs(call);
            return;
        }
    }
    if (n.tupleIndex >= 0) {
        emitTupleElementAccess(n);
        return;
    }
    // Поле НАТИВНОГО класса: `obj.%field` - правый операнд Ident (не вызов), объект - нативный
    // класс (Group::kNativeClass). Эмитим `(obj).field` (нативное имя без '%'), а не словарный
    // `.at("...")`. Тип объекта (lhsType) ставит семантика; имя типа в кодогене не резолвится.
    if (n.m_right && n.m_right->kind() == ParserToken::Kind::Ident) {
        TypeId objType = (n.lhsType.getTypeId() != INVALID_TYPE_ID) ? m_ectx.m_ctx.readTypes().store().desugaredTypeId(n.lhsType.getTypeId()) : INVALID_TYPE_ID;
        if (objType != INVALID_TYPE_ID && m_ectx.m_ctx.readTypes().store().isNativeClassType(objType)) {
            std::string fname(requiredText(*n.m_right));
            if (!fname.empty() && fname.front() == '%') {
                fname.erase(0, 1); // нативное имя поля (без '%')
            }
            m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, "(");
            m_driver.emitExpr(n.m_left.get());
            m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, ").");
            m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, fname);
            return;
        }
        // Поле пользовательского Record-типа (Struct/Class): `obj.field` → `(obj).c_field`
        // (манглинг `c_`, как у полей при эмиссии struct; семантика проверила наличие поля).
        if (objType != INVALID_TYPE_ID && m_ectx.m_ctx.readTypes().store().isRecordType(objType)) {
            std::string fname = cppName(requiredText(*n.m_right), CppEntityKind::kField);
            m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, "(");
            m_driver.emitExpr(n.m_left.get());
            m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, ").");
            m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, fname);
            return;
        }
    }
    emitDictElementAccess(n);
}

} // namespace trust
