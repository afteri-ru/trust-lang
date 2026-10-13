// src/ast/nodes/ast_expr.cpp
// Выражения AST: Literal, ContextMacro, Binary, CallExpr, RangeExpr. Единый разбор синтаксиса
// вида ссылки (ast/ref_syntax.hpp) - в этом же TU (общая зона "выражения/виды ссылок").
#include "ast/ast_nodes.hpp"
#include "ast/nodes/ast_helpers.hpp"
#include "ast/ref_syntax.hpp"
#include "ast/term_to_ast.hpp"
#include "ast/token_type.hpp"
#include "attrs/attr_builtin.hpp"
#include "syntax/term/term.h"
#include "utils/error.hpp"
#include <string>
namespace trust {

Binary::Binary(ParserToken::Kind k, TermPtr term, Context* ctx)
: AstNodeAttr(k, term, ctx) {
    EXPECT(term && "Binary term-constructor requires a source Term");
    m_isTypeDefine = (term->getTermID() == TermID::TYPE_DEFINE);
    m_isTypeAlias = (term->getTermID() == TermID::ALIAS_TYPE);
    // Единая точка заполнения enum-opcode конкретного оператора (Term->AST).
    if (k == ParserToken::Kind::IncDecOp) {
        // `++`/`--`: text() одинаков для префикса и постфикса, позицию кодирует СЛОТ операнда -
        // префикс кладёт операнд в m_right (как унарный минус), постфикс - в m_left.
        const bool postfix = (term->m_left != nullptr);
        const bool inc = (term->getTermID() == TermID::OP_INC);
        m_op = inc ? (postfix ? BinaryOp::PostIncrement : BinaryOp::PreIncrement) : (postfix ? BinaryOp::PostDecrement : BinaryOp::PreDecrement);
    } else {
        m_op = parseBinaryOp(term->getText());
        EXPECT(m_op != BinaryOp::None && "unknown binary operator glyph (no BinaryOp opcode)");
    }
    if (ctx) {
        if (term->m_left) {
            m_left = convertChild(*ctx, term->m_left);
        }
        if (term->m_right) {
            m_right = convertChild(*ctx, term->m_right);
        }
    }
    if (m_left && m_right) {
        const MapperRange l = m_left->range();
        const MapperRange r = m_right->range();
        if (!l.isInvalid() && !r.isInvalid() && l.begin.fileIdx() == r.end.fileIdx() && l.begin.offset() <= r.end.offset()) {
            setRange(MapperRange{l.begin, r.end});
        }
    }
}

CallExpr::CallExpr(ParserToken::Kind k, TermPtr term, Context* ctx)
: AstNodeAttr(k, term) {
    EXPECT(term && "CallExpr term-constructor requires a source Term");
    if (ctx) {
        m_callee = std::make_shared<IdentName>(term);
        std::vector<AstNodePtr> args;
        convertChildren(*ctx, term, args);
        if (!args.empty()) {
            m_args = std::move(args);
        }
    }
}

// -- Literal::dump --

std::string Literal::dump(size_t indent) const {
    std::string result = std::string(indent, ' ');
    result += ParserToken::name(kind());
    if (kind() == ParserToken::Kind::StrChar || kind() == ParserToken::Kind::StrWide) {
        result += " \"";
        result += text();
        result += "\"";
    } else {
        // Numeric and other non-string literals are printed without quotes
        // (matching the convention used by clang -ast-dump).
        result += " ";
        result += text();
    }
    return result;
}

// -- ContextMacro::dump --

std::string ContextMacro::dump(size_t indent) const {
    std::string result = std::string(indent, ' ');
    result += ParserToken::name(kind());
    if (!text().empty()) {
        result += " '";
        result += text();
        result += "'";
    }
    return result;
}

// -- CheckAreaStmt::dump --

std::string CheckAreaStmt::dump(size_t indent) const {
    std::string result = std::string(indent, ' ');
    result += ParserToken::name(kind());
    if (area.has_value()) {
        result += " area=";
        result.append(areaKindName(*area));
    }
    result += " behavior=";
    if (behavior.has_value()) {
        result.append(severityName(*behavior));
    } else {
        result += "default";
    }
    return result;
}

// -- DebugStmt::dump --

std::string DebugStmt::dump(size_t indent) const {
    std::string result = std::string(indent, ' ');
    result += ParserToken::name(kind());
    result += (mode == DebugMode::Scope) ? " scope" : " filter";
    for (const auto& a : args) {
        result += " '";
        result += a;
        result += "'";
    }
    return result;
}

// -- Binary::dump --

std::string Binary::dump(size_t indent) const {
    std::string result = AstNodeAttr::dump(indent);
    dumpLabeled(result, indent, "left", m_left);
    dumpLabeled(result, indent, "right", m_right);
    return result;
}

// -- CallExpr::dump --

std::string CallExpr::dump(size_t indent) const {
    std::string result = AstNodeAttr::dump(indent);
    dumpLabeled(result, indent, "callee", m_callee);
    if (m_args) {
        result += "\n";
        result += std::string(indent, ' ');
        result += "args:";
        for (size_t i = 0; i < m_args->size(); i++) {
            const auto& arg = (*m_args)[i];
            result += "\n";
            if (arg) {
                result += arg->dump(indent + 2);
            } else {
                result += std::string(indent + 2, ' ') + "(null)";
            }
        }
    }
    return result;
}

// -- RangeExpr::dump --

std::string RangeExpr::dump(size_t indent) const {
    std::string result = AstNodeAttr::dump(indent);
    if (elementType.getTypeId() != INVALID_TYPE_ID) {
        result += "elem=";
        result += std::to_string(elementType.raw());
        result += ' ';
    }
    dumpBody(result, m_body, indent, indent + 2);
    return result;
}

std::optional<RefType> refKindFromAttrArgs(const std::vector<std::string>* args) {
    if (args == nullptr || args->empty()) {
        return std::nullopt;
    }
    const auto kind = refTypeFromString(args->front());
    return kind;
}

std::optional<RefType> refKindOfAttr(const AttrPool& attrs, const AstNodeAttr& node) {
    const auto rid = attrs.lookup(attr::Reftype);
    if (!rid.has_value() || !node.has_attr(*rid)) {
        return std::nullopt;
    }
    return refKindFromAttrArgs(node.attr_args(*rid));
}

std::optional<RefType> refKindOfTypeNode(const AstNodeBase* typeNode) {
    if (typeNode == nullptr) {
        return std::nullopt;
    }
    const auto kind = typeNode->kind();
    if (kind == ParserToken::Kind::RefMakeExpr) {
        const auto* make = typeNode->as<RefMakeExpr>();
        EXPECT(make != nullptr && "RefMakeExpr kind must map to RefMakeExpr class");
        if (make == nullptr || make->m_sigilKind == RefType::kValue) {
            return std::nullopt; // нативный `&` либо вид не задан
        }
        return make->m_sigilKind;
    }
    // RefTakeExpr (`*`) не несёт вида smart-ссылки: текст-маркер у операторов не хранится.
    return std::nullopt;
}

std::optional<RefType> refKindOfTypeSpec(const AstNodeBase* typeNode, const AttrPool& attrs) {
    if (const auto sigil = refKindOfTypeNode(typeNode)) {
        return sigil;
    }
    if (typeNode == nullptr) {
        return std::nullopt;
    }
    if (const AstNodeAttr* attrNode = typeNode->as_attr()) {
        return refKindOfAttr(attrs, *attrNode);
    }
    return std::nullopt;
}

bool hasAutoRefKindAttr(const AstNodeAttr& node, const AttrPool& pool) {
    const auto rid = pool.lookup(attr::Reftype);
    if (!rid.has_value()) {
        return false;
    }
    const AttrId idx = static_cast<AttrId>(*rid & detail::kAttrIndexMask);
    for (const AttrId id : node.attrs()) {
        if (static_cast<AttrId>(id & detail::kAttrIndexMask) == idx && !detail::is_manual(id)) {
            return true;
        }
    }
    return false;
}

} // namespace trust
