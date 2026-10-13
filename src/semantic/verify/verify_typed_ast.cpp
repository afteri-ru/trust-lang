#include "types/type_read.hpp"
#include "semantic/verify/verify_typed_ast.hpp"
#include "ast/ident_name.hpp"
#include "ast/binary_op.hpp"
#include "ast/ast_nodes.hpp"
#include "types/type_system.hpp"
#include "types/type_data.hpp"

#include <functional>
#include <string>
#include <vector>

namespace trust {

namespace {

void walkNodes(const AstNodePtr& node, const std::function<void(const AstNodeBase&)>& onNode) {
    if (!node) {
        return;
    }
    onNode(*node);
    for (const AstNodePtr& child : node->children()) {
        walkNodes(child, onNode);
    }
}

std::string kindFieldMessage(const AstNodeBase& node, std::string_view field) {
    return std::string(ParserToken::name(node.kind())) + "::" + std::string(field);
}

} // namespace

std::optional<std::string> verifyTypedAst(const TypeReadModel& types, const std::vector<AstNodePtr>& roots) {
    std::optional<std::string> failure;
    const auto fail = [&](std::string message) {
        if (!failure.has_value()) {
            failure = std::move(message);
        }
    };
    const auto visit = [&](const AstNodeBase& node) {
        if (failure.has_value()) {
            return;
        }

        // 1) VarDecl: кодоген читает inferredType для нетипизированных переменных с
        //    инициализатором. Исключения: `_` (тип выводится по истории записей в DA,
        //    который идёт ПОСЛЕ семантики) и типизированные forward-объявления (`m_type`).
        if (node.is<VarDecl>()) {
            const auto& v = *node.as<VarDecl>();
            const bool deferredToDa = isNoneMarker(v.m_initializer.get());
            if (v.m_type == nullptr && v.m_initializer != nullptr && !deferredToDa && !v.inferredType.isValid()) {
                fail(kindFieldMessage(node, "inferredType") + " is not materialized");
                return;
            }
        }

        // 2) Binary: специальные операторы обязаны иметь материализованную классификацию.
        if (node.is<Binary>()) {
            const auto& b = static_cast<const Binary&>(node);
            if (isTypeCheckOp(b.m_op) && !b.m_typeCheckConst.has_value()) {
                fail(kindFieldMessage(node, "m_typeCheckConst") + " is not classified");
                return;
            }
            if (isOverflowCheckableOp(b.m_op) && !b.m_overflowCheck.has_value()) {
                fail(kindFieldMessage(node, "m_overflowCheck") + " is not classified");
                return;
            }
        }

        // 4) CallExpr: выбранная сигнатура — интернированный функциональный тип; слоты
        //    приведений и типовые аргументы должны быть валидны и согласованы по арности.
        if (node.kind() == ParserToken::Kind::CallExpr) {
            const auto& call = static_cast<const CallExpr&>(node);
            if (call.resolvedSignature != INVALID_TYPE_ID && types.store().getTypeDataAs<FunctionTypeData>(call.resolvedSignature) == nullptr) {
                fail("CallExpr::resolvedSignature is not an interned function type");
                return;
            }
            if (!call.argCasts.empty()) {
                const std::size_t expected = call.boundArgs.has_value() ? call.boundArgs->size() : (call.m_args.has_value() ? call.m_args->size() : 0);
                if (call.argCasts.size() != expected) {
                    fail("CallExpr::argCasts size does not match the bound argument list");
                    return;
                }
            }
            for (const TypeId id : call.resolvedTemplateArgs) {
                if (id == INVALID_TYPE_ID) {
                    fail("CallExpr::resolvedTemplateArgs contains an invalid type");
                    return;
                }
            }
        }
    };

    for (const AstNodePtr& root : roots) {
        walkNodes(root, visit);
        if (failure.has_value()) {
            break;
        }
    }
    return failure;
}

} // namespace trust
