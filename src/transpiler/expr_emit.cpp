// Generated: src/transpiler/expr_emit.cpp
#include "transpiler/expr_emit.hpp"
#include "transpiler/transpiler.hpp"
#include "transpiler/emit_common.hpp"
#include "ast/ast_nodes.hpp"
#include "ast/attr_builtin.hpp"
#include "ast/ident_name.hpp"
#include "ast/kind_visitor.hpp"
#include "ast/token_type.hpp"
#include "diag/context.hpp"
#include "diag/registry.hpp"
#include "diag/base_diags.hpp"
#include "semantic/symbol_table.hpp"
#include "semantic/solver.hpp"
#include "syntax/term.h"
#include "types/registry.hpp"
#include "types/runtime_symbols.hpp"
#include "types/intrinsics.hpp"
#include "types/type_id.hpp"
#include "types/type_names.hpp"
#include "transpiler/diag.hpp"
#include "utils/operators.hpp"
#include "utils/strings.hpp"
#include <format>
#include <memory>

namespace trust {

void ExprEmitter::emitBinaryOpRaw(const Binary& binary_node) {
    const auto op = binary_node.text();
    // Потоковый вывод бинарного оператора, включая '//'/'//=' (целочисленное деление).
    if (utils::isIntDivOp(op) && !utils::isCompoundAssignOp(op)) {
        m_ectx.m_ctx.source().output_append(m_ectx.m_out, "static_cast<int64_t>(");
        m_driver.emitExpr(binary_node.m_left.get());
        m_ectx.m_ctx.source().output_append(m_ectx.m_out, ") / static_cast<int64_t>(");
        m_driver.emitExpr(binary_node.m_right.get());
        m_ectx.m_ctx.source().output_append(m_ectx.m_out, ")");
    } else if (m_ectx.m_exprDepth == 0 && utils::isIntDivOp(op) && utils::isCompoundAssignOp(op)) {
        // //= - только statement (присваивание); как вложенное выражение не используется.
        m_driver.emitExpr(binary_node.m_left.get());
        m_ectx.m_ctx.source().output_append(m_ectx.m_out, " = static_cast<int64_t>(");
        m_driver.emitExpr(binary_node.m_left.get());
        m_ectx.m_ctx.source().output_append(m_ectx.m_out, ") / static_cast<int64_t>(");
        m_driver.emitExpr(binary_node.m_right.get());
        m_ectx.m_ctx.source().output_append(m_ectx.m_out, ")");
    } else {
        // LHS: для простого присвоения "=" - это адрес хранения (any_cast неприменим);
        // иначе (арифметика/составные) - значение, может требовать std::any_cast.
        const bool plainAssign = (binary_node.kind() == ParserToken::Kind::AssignOp && utils::isPlainAssignOp(op));
        if (binary_node.m_left) {
            if (plainAssign) {
                m_driver.emitExpr(binary_node.m_left.get());
            } else {
                emitBinaryOperand(binary_node.m_left.get(), binary_node.lhsType, binary_node.commonType);
            }
        }
        m_ectx.m_ctx.source().output_append(m_ectx.m_out, " " + std::string(op) + " ");
        if (binary_node.m_right) {
            emitBinaryOperand(binary_node.m_right.get(), binary_node.rhsType, binary_node.commonType);
        }
    }
}

void ExprEmitter::emitBinaryOperand(const AstNodeBase* operand, TypeId operandType, TypeId castType) {
    // std::any-операнд: привести к конкретному типу (commonType). Элемент словаря возвращает
    // trust::TypedValue → типизированный доступ .getAs<Cpp>() (fast-path variant); переменная
    // типа std::any → std::any_cast<Cpp>.
    if (operand && operandType != INVALID_TYPE_ID && castType != INVALID_TYPE_ID) {
        if (isAnyType(operandType, m_ectx.m_ctx.types())) {
            auto cpp = m_driver.m_type.emitTypeName(castType, "");
            if (cpp) {
                const bool dictElement = operand->kind() == ParserToken::Kind::MemberAccess || operand->kind() == ParserToken::Kind::ArrayAccess;
                if (dictElement) {
                    m_ectx.m_ctx.source().output_append(m_ectx.m_out, "(");
                    m_driver.emitExpr(operand);
                    m_ectx.m_ctx.source().output_append(m_ectx.m_out, ").getAs<" + *cpp + ">()");
                } else {
                    // std::any → универсальный runtime-конвертер anyToInt64. Точный
                    // `std::any_cast<Cpp>` ломался на гетерогенных значениях словаря (bool, int8,
                    // int64...), а конвертер принимает любую числовую/bool-категорию (dict.hpp).
                    // anyToInt64 требует <any> - подключаем по-типу (тип операнда Any).
                    m_driver.m_type.recordUsedType(operandType);
                    m_ectx.m_ctx.source().output_append(m_ectx.m_out, "trust::detail::anyToInt64(");
                    m_driver.emitExpr(operand);
                    m_ectx.m_ctx.source().output_append(m_ectx.m_out, ")");
                }
                return;
            }
        }
    }
    m_driver.emitExpr(operand);
}

void ExprEmitter::emitBinaryStmtOrExpr(const Binary& binary_node) {
    // statement-root (m_ectx.m_exprDepth==0, как ребёнок SemicolonStmt): текст без внешних скобок;
    // маппинг и ';' добавляет SemicolonStmt. Вложенное выражение (m_ectx.m_exprDepth>0): '(lhs op rhs)'.
    const bool stmt = (m_ectx.m_exprDepth == 0);
    if (stmt) {
        emitBinaryOpRaw(binary_node);
    } else {
        m_ectx.m_ctx.source().output_append(m_ectx.m_out, "(");
        emitBinaryOpRaw(binary_node);
        m_ectx.m_ctx.source().output_append(m_ectx.m_out, ")");
    }
}

void ExprEmitter::visit_Attr(const Sequence&) {
}

void ExprEmitter::visit_ArgNode(const ArgNode&) {
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
        m_driver.emitPlaceholderExpr(m_ectx.m_out);
        return;
    }
    // emitTypeName резолвит C++-имя и записывает инклуды контейнера (сайд-эффект); сам
    // результат не нужен - ветка кодогенерации выбирается по каноническому TypeId ниже.
    if (!m_driver.m_type.emitTypeName(n.lhsType, "")) {
        m_ectx.m_ctx.report(n.range(), diag::DiagId::ParseError, "unable to resolve container type for append '[]='");
        return;
    }
    const TypeRegistry& reg = m_ectx.m_ctx.types();
    const TypeId cid = reg.getCanonicalTypeId(n.lhsType);
    const TypeId dictId = reg.getType(type::Dict);
    const TypeId strCharId = reg.getType(type::StrChar);
    const TypeId strWideId = reg.getType(type::StrWide);

