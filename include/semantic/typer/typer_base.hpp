#pragma once

// Общая база типизаторов выражений: доступ к AnalysisContext и сервисам драйвера
// NameResolutionPass (дружба объявлена только для базы; наследники используют
// protected-обёртки), плюс общие проверки присваивания, разделяемые семействами типизаторов.

#include "symbols/symbol_table.hpp"
#include "ast/ast_nodes.hpp"
#include "types/type_id.hpp"
#include <string_view>

namespace trust {

class AnalysisContext;
class NameResolutionPass;

class TyperBase {
  protected:
    TyperBase(AnalysisContext& actx, NameResolutionPass& core);
    ~TyperBase() = default;

    void widenInferredTarget(const AstNodeBase* lhs, TypeId result);
    void checkAssignmentNarrowing(const AstNodeBase* valueNode, TypeId sourceType, TypeId targetType, std::string_view targetName);

    AnalysisContext& m_actx;
    NameResolutionPass& m_core;
};

} // namespace trust
