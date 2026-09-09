// src/ast/ref_syntax.cpp
// Единый разбор синтаксиса вида ссылки - см. include/ast/ref_syntax.hpp.
#include "ast/ref_syntax.hpp"

#include "attrs/attr_builtin.hpp"
#include "ast/token.hpp"

namespace trust {

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
    if (kind != ParserToken::Kind::RefMakeExpr && kind != ParserToken::Kind::RefTakeExpr) {
        return std::nullopt;
    }
    return refTypeFromTypeSigil(typeNode->text());
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

} // namespace trust