    m_driver.emitExpr(n.m_left.get());
    if (cid == dictId) {
        if (n.m_right && n.m_right->kind() == ParserToken::Kind::Ellipsis) {
            // Spread-merge `X []= ... dict` → `X.extend(dict)`: добор ВСЕХ элементов словаря-
            // операнда (аналог extend/update). Операнд - m_body[0] узла Ellipsis (литерал
            // → trust::Dict{...}, переменная → c_d2, выражение → его значение).
            m_ectx.m_ctx.source().output_append(m_ectx.m_out, ".extend(");
            const auto& ell = static_cast<const Sequence&>(*n.m_right);
            if (!ell.m_body.empty() && ell.m_body[0]) {
                m_driver.emitExpr(ell.m_body[0].get());
            } else {
                m_ectx.m_ctx.source().output_append(m_ectx.m_out, "{}");
            }
            m_ectx.m_ctx.source().output_append(m_ectx.m_out, ")");
            return;
        }
        // Dict: только push_back(name, value); безымянный append - пустое имя (позиционный элемент).
        m_ectx.m_ctx.source().output_append(m_ectx.m_out, ".push_back(\"\", ");
        emitTypedDictValue(n.m_right.get(), n.resultType);
        m_ectx.m_ctx.source().output_append(m_ectx.m_out, ")");
    } else if (cid == strCharId || cid == strWideId) {
        const bool wide = (cid == strWideId);
        m_ectx.m_ctx.source().output_append(m_ectx.m_out, ".append(");
        if (wide && n.m_right && n.m_right->kind() == ParserToken::Kind::StrChar) {
            // Узкий символьный литерал 'c' в wide-контейнер → wide-литерал L"c" (char→wchar).
            // Источник - StrChar (одинарные кавычки): голый " экранируем в \" (см. visit_StrChar).
            const std::string_view t = n.m_right->text();
            std::string body;
            body.reserve(t.size());
            for (size_t i = 0; i < t.size(); ++i) {
                const char c = t[i];
                if (c == '\\' && i + 1 < t.size()) { // escape-последовательность - как есть
                    body += c;
                    body += t[++i];
                } else if (c == '"') {
                    body += "\\\"";
                } else {
                    body += c;
                }
            }
            m_ectx.m_ctx.source().output_append(m_ectx.m_out, std::format("L\"{}\"", body));
        } else {
            m_driver.emitExpr(n.m_right.get());
        }
        m_ectx.m_ctx.source().output_append(m_ectx.m_out, ")");
    } else {
        m_ectx.m_ctx.report(n.range(), diag::DiagId::ParseError, "append '[]=' is not supported for container type '{}'", reg.getFullTypeName(cid));
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
                    if (call.m_args) {
                        for (size_t i = 0; i < call.m_args->size(); ++i) {
                            if (i) {
                                m_ectx.m_ctx.source().output_append(m_ectx.m_out, ", ");
                            }
                            m_driver.emitExpr((*call.m_args)[i].get());
                        }
                    }
                    m_ectx.m_ctx.source().output_append(m_ectx.m_out, ")");
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
                    if (call.m_args) {
                        for (size_t i = 0; i < call.m_args->size(); ++i) {
                            if (i) {
                                m_ectx.m_ctx.source().output_append(m_ectx.m_out, ", ");
                            }
                            m_driver.emitExpr((*call.m_args)[i].get());
                        }
                    }
                    m_ectx.m_ctx.source().output_append(m_ectx.m_out, ")");
                    return;
                }
                // Член variant: Value.RED → c_Value::c_RED (тип члена).
                const std::string member_cpp = utils::name_to_cpp(n.m_right->text());
                m_ectx.m_ctx.source().output_append(m_ectx.m_out, var_cpp + "::" + member_cpp);
                return;
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
            if (call.m_args) {
                for (size_t i = 0; i < call.m_args->size(); ++i) {
                    if (i) {
                        m_ectx.m_ctx.source().output_append(m_ectx.m_out, ", ");
                    }
                    m_driver.emitExpr((*call.m_args)[i].get());
                }
            }
            m_ectx.m_ctx.source().output_append(m_ectx.m_out, ")");
            return;
        }
    }
    if (n.tupleIndex >= 0) {
        emitTupleElementAccess(n);
        return;
    }
    emitDictElementAccess(n);
}

