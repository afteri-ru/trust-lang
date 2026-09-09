// Generated: src/transpiler/expr_emit.cpp
#include "transpiler/expr_emit.hpp"
#include "transpiler/transpiler.hpp"
#include "transpiler/emit_common.hpp"
#include "ast/ast_nodes.hpp"
#include "attrs/attr_builtin.hpp"
#include "ast/ident_name.hpp"
#include "ast/kind_visitor.hpp"
#include "ast/token_type.hpp"
#include "session/context.hpp"
#include "diag/registry.hpp"
#include "diag/base_diags.hpp"
#include "analysis/symbol_table.hpp"
#include "syntax/term.h"
#include "types/registry.hpp"
#include "types/runtime_symbols.hpp"
#include "types/intrinsics.hpp"
#include "types/group.hpp"
#include "types/type_id.hpp"
#include "types/typekind.hpp"
#include "types/type_names.hpp"
#include "transpiler/diag.hpp"
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
    const TypeId rt = n.resultType;
    const bool concrete = (rt != INVALID_TYPE_ID && !isAnyType(rt, m_ectx.m_ctx.types()));
    std::string concreteCpp;
    if (concrete) {
        if (auto cpp = m_driver.m_type.emitTypeName(rt, "")) {
            concreteCpp = std::move(*cpp);
        }
    }
    // объект
    if (n.m_left) {
        m_driver.emitExpr(n.m_left.get());
    } else {
        m_ectx.m_ctx.source().output_append(m_ectx.m_out, "{}");
    }
    m_ectx.m_ctx.source().output_append(m_ectx.m_out, ".at(");
    // ключ
    if (n.kind() == ParserToken::Kind::MemberAccess && n.m_right && n.m_right->kind() == ParserToken::Kind::IntLiteral) {
        m_driver.emitExpr(n.m_right.get()); // статический индекс: d.1 → at(1)
    } else if (n.kind() == ParserToken::Kind::MemberAccess && n.m_right) {
        // Имя поля: d.two → at("two").
        m_ectx.m_ctx.source().output_append(m_ectx.m_out, "\"" + utils::escape_cpp_string(n.m_right->text()) + "\"");
    } else if (n.m_right) {
        m_driver.emitExpr(n.m_right.get()); // динамический индекс: d[expr] → at(expr)
    } else {
        m_ectx.m_ctx.source().output_append(m_ectx.m_out, "0");
    }
    m_ectx.m_ctx.source().output_append(m_ectx.m_out, ")");
    if (concrete && !concreteCpp.empty()) {
        // Типизированный доступ к значению по C++-типу (fast-path variant / std::any).
        m_ectx.m_ctx.source().output_append(m_ectx.m_out, ".getAs<" + concreteCpp + ">()");
    }
    return concrete;
}

