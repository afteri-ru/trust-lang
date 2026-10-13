#pragma once

// include/semantic/typer/container_typer.hpp
// Типизатор контейнерных литералов: Dict/Array/Tensor/Range, вывод элементного типа,
// формы/размерности и доступ к элементам. Выделен из ExprTyper как отдельная зона
// ответственности.

#include "semantic/typer/typer_base.hpp"
#include <cstdint>
#include <vector>

namespace trust {

class ContainerTyper : public TyperBase {
  public:
    ContainerTyper(AnalysisContext& actx, NameResolutionPass& core)
    : TyperBase(actx, core) {}

    void analyzeDictLiteral(Sequence& dict_node);
    void analyzeArrayInit(DictLiteralNode& node);
    void analyzeTensorInit(DictLiteralNode& node, TypeId target);
    TypeId arrayElementJoin(const std::vector<TypeId>& elementTypes) const;
    void analyzeRangeExpr(RangeExpr& range_node);
    void coerceArrayInitToTarget(DictLiteralNode& node, TypeId targetType);
    int64_t dictSizeOf(const AstNodeBase* obj) const;
    QualType dictElementType(const AstNodeBase* valueNode) const;
    std::vector<QualType> dictElementTypes(const AstNodeBase* src) const;
    TypeId naturalRuntimeType(TypeId nominal) const;
    TypeId joinElementTypes(const std::vector<TypeId>& naturalized) const;
    TypeId dictFieldTypeOf(const Binary& access) const;
};

} // namespace trust