// Динамический доступ по индексу: d[expr].
void ExprEmitter::visit_ArrayAccess(const Binary& n) {
    if (n.tupleIndex >= 0) {
        emitTupleElementAccess(n);
        return;
    }
    // Доступ к элементу массива `a[i]` (структурный Array-тип): `(obj).at(idx)` -
    // безопасный (bounds-check) доступ, как для словаря. lhsType ставит семантика
    // (resolveArrayAccess).
    if (n.lhsType != INVALID_TYPE_ID && m_ectx.m_ctx.types().isArrayType(n.lhsType)) {
        if (n.m_left) {
            m_ectx.m_ctx.source().output_append(m_ectx.m_out, "(");
            m_driver.emitExpr(n.m_left.get());
            m_ectx.m_ctx.source().output_append(m_ectx.m_out, ").at(");
        }
        if (n.m_right) {
            m_driver.emitExpr(n.m_right.get());
        }
        m_ectx.m_ctx.source().output_append(m_ectx.m_out, ")");
        return;
    }
    emitDictElementAccess(n);
}

// Кортеж: `t.name` / `t.0` / `t[idx]` → `std::get<index>(obj)`. Индекс резолвит семантика
// (Binary::tupleIndex). std::get возвращает ссылку на конкретный элемент std::tuple.
void ExprEmitter::emitTupleElementAccess(const Binary& n) {
    m_driver.m_type.recordRequiredInclude("#include <tuple>");
    m_ectx.m_ctx.source().output_append(m_ectx.m_out, "std::get<");
    m_ectx.m_ctx.source().output_append(m_ectx.m_out, std::to_string(n.tupleIndex));
    m_ectx.m_ctx.source().output_append(m_ectx.m_out, ">(");
    if (n.m_left) {
        m_driver.emitExpr(n.m_left.get());
    }
    m_ectx.m_ctx.source().output_append(m_ectx.m_out, ")");
}

// Литерал - всегда только текст (statement-позицию оборачивает SemicolonStmt, добавляя ';').
void ExprEmitter::visit_IntLiteral(const Literal& n) {
    m_ectx.m_ctx.source().output_append(m_ectx.m_out, n.text());
}

void ExprEmitter::visit_RationalLiteral(const Literal& n) {
    // Рациональный литерал `num\den` создаётся непосредственно из строки: эмитим
    // `trust::Rational("num\den")` - парсинг `num\den` выполняет однострочный конструктор
    // Rational. В C++-строковом литерале обратная косая экранируется (`\` → `\\`).
    // Инклуд Rational-типа записывается через emitTypeName (единая точка сбора).
    if (n.typeId != INVALID_TYPE_ID) {
        m_driver.m_type.emitTypeName(n.typeId, "");
    } else if (auto rid = m_ectx.m_ctx.types().findType(type::Rational)) {
        m_driver.m_type.emitTypeName(*rid, "Rational");
    }
    const std::string_view t = n.text();
    std::string escaped;
    escaped.reserve(t.size() * 2);
    for (const char c : t) {
        if (c == '\\') {
            escaped += "\\\\";
        } else {
            escaped += c;
        }
    }
    m_ectx.m_ctx.source().output_append(m_ectx.m_out, std::format("trust::Rational(\"{}\")", escaped));
}