// Доступ к элементу словаря по имени или статическому индексу: d.two / d.1.
// Вызов метода на объекте (obj.method(args)) - нативный член C++-объекта, вставляется как есть.
void ExprEmitter::visit_MemberAccess(const Binary& n) {
    // Доступ к enum через имя типа: Color.RED → c_Color::RED; Color.count()/fromName(...) →
    // c_Color::count()/... (тип-уровневые методы; члены и методы - статические члены структуры).
    if (n.m_left && n.m_left->kind() == ParserToken::Kind::Ident) {
        if (auto tid = m_ectx.m_ctx.types().findType(n.m_left->text())) {
            if (isEnumType(*tid, m_ectx.m_ctx.types())) {
                const std::string enum_cpp = utils::name_to_cpp(n.m_left->text());
                if (n.m_right && n.m_right->kind() == ParserToken::Kind::CallExpr) {
                    const auto& call = static_cast<const CallExpr&>(*n.m_right);
                    std::string mname = call.m_callee ? std::string(call.m_callee->text()) : std::string();
                    if (!mname.empty() && mname.front() == '%') {
                        mname.erase(0, 1);
                    }
                    m_ectx.m_ctx.source().output_append(m_ectx.m_out, enum_cpp + "::" + mname + "(");
                    emitCallArgs(call);
                    return;
                }
                // Член enum: Color.RED → c_Color::RED.
                const std::string member_cpp = utils::name_to_cpp(n.m_right->text());
                m_ectx.m_ctx.source().output_append(m_ectx.m_out, enum_cpp + "::" + member_cpp);
                return;
            }
            if (isVariantType(*tid, m_ectx.m_ctx.types())) {
                const std::string var_cpp = utils::name_to_cpp(n.m_left->text());
                if (n.m_right && n.m_right->kind() == ParserToken::Kind::CallExpr) {
                    const auto& call = static_cast<const CallExpr&>(*n.m_right);
                    std::string mname = call.m_callee ? std::string(call.m_callee->text()) : std::string();
                    if (!mname.empty() && mname.front() == '%') {
                        mname.erase(0, 1);
                    }
                    m_ectx.m_ctx.source().output_append(m_ectx.m_out, var_cpp + "::" + mname + "(");
                    emitCallArgs(call);
                    return;
                }
                // Член variant: Value.RED → c_Value::c_RED (тип члена).
                const std::string member_cpp = utils::name_to_cpp(n.m_right->text());
                m_ectx.m_ctx.source().output_append(m_ectx.m_out, var_cpp + "::" + member_cpp);
                return;
            }
        }
        // Доступ к СТАТИЧЕСКОМУ члену нативного класса `Cls.field` / `Cls.st(...)`: левый операнд -
        // ИМЯ ТИПА (нативный класс) → эмитим `cppName::name` / `cppName::name(args)` (инклуд on-use).
        // Инстанс-член через имя типа семантика отклоняет до кодогена, поэтому здесь только статика.
        if (auto tid = m_ectx.m_ctx.types().findType(n.m_left->text())) {
            if (m_ectx.m_ctx.types().isNativeClassType(*tid)) {
                const std::string_view cpp = m_ectx.m_ctx.types().nativeClassCppName(*tid);
                if (!cpp.empty()) {
                    std::string mname = n.m_right ? std::string(n.m_right->text()) : std::string();
                    const CallExpr* call = nullptr;
                    if (n.m_right && n.m_right->kind() == ParserToken::Kind::CallExpr) {
                        call = static_cast<const CallExpr*>(n.m_right.get());
                        mname = call->m_callee ? std::string(call->m_callee->text()) : mname;
                    }
                    const std::string bare = std::string(utils::bare_name(mname));
                    m_driver.m_type.recordUsedType(*tid); // инклуд @[include] on-use
                    m_ectx.m_ctx.source().output_append(m_ectx.m_out, cpp);
                    m_ectx.m_ctx.source().output_append(m_ectx.m_out, "::");
                    m_ectx.m_ctx.source().output_append(m_ectx.m_out, bare);
                    if (call) {
                        m_ectx.m_ctx.source().output_append(m_ectx.m_out, "(");
                        emitCallArgs(*call);
                    }
                    return;
                }
            }
        }
    }
    if (n.m_right && n.m_right->kind() == ParserToken::Kind::CallExpr) {
        const auto& call = static_cast<const CallExpr&>(*n.m_right);
        if (call.m_callee) {
            // Метод на объекте: (объект).<нативный_член>(args). Нативность/константность метода -
            // из полного ключа (findMethodInfo: '%' нативный, '^' константный); нативное имя - из
            // ключа (срез '%'/'^'). const-вызов `obj.method^()` - attr::ReadOnly на ВЫЗОВЕ
            // (convertAttrsToNode/CallExpr) → const_cast<const T&>(obj) (гарантированно const-перегрузка).
            // const_cast-тип T - из TypeId объекта (n.lhsType, сохранён семантикой; кодген не может
            // восстановить его для локальной переменной - скоуп-стек сброшен). Fallback - резолв имени.
            TypeId objType = (n.lhsType != INVALID_TYPE_ID) ? m_ectx.m_ctx.types().getCanonicalTypeId(n.lhsType) : INVALID_TYPE_ID;
            if (objType == INVALID_TYPE_ID && n.m_left && n.m_left->kind() == ParserToken::Kind::Ident) {
                if (auto t = m_driver.m_type.resolveTypeIdByName(n.m_left->text())) {
                    objType = m_ectx.m_ctx.types().getCanonicalTypeId(*t);
                }
            }
            // Нативное имя: из полного ключа совпавшего метода (алиас → ключ цели); иначе - как есть.
            std::string mname(call.m_callee->text());
            std::string native;
            if (objType != INVALID_TYPE_ID) {
                if (auto mi = m_ectx.m_ctx.types().findMethodInfo(objType, mname)) {
                    native = utils::bare_name(mi->key); // срез '%'/'^' → нативное имя (count/size/...)
                }
            }
            if (native.empty()) {
                native = mname;
                if (!native.empty() && native.front() == '%') {
                    native.erase(0, 1);
                }
            }
            // Пользовательский Record-тип (Struct/Class): метод эмитится как обычный член struct
            // с манглингом `c_` (единый манлинг с эмиссией метода в emitRecordDecl).
            if (objType != INVALID_TYPE_ID && m_ectx.m_ctx.types().isRecordType(objType) && !native.empty()) {
                native = utils::name_to_cpp(native);
            }
            // Перегруженный пользовательский метод: уникальный суффикс (выбор сделан СЕМАНТИКОЙ;
            // кодоген не пере-резолвит). Совпадает с суффиксом объявления метода в struct.
            if (!n.resolvedMethodSuffix.empty()) {
                native += n.resolvedMethodSuffix;
            }
            // const-вызов `obj.method^()` - attr::ReadOnly на вызове.
            const bool constCall = call.as_attr() && call.as_attr()->has_attr(m_ectx.m_ctx.attrs(), attr::ReadOnly);
            if (constCall) {
                m_ectx.m_ctx.source().output_append(m_ectx.m_out, "const_cast<const ");
                if (auto ct = m_driver.m_type.resolveCppTypeId(objType, "Range.Const")) {
                    m_ectx.m_ctx.source().output_append(m_ectx.m_out, ct->first);
                } else {
                    m_ectx.m_ctx.source().output_append(m_ectx.m_out, "std::any");
                }
                m_ectx.m_ctx.source().output_append(m_ectx.m_out, "&>(");
                m_driver.emitExpr(n.m_left.get());
                m_ectx.m_ctx.source().output_append(m_ectx.m_out, ")");
            } else {
                m_ectx.m_ctx.source().output_append(m_ectx.m_out, "(");
                m_driver.emitExpr(n.m_left.get());
                m_ectx.m_ctx.source().output_append(m_ectx.m_out, ")");
            }
            m_ectx.m_ctx.source().output_append(m_ectx.m_out, ".");
            m_ectx.m_ctx.source().output_append(m_ectx.m_out, native);
            m_ectx.m_ctx.source().output_append(m_ectx.m_out, "(");
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
    // `.at("...")`. lhsType ставит семантика; fallback - резолв имени (как у метода).
    if (n.m_right && n.m_right->kind() == ParserToken::Kind::Ident) {
        TypeId objType = (n.lhsType != INVALID_TYPE_ID) ? m_ectx.m_ctx.types().getCanonicalTypeId(n.lhsType) : INVALID_TYPE_ID;
        if (objType == INVALID_TYPE_ID && n.m_left && n.m_left->kind() == ParserToken::Kind::Ident) {
            if (auto t = m_driver.m_type.resolveTypeIdByName(n.m_left->text())) {
                objType = m_ectx.m_ctx.types().getCanonicalTypeId(*t);
            }
        }
        if (objType != INVALID_TYPE_ID && m_ectx.m_ctx.types().isNativeClassType(objType)) {
            std::string fname(n.m_right->text());
            if (!fname.empty() && fname.front() == '%') {
                fname.erase(0, 1); // нативное имя поля (без '%')
            }
            m_ectx.m_ctx.source().output_append(m_ectx.m_out, "(");
            m_driver.emitExpr(n.m_left.get());
            m_ectx.m_ctx.source().output_append(m_ectx.m_out, ").");
            m_ectx.m_ctx.source().output_append(m_ectx.m_out, fname);
            return;
        }
        // Поле пользовательского Record-типа (Struct/Class): `obj.field` → `(obj).c_field`
        // (манглинг `c_`, как у полей при эмиссии struct; семантика проверила наличие поля).
        if (objType != INVALID_TYPE_ID && m_ectx.m_ctx.types().isRecordType(objType)) {
            std::string fname = utils::name_to_cpp(n.m_right->text());
            m_ectx.m_ctx.source().output_append(m_ectx.m_out, "(");
            m_driver.emitExpr(n.m_left.get());
            m_ectx.m_ctx.source().output_append(m_ectx.m_out, ").");
            m_ectx.m_ctx.source().output_append(m_ectx.m_out, fname);
            return;
        }
    }
    emitDictElementAccess(n);
}

} // namespace trust
