// src/ast/ast_children.cpp
// Единый источник истины «kind → дети»: AstNodeBase::collectChildren и const-обёртка
// children(), а также базовые HasText-конструктор и applyReadonlyFromCaret.
// Выделено из ast_nodes.cpp (модуль ast_children).
#include "ast/ast_nodes.hpp"
#include "ast/attr_pool.hpp"
#include "ast/term_to_ast.hpp"
#include "ast/token_type.hpp"
#include "syntax/term.h"
#include "utils/error.hpp"
#include <vector>
namespace trust {

// -- AstNodeBase::collectChildren - единый источник истины «kind → дети». --
//    Заполняет out указателями на дочерние слоты (позволяет заменять узлы при обходе).
//    Листья - без слотов. Используется семантикой (обход с подменой узлов) и const-
//    обёрткой children() (см. ниже). Не мутирует дерево - только читает слоты.

void AstNodeBase::collectChildren(std::vector<AstNodePtr*>& out) {
    switch (m_kind) {
    // -- Sequence / блоки / модуль: m_body --
    case ParserToken::Kind::sequence:
    case ParserToken::Kind::Attr:
    case ParserToken::Kind::ScopeBlock:
    case ParserToken::Kind::ModuleDecl:
    case ParserToken::Kind::ArrayInit:
    case ParserToken::Kind::DictLiteral:
    case ParserToken::Kind::Tuple:
    case ParserToken::Kind::RangeExpr:
    case ParserToken::Kind::RefMakeExpr:
    case ParserToken::Kind::RefTakeExpr:
    case ParserToken::Kind::Ellipsis:
    case ParserToken::Kind::TryCatchStmt:
    case ParserToken::Kind::CatchBlock:
    case ParserToken::Kind::EnumDecl:
    case ParserToken::Kind::EnumMember:
    case ParserToken::Kind::StructDecl:
    case ParserToken::Kind::StructField: {
        auto& s = static_cast<Sequence&>(*this);
        for (auto& c : s.m_body) {
            out.push_back(&c);
        }
        break;
    }
    // -- Binary: m_left/m_right. Для объявлений (TypeDecl/NameDecl) m_left - имя
    //    объявляемого, его НЕ обходим (не резолвим как переменную) - только m_right. --
    case ParserToken::Kind::AssignOp:
    case ParserToken::Kind::MathOp:
    case ParserToken::Kind::BitwiseOp:
    case ParserToken::Kind::CompareOp:
    case ParserToken::Kind::LogicalOp:
    case ParserToken::Kind::MemberAccess:
    case ParserToken::Kind::ArrayAccess:
    case ParserToken::Kind::AppendStmt: {
        auto& b = static_cast<Binary&>(*this);
        if (b.m_left) {
            out.push_back(&b.m_left);
        }
        if (b.m_right) {
            out.push_back(&b.m_right);
        }
        break;
    }
    case ParserToken::Kind::TypeDecl:
    case ParserToken::Kind::NameDecl: {
        auto& b = static_cast<Binary&>(*this);
        if (b.m_right) {
            out.push_back(&b.m_right);
        }
        break;
    }
    // -- DestructureDecl: обходим только источник; цели слева - объявления, их не резолвим --
    case ParserToken::Kind::DestructureDecl: {
        auto& d = static_cast<DestructureDecl&>(*this);
        if (d.m_source) {
            out.push_back(&d.m_source);
        }
        break;
    }
    // -- CallExpr: m_callee + m_args --
    case ParserToken::Kind::CallExpr: {
        auto& c = static_cast<CallExpr&>(*this);
        if (c.m_callee) {
            out.push_back(&c.m_callee);
        }
        if (c.m_args) {
            for (auto& a : *c.m_args) {
                out.push_back(&a);
            }
        }
        break;
    }
    // -- IdentType: dims [...] + params (...) --
    case ParserToken::Kind::TypeName: {
        auto& t = static_cast<IdentType&>(*this);
        if (t.dims()) {
            auto& dims = const_cast<std::vector<AstNodePtr>&>(*t.dims());
            for (auto& d : dims) {
                out.push_back(&d);
            }
        }
        if (t.params()) {
            auto& params = const_cast<std::vector<AstNodePtr>&>(*t.params());
            for (auto& p : params) {
                out.push_back(&p);
            }
        }
        break;
    }
    // -- JumpStmt (return/throw/break/continue): m_value - выражение. m_label - метка
    //    (не переменная), её НЕ обходим и не резолвим. --
    case ParserToken::Kind::ReturnStmt:
    case ParserToken::Kind::ThrowStmt:
    case ParserToken::Kind::BreakStmt:
    case ParserToken::Kind::ContinueStmt: {
        auto& j = static_cast<JumpStmt&>(*this);
        if (j.m_value) {
            out.push_back(&j.m_value);
        }
        break;
    }
    // -- VarDecl: m_type + m_initializer --
    case ParserToken::Kind::VarDecl: {
        auto& v = static_cast<VarDecl&>(*this);
        if (v.m_type) {
            out.push_back(&v.m_type);
        }
        if (v.m_initializer) {
            out.push_back(&v.m_initializer);
        }
        break;
    }
    // -- FuncDecl: m_type + m_params + m_body --
    case ParserToken::Kind::FuncDecl: {
        auto& f = static_cast<FuncDecl&>(*this);
        if (f.m_type) {
            out.push_back(&f.m_type);
        }
        if (f.m_params) {
            for (auto& p : *f.m_params) {
                out.push_back(&p);
            }
        }
        if (f.m_body) {
            for (auto& b : *f.m_body) {
                out.push_back(&b);
            }
        }
        break;
    }
    // -- ArgNode: m_type + m_value --
    case ParserToken::Kind::ArgNode: {
        auto& p = static_cast<ArgNode&>(*this);
        if (p.m_type) {
            out.push_back(&p.m_type);
        }
        if (p.m_value) {
            out.push_back(&p.m_value);
        }
        break;
    }
    // -- ControlFlowStmt (if/while/do-while): cond + body + else (+ elseifs у if) --
    case ParserToken::Kind::IfStmt:
    case ParserToken::Kind::WhileStmt:
    case ParserToken::Kind::DoWhileStmt: {
        auto& cf = static_cast<ControlFlowStmt&>(*this);
        if (cf.m_cond) {
            out.push_back(&cf.m_cond);
        }
        if (cf.m_body) {
            out.push_back(&cf.m_body);
        }
        if (cf.m_else) {
            out.push_back(&cf.m_else);
        }
        if (m_kind == ParserToken::Kind::IfStmt) {
            auto& i = static_cast<IfStmt&>(*this);
            for (auto& [cond, body] : i.m_elseifs) {
                if (cond) {
                    out.push_back(&cond);
                }
                if (body) {
                    out.push_back(&body);
                }
            }
        }
        break;
    }
    // -- SemicolonStmt: m_expr --
    case ParserToken::Kind::SemicolonStmt: {
        auto& e = static_cast<SemicolonStmt&>(*this);
        if (e.m_expr) {
            out.push_back(&e.m_expr);
        }
        break;
    }
    // -- MatchStmt: m_value + patterns/body веток + m_default --
    case ParserToken::Kind::MatchingStmt: {
        auto& m = static_cast<MatchStmt&>(*this);
        if (m.m_value) {
            out.push_back(&m.m_value);
        }
        for (auto& c : m.m_cases) {
            for (auto& p : c.patterns) {
                if (p) {
                    out.push_back(&p);
                }
            }
            if (c.body) {
                out.push_back(&c.body);
            }
        }
        if (m.m_default) {
            out.push_back(&m.m_default);
        }
        break;
    }
    default:
        break; // листья: Ident/Literal/LabelRef/ContextMacro/AstNodeAttr-листья
    }
}

// -- AstNodeBase::children - const-обёртка над collectChildren (копии узлов). --
//    Единый источник истины «kind → дети» - collectChildren; этот метод только
//    копирует слоты в возвращаемый вектор (для const-потребителей, напр. pipeline).

std::vector<AstNodePtr> AstNodeBase::children() const {
    std::vector<AstNodePtr*> slots;
    const_cast<AstNodeBase*>(this)->collectChildren(slots);
    std::vector<AstNodePtr> out;
    out.reserve(slots.size());
    for (auto* p : slots) {
        if (p) {
            out.push_back(*p);
        }
    }
    return out;
}

// -- Единый хелпер иммутабельности ('^' → attr::ReadOnly) - объявлен в token_base.hpp.
//    Используется конвертером Term→AST и IdentName (manual-конструктор).
bool applyReadonlyFromCaret(AstNodeAttr& node, std::string_view raw, AttrPool* pool) {
    if (raw.empty() || raw.back() != '^') {
        return false;
    }
    if (!canHaveImmutableQualifier(node.kind())) {
        return false; // '^' неприменим к этому kind - диагностика выполняется отдельно (в convert)
    }
    EXPECT(pool && "applyReadonlyFromCaret requires AttrPool");
    auto readonly_id = pool->lookup(attr::ReadOnly);
    EXPECT(readonly_id.has_value() && "Predefined attr::ReadOnly not registered!");
    node.add_attr(readonly_id.value());
    return true;
}

// -- HasText: терм-конструктор - текст читается из Term и нормализуется по kind.
//    m_term хранится для range(); m_text - нормализованный текст (text() читает m_text).
HasText::HasText(ParserToken::Kind k, TermPtr term)
: AstNodeAttr(k, std::move(term)) {
    EXPECT(m_term && "HasText term-constructor requires a source Term");
    m_text = normalizeTermText(k, m_term->getText());
}
} // namespace trust