void ExprEmitter::visit_StrChar(const Literal& n) {
    // Строка в одинарных кавычках '…' → StrChar → обычная "…". Ограничитель StrChar - ',
    // поэтому голый " в нём допустим; в C++-литерале "…" экранируем его в \" (иначе литерал
    // обрывается). Escape-последовательности trust сохранены как есть и валидны в C++ без
    // изменений - копируем их (backslash + следующий символ) не трогая.
    const std::string_view t = n.text();
    std::string body;
    body.reserve(t.size());
    for (size_t i = 0; i < t.size(); ++i) {
        const char c = t[i];
        if (c == '\\' && i + 1 < t.size()) { // escape-последовательность - как есть
            body += c;
            body += t[++i];
        } else if (c == '"') {
            body += "\\\"";
        } else {
            body += c;
        }
    }
    m_ectx.m_ctx.source().output_append(m_ectx.m_out, std::format("\"{}\"", body));
}

void ExprEmitter::visit_StrWide(const Literal& n) {
    // Строка в двойных кавычках "…" → StrWide → wide-строка L"…". Голый " в StrWide невозможен
    // (закрыл бы строку), кавычка вставляется как \" и уже валидна в C++ - экранирование не нужно.
    m_ectx.m_ctx.source().output_append(m_ectx.m_out, std::format("L\"{}\"", n.text()));
}

void ExprEmitter::visit_FloatLiteral(const Literal& n) {
    m_ectx.m_ctx.source().output_append(m_ectx.m_out, n.text());
}

// Контекст-макросы (@__NAMESPACE__, @::, @__FUNCTION__, @__FUNCSIG__, @__FUNCDNAME__)
// раскрываются анализатором (заменяются на Literal/StrChar или имя) до транспиляции.
// До транспилятора такие узлы доходить не должны - это недостижимая ветка (сигнал бага).
void ExprEmitter::visit_ContextMacro(const ContextMacro&) {
    FAULT("ContextMacro reached the transpiler (must be expanded by the analyzer)");
}

// EmbedExpr - raw text: statement с маппингом, выражение - только текст.
void ExprEmitter::visit_EmbedExpr(const AstNodeAttr& n) {
    // Узкий чек ТОЛЬКО по тексту вставки: рантайм-функции/символы → запись их заголовков
    // (нужно для заголовков, которые физически отсутствуют в каталоге подключаемых файлов
    // и должны быть извлечены из trust-runtime).
    m_driver.m_type.recordRuntimeSymbolsInText(n.text());
    if (m_ectx.m_exprDepth > 0) {
        // C++-вставка: текст остаётся как есть, кроме trust-маркеров $/@ → name_to_cpp.
        std::string converted = utils::transform_embed_cpp(n.text());
        m_driver.emitExprText(converted);
    } else {
        MapperScope scope(m_ectx.m_ctx.source(), n.range(), m_ectx.m_out);
        m_driver.emitExpr(&n);
    }
}

// Document - документирующий комментарий: эмитится в statement-позиции. Trust-доки `##`/`##<`
// невалидны в C++ (префикс '#' - препроцессор), поэтому нормализуются в однострочные C++ `///`/`///<`;
// `/** … */` и `///` выводится как есть. Подавление (флаг -Wno-comments) - на уровне обхода.
void ExprEmitter::visit_Document(const AstNodeAttr& n) {
    if (m_ectx.m_exprDepth > 0) {
        return;
    }
    std::string_view t = n.text();
    if (t.starts_with("##")) {
        m_ectx.m_ctx.source().output_append(m_ectx.m_out, "///");
        m_ectx.m_ctx.source().output_append(m_ectx.m_out, t.substr(2));
    } else {
        m_ectx.m_ctx.source().output_append(m_ectx.m_out, t);
    }
}

void ExprEmitter::visit_Ident(const IdentName& n) {
    // Пост-условие функции: имя функции = возвращаемое значение (temp, см. visit_ReturnStmt).
    if (!m_ectx.m_resultCpp.empty() && n.text() == m_ectx.m_resultName) {
        m_driver.emitExprText(m_ectx.m_resultCpp);
        return;
    }
    // Нативный импорт-алиас: вызов trust-имени переписывается в прямой вызов нативного C++-имени.
    auto it = m_ectx.m_nativeImports.find(std::string(n.text()));
    if (it != m_ectx.m_nativeImports.end()) {
        m_driver.emitExprText(it->second);
        return;
    }
    // Идентификатор в выражении: манглинг trust-имени в C++-идентификатор (x → c_x).
    m_driver.emitExprText(utils::name_to_cpp(n.text()));
}

void ExprEmitter::visit_TypeName(const IdentType& n) {
    m_driver.emitExprText(n.text());
}

