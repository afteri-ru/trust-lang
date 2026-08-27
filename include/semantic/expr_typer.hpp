#pragma once

// include/semantic/expr_typer.hpp
// Компонент семантики: ExprTyper. Разделяет AnalysisContext с драйвером NameResolutionPass;
// рекурсия/вызовы других компонентов и драйвера идут через NameResolutionPass (friend).

#include "semantic/pass.hpp"
#include "semantic/symbol_table.hpp"
#include "ast/ast_nodes.hpp"
#include "types/type_id.hpp"
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace trust {

class NameResolutionPass;

class ExprTyper {
  public:
    explicit ExprTyper(AnalysisContext& actx, NameResolutionPass& core)
    : m_actx(actx)
    , m_core(core) {}
    void analyzeDictLiteral(Sequence& dict_node);
    void analyzeArrayInit(DictLiteralNode& node);
    TypeId arrayElementJoin(const std::vector<TypeId>& elementTypes) const;
    void analyzeRangeExpr(RangeExpr& range_node);
    int64_t dictSizeOf(const AstNodeBase* obj) const;
    TypeId dictElementType(const AstNodeBase* valueNode) const;
    std::vector<TypeId> dictElementTypes(const AstNodeBase* src) const;
    TypeId naturalRuntimeType(TypeId nominal) const;
    TypeId joinElementTypes(const std::vector<TypeId>& naturalized) const;
    TypeId dictFieldTypeOf(const Binary& access) const;
    TypeId typeBinaryResult(Binary& b);
    void typeExpr(AstNodeBase* node);
    void checkFormatArgs(CallExpr& call);
    void checkFormatStringArgs(CallExpr& call);
    void widenInferredTarget(const AstNodeBase* lhs, TypeId result);
    void checkAssignmentNarrowing(const AstNodeBase* valueNode, TypeId sourceType, TypeId targetType, std::string_view targetName);

  private:
    AnalysisContext& m_actx;
    NameResolutionPass& m_core;
};

} // namespace trust