// CallExpr - только выражение: callee(args). Statement-позицию оборачивает SemicolonStmt.
void ExprEmitter::visit_CallExpr(const CallExpr& n) {
    // Интринсик языка (например `trust::intrinsic_assert`): разворачивается на этапе генерации
    // (emitIntrinsic), а не эмитится как обычный вызов (см. types/intrinsics.hpp).
    if (n.m_callee) {
        if (const auto id = findIntrinsicByName(n.m_callee->text())) {
            emitIntrinsic(*id, n);
            return;
        }
    }
    // Строка-формат: `"{}"(args)` / `'{}'(args)` - callee строковый литерал → std::format.
    if (n.m_callee && (n.m_callee->kind() == ParserToken::Kind::StrWide || n.m_callee->kind() == ParserToken::Kind::StrChar)) {
        emitFormatCall(n);
        return;
    }
    // Рантайм-функции (print/assert): заголовки по имени callee (точное совпадение с
    // рантайм-символом; ведущий '%' срезается, как в семантике) - без скана всего буфера.
    if (n.m_callee) {
        if (const auto id = findRuntimeSymbolByName(n.m_callee->text())) {
            m_driver.m_type.recordRuntimeSymbolHeaders(*id);
        }
    }
    m_driver.emitExpr(n.m_callee.get());
    m_ectx.m_ctx.source().output_append(m_ectx.m_out, "(");
    if (n.m_args) {
        for (size_t i = 0; i < n.m_args->size(); ++i) {
            if (i) {
                m_ectx.m_ctx.source().output_append(m_ectx.m_out, ", ");
            }
            m_driver.emitExpr((*n.m_args)[i].get());
        }
    }
    m_ectx.m_ctx.source().output_append(m_ectx.m_out, ")");
}

// Строка-формат `"{}"(args)` / `'{}'(args)` → `std::format(fmt, args...)`. Ширина строки
// задаётся Kind callee: StrWide → L"…" (std::format<wchar_t>), StrChar → "…". Требуется
// #include <format> в генерируемом C++ (записываем через recordRequiredInclude).
void ExprEmitter::emitFormatCall(const CallExpr& n) {
    m_driver.m_type.recordRequiredInclude("#include <format>");
    m_ectx.m_ctx.source().output_append(m_ectx.m_out, "std::format(");
    m_driver.emitExpr(n.m_callee.get());
    if (n.m_args) {
        for (const auto& arg : *n.m_args) {
            m_ectx.m_ctx.source().output_append(m_ectx.m_out, ", ");
            m_driver.emitExpr(arg.get());
        }
    }
    m_ectx.m_ctx.source().output_append(m_ectx.m_out, ")");
}

// Expression-kind'ы без реализованной генерации: как statement - no-op,
// как выражение - placeholder "{}" (сохранение прежнего default из emitExpr).
void ExprEmitter::visit_VarRef(const AstNodeAttr&) {
    m_driver.emitPlaceholderExpr(m_ectx.m_out);
}
// namespace
// Литерал массива `[1,2:Int8,3,]` / `[1,2,3,]:Int32` → `std::vector<Elem>{...}` (mut)
// или `std::array<Elem,N>{...}` (константная/фиксированная форма). Тип элемента - единый
// источник семантики: результат анализа ArrayInit (структурный Array<Elem>, resolveCppTypeId).
void ExprEmitter::visit_ArrayInit(const DictLiteralNode& n) {
    if (n.arrayType == INVALID_TYPE_ID) {
        m_driver.emitPlaceholderExpr(m_ectx.m_out);
        return;
    }
    emitArrayLiteral(n, n.arrayType);
}

// Эмитит литерал/конструкцию массива: `std::vector<Elem>{v1, v2, ...}` (mutable) либо
// `std::array<Elem,N>{v1, ...}` (константная форма). Тип контейнера берётся из структурного
// Array<Elem> через resolveCppTypeId (записывает инклуды <vector>/<array>). Элементы - ArgNode
// (значение в m_value); именованные/пустые отбрасываются.
void ExprEmitter::emitArrayLiteral(const DictLiteralNode& n, TypeId arrayType) {
    // Многомерный массив (несколько размерностей или вложенные литералы): генерация тензора
    // не реализована → диагностика (анализ/регистрация типа при этом работают).
    if (isMultiDimArray(arrayType, m_ectx.m_ctx.types())) {
        m_ectx.m_ctx.report(n.range(), diag::DiagId::ParseError, "многомерные массивы пока не реализованы: транслируются как тензоры (LibTorch)");
        m_driver.emitPlaceholderExpr(m_ectx.m_out);
        return;
    }
    auto container = m_driver.m_type.emitTypeName(arrayType, "Array");
    if (!container) {
        m_driver.emitPlaceholderExpr(m_ectx.m_out);
        return;
    }
    m_ectx.m_ctx.source().output_append(m_ectx.m_out, *container + "{");
    bool first = true;
    for (const auto& el : dictElements(n)) {
        if (!el.value) {
            continue;
        }
        if (!first) {
            m_ectx.m_ctx.source().output_append(m_ectx.m_out, ", ");
        }
        first = false;
        m_driver.emitExpr(el.value);
    }
    m_ectx.m_ctx.source().output_append(m_ectx.m_out, "}");
}

// Литерал словаря/кортежа: `(1, two="2", name=3,)` → trust::Dict{ {"", expr}, {"two", expr}, ... }.
// Контракт: все элементы m_body - Binary(AssignOp) (left=Ident-метка или пустой, right=значение),
// строятся из канонических пар грамматики `args` (term_to_ast::visit_DICT). Тип значения -
// единый источник семантики: Binary::resultType (из resolvedType), см. emitTypedDictValue.
// Литерал словаря/конструкция/каст и кортеж. kind==Tuple → visit_Tuple (std::tuple);
// типизированный `:Type(...)`/`(...):Type` (не Tuple) → emitTypedConstruction (каст/конструктор);
// голый `(...)` → emitDictLiteralBody (trust::Dict). Контракт элементов - Binary(AssignOp)
// (term_to_ast::visit_DICT); тип значения - единый источник Binary::resultType.
void ExprEmitter::visit_DictLiteral(const DictLiteralNode& n) {
    if (n.m_type) {
        emitTypedConstruction(n);
        return;
    }
    emitDictLiteralBody(n);
}

// Кортеж `:Tuple(...)` / `(...):Tuple` (kind==Tuple, выставлен анализатором по типу из реестра)
// → std::tuple (именованные элементы конвертируются в индексы по порядку; имена в C++ не попадают).
void ExprEmitter::visit_Tuple(const DictLiteralNode& n) {
    m_driver.m_type.recordRequiredInclude("#include <tuple>");
    m_ectx.m_ctx.source().output_append(m_ectx.m_out, "std::make_tuple(");
    bool first = true;
    for (const auto& el : dictElements(n)) {
        if (!el.value) {
            continue;
        }
        if (!first) {
            m_ectx.m_ctx.source().output_append(m_ectx.m_out, ", ");
        }
        first = false;
        m_driver.emitExpr(el.value);
    }
    m_ectx.m_ctx.source().output_append(m_ectx.m_out, ")");
}

// Литерал диапазона `start..stop` / `start..stop..step` → `trust::Range<Elem>(start, stop[, step])`.
// Универсальный тип `:Range` (как `:Dict`): элементный тип Elem - join типов start/stop/step,
// вычисленный семантикой (RangeExpr::elementType из analyzeRangeExpr) и параметризующий шаблон
// trust::Range<T> при кодогенерации. Рациональные значения оборачиваются trust::Rational(...)
// самим visit_RationalLiteral. Записывает @trust/range.hpp (механизм как у visit_Tuple/<tuple>).
void ExprEmitter::visit_RangeExpr(const RangeExpr& node) {
    // range.hpp самодостаточен, но для toDict/элементов нужны dict.hpp и rational.hpp - их
    // пайплайн извлекает из рантайма ТОЛЬКО по прямому запросу (транзитивные инклуды не
    // отслеживаются), поэтому перечисляем весь транзитивный набор (как у Dict-типа).
    m_driver.m_type.recordRequiredInclude("@trust/range.hpp");
    m_driver.m_type.recordRequiredInclude("@trust/dict.hpp");
    m_driver.m_type.recordRequiredInclude("@trust/rational.hpp");
    // Элементный C++-тип - из семантики (join start/stop/step). INVALID → универсальный Any.
    TypeId elemTid = node.elementType;
    if (elemTid == INVALID_TYPE_ID) {
        elemTid = m_ectx.m_ctx.types().getType(type_generic::Any);
    }
    std::string elemCpp;
    if (auto n = m_driver.m_type.emitTypeName(elemTid, "Range")) {
        elemCpp = std::move(*n);
    } else {
        elemCpp = "std::any";
    }
    m_ectx.m_ctx.source().output_append(m_ectx.m_out, "trust::Range<" + elemCpp + ">(");
    m_driver.emitExpr(node.start().get());
    m_ectx.m_ctx.source().output_append(m_ectx.m_out, ", ");
    m_driver.emitExpr(node.stop().get());
    if (node.hasStep()) {
        m_ectx.m_ctx.source().output_append(m_ectx.m_out, ", ");
        m_driver.emitExpr(node.step().get());
    }
    m_ectx.m_ctx.source().output_append(m_ectx.m_out, ")");
}

// Тело словаря trust::Dict{ {"name", TypedValue}, ... } - для голого `(...)` и для типизированного
// с аннотацией, резолвящейся в сам Dict.
void ExprEmitter::emitDictLiteralBody(const DictLiteralNode& n) {
    // Имя и заголовки Dict-типа - через emitTypeName (единая точка сбора; без хардкода пути).
    auto dictId = m_ectx.m_ctx.types().findType(type::Dict);
    if (!dictId) {
        m_driver.emitPlaceholderExpr(m_ectx.m_out);
        return;
    }
    auto dictName = m_driver.m_type.emitTypeName(*dictId, "Dict");
    if (!dictName) {
        m_driver.emitPlaceholderExpr(m_ectx.m_out);
        return;
    }
    m_ectx.m_ctx.source().output_append(m_ectx.m_out, *dictName + "{");
    bool first = true;
    for (const auto& el : dictElements(n)) {
        if (!el.value) {
            continue;
        }
        if (!first) {
            m_ectx.m_ctx.source().output_append(m_ectx.m_out, ", ");
        }
        first = false;
        // Имя поля (у безымянного - пустая строка). Экранируем для C++-строки.
        m_ectx.m_ctx.source().output_append(m_ectx.m_out, "{\"" + utils::escape_cpp_string(el.name) + "\", ");
        // Элемент: TypedValue{kind, значение} - конструктор размещает в быструю ветку variant.
        emitTypedDictValue(el.value, el.resultType);
        m_ectx.m_ctx.source().output_append(m_ectx.m_out, "}");
    }
    m_ectx.m_ctx.source().output_append(m_ectx.m_out, "}");
}

// Типизированная конструкция/каст `:Type(...)`/`(...):Type` (не Tuple). Решение по типу из реестра:
//   - если аннотация резолвится в сам универсальный Dict → обычный словарь (emitDictLiteralBody);
//   - один элемент → каст trust::checked_cast<Type>(v) / trust::any_to<Type>(v);
//   - несколько элементов → конструктор Type(v1, ..., vN).
void ExprEmitter::emitTypedConstruction(const DictLiteralNode& n) {
    const AstNodeBase* typeNode = n.m_type.get();
    if (!typeNode || typeNode->kind() != ParserToken::Kind::TypeName) {
        m_driver.emitPlaceholderExpr(m_ectx.m_out);
        return;
    }
    // Конструкция массива `:Array(...)` / `:Array^(...)`: семантика интернировала структурный
    // Array<Elem> (DictLiteralNode::arrayType) → эмитим std::vector<Elem>{...}/std::array<Elem,N>{...}.
    if (n.arrayType != INVALID_TYPE_ID) {
        emitArrayLiteral(n, n.arrayType);
        return;
    }
    // Аннотация = сам Dict → словарь (не конструктор).
    if (auto tid = m_driver.m_type.resolveTypeIdByName(typeNode->text()); tid.has_value()) {
        if (auto dictId = m_ectx.m_ctx.types().findType(type::Dict);
            dictId && m_ectx.m_ctx.types().getCanonicalTypeId(*tid) == m_ectx.m_ctx.types().getCanonicalTypeId(*dictId)) {
            emitDictLiteralBody(n);
            return;
        }
    }
    // Тип-цель → C++ имя (и запись инклудов типа через emitTypeNameForNode). None/Void → "void".
    std::string typeCpp;
    const std::string_view tt = typeNode->text();
    if (tt == "Void" || tt == "None") {
        typeCpp = "void";
    } else {
        typeCpp = m_driver.m_type.emitTypeNameForNode(typeNode);
    }
    if (typeCpp.empty()) {
        m_driver.emitPlaceholderExpr(m_ectx.m_out);
        return; // emitTypeNameForNode уже вывел диагностику
    }
    // Операнды (значения элементов) в порядке m_body.
    std::vector<const AstNodeBase*> values;
    for (const auto& el : dictElements(n)) {
        if (el.value) {
            values.push_back(el.value);
        }
    }
    if (values.empty()) {
        m_driver.emitPlaceholderExpr(m_ectx.m_out);
        return;
    }
    if (values.size() == 1) {
        // Каст одного значения. Операнд - элемент словаря: any_to<Type> только когда тип поля
        // неизвестен (Any/INVALID); для конкретного типа - обычный checked_cast.
        const AstNodeBase* operand = values[0];
        bool operandIsAny = false;
        if (operand && (operand->kind() == ParserToken::Kind::MemberAccess || operand->kind() == ParserToken::Kind::ArrayAccess)) {
            const auto& b = static_cast<const Binary&>(*operand);
            // Вызов метода (obj.m(...)) возвращает КОНКРЕТНОЕ C++-значение нативного члена
            // (напр. Range.at(i) → int64), а НЕ trust::TypedValue: для него any_to неприменим -
            // используем checked_cast (как для любого конкретного значения). any_to - только
            // для доступа к элементу словаря (TypedValue), где тип поля неизвестен (Any/INVALID).
            const bool isMethodCall = (b.kind() == ParserToken::Kind::MemberAccess && b.m_right && b.m_right->kind() == ParserToken::Kind::CallExpr);
            if (!isMethodCall) {
                const TypeId rt = b.resultType;
                operandIsAny = (rt == INVALID_TYPE_ID || isAnyType(rt, m_ectx.m_ctx.types()));
            }
        }
        if (operandIsAny) {
            m_driver.m_type.recordRuntimeSymbolHeaders(RuntimeSymbolId::kAnyTo);
            m_ectx.m_ctx.source().output_append(m_ectx.m_out, "trust::any_to<" + typeCpp + ">(");
            m_driver.emitExpr(operand);
            m_ectx.m_ctx.source().output_append(m_ectx.m_out, ")");
            return;
        }
        m_driver.m_type.recordRuntimeSymbolHeaders(RuntimeSymbolId::kCheckedCast);
        m_ectx.m_ctx.source().output_append(m_ectx.m_out, "trust::checked_cast<" + typeCpp + ">(");
        m_driver.emitExpr(operand);
        m_ectx.m_ctx.source().output_append(m_ectx.m_out, ")");
        return;
    }
    // Конструктор нескольких значений: Type(v1, ..., vN).
    m_ectx.m_ctx.source().output_append(m_ectx.m_out, typeCpp + "(");
    bool first = true;
    for (const AstNodeBase* v : values) {
        if (!first) {
            m_ectx.m_ctx.source().output_append(m_ectx.m_out, ", ");
        }
        first = false;
        m_driver.emitExpr(v);
    }
    m_ectx.m_ctx.source().output_append(m_ectx.m_out, ")");
}

// Эмитит trust::TypedValue{kind, значение} для элемента словаря. kind - TypeKind значения,
// вычисленный семантикой (resolvedType) и сохранённый на элементе-AssignOp (Binary::resultType).
// Конструктор TypedValue сам размещает значение в быструю ветку std::variant (по группе kind:
// числа/bool/строки) либо в std::any-ветку (открытые типы, вложенный Dict).
// kind и C++-имя выводятся из TypeId без дублирования логики диапазонов/маппинга группа→имя.
// Единый предикат литералов - ast::is_literal_kind.
void ExprEmitter::emitTypedDictValue(const AstNodeBase* valueNode, TypeId tid) {
    const TypeKind kind = getKindFromId(tid);
    // C++-имя - через emitTypeName (единая точка: резолв + запись инклудов типа).
    auto cpp = m_driver.m_type.emitTypeName(tid, "");

    // kind (TypeKind) - 32-битная битовая кодировка типа. Печатаем в hex (0x…), чтобы были
    // наглядны разряды Group/Data/RefType/…; в C++-литерале эквивалентно десятичному значению.
    m_ectx.m_ctx.source().output_append(m_ectx.m_out, std::format("trust::TypedValue{{0x{:x}, ", kind));
    // Точный C++-тип значения из реестра для литералов. RationalLiteral уже эмитится как
    // trust::Rational(...) - не оборачиваем. Не-литералы (вложенный Dict, переменная, вызов) - как есть.
    if (cpp && !cpp->empty() && is_literal_kind(valueNode->kind()) && valueNode->kind() != ParserToken::Kind::RationalLiteral) {
        m_ectx.m_ctx.source().output_append(m_ectx.m_out, *cpp + "(");
        m_driver.emitExpr(valueNode);
        m_ectx.m_ctx.source().output_append(m_ectx.m_out, ")");
    } else {
        m_driver.emitExpr(valueNode);
    }
    m_ectx.m_ctx.source().output_append(m_ectx.m_out, "}");
}

// Каст/конструкция `:Type(...)`/`(...):Type` перенесён в visit_DictLiteral/emitTypedConstruction
// (единый узел DictLiteralNode, решение по типу из реестра). Kind CastExpr удалён.
void ExprEmitter::visit_RefMakeExpr(const Sequence&) {
    m_driver.emitPlaceholderExpr(m_ectx.m_out);
}

void ExprEmitter::visit_RefTakeExpr(const Sequence&) {
    m_driver.emitPlaceholderExpr(m_ectx.m_out);
}

void ExprEmitter::visit_Ellipsis(const Sequence&) {
    m_driver.emitPlaceholderExpr(m_ectx.m_out);
}

// Trust-контракты (единый узел TrustContract, kind в поле).
// В режиме --solver-mode=assert генерируются рантайм-проверки:
//   if (!(<cond>)) trust::trust__abort__("<file>", <line>, "<kind> '<cond-text>' violated", 1);
// (по образцу DSL-макроса @assert; trust__abort__ определён в trust/assert.hpp). В остальных
// режимах (ignore/warning/error) транспилятор ничего не генерирует: warning/error обрабатывает
// семантика, export/calculate там же сообщают «не реализовано».

void ExprEmitter::emitIntrinsic(IntrinsicId id, const CallExpr& call) {
    // Требуемые заголовки - единый источник (реестр интринсиков, types/intrinsics.hpp).
    for (const auto h : intrinsicHeaders(id)) {
        m_driver.m_type.recordRequiredInclude(h);
    }
    switch (id) {
    case IntrinsicId::kTrustAssert:
        m_driver.m_contract.emitRuntimeAssertCheck(call);
        return;
    case IntrinsicId::kCount:
        break;
    }
}
} // namespace trust
