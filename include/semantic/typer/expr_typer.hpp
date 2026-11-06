#pragma once

// Компонент семантики: ExprTyper — фасад над профильными типизаторами выражений
// (Var/Call/MethodCall/Container/Binary/Ref). Каждый коллаборатор получает AnalysisContext
// и драйвер NameResolutionPass через TyperBase; фасад сохраняет публичный API драйвера.

#include "semantic/core/pass.hpp"
#include "semantic/typer/typer_base.hpp"
#include "symbols/symbol_table.hpp"
#include "ast/ast_nodes.hpp"
#include "types/type_id.hpp"
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace trust {

class NameResolutionPass;
class BinaryTyper;
class ContainerTyper;
class VarTyper;
class CallTyper;
class MethodCallTyper;
class RefTyper;

class ExprTyper : public TyperBase {
  public:
    explicit ExprTyper(AnalysisContext& actx, NameResolutionPass& core);
    ~ExprTyper();
    void analyzeDictLiteral(Sequence& dict_node);
    void analyzeArrayInit(DictLiteralNode& node);
    void analyzeTensorInit(DictLiteralNode& node, TypeId target);
    TypeId arrayElementJoin(const std::vector<TypeId>& elementTypes) const;
    void analyzeRangeExpr(RangeExpr& range_node);
    int64_t dictSizeOf(const AstNodeBase* obj) const;
    QualType dictElementType(const AstNodeBase* valueNode) const;
    std::vector<QualType> dictElementTypes(const AstNodeBase* src) const;
    TypeId naturalRuntimeType(TypeId nominal) const;
    TypeId joinElementTypes(const std::vector<TypeId>& naturalized) const;
    TypeId dictFieldTypeOf(const Binary& access) const;
    void typeExpr(AstNodeBase* node);
    /// Базовые листовые обработчики (Filling, литералы) — остаются в фасаде.
    void typeFillingNode(Sequence& f);
    void typeLiteralNode(Literal& lit);
    /// Типизация вызова метода (делегируется MethodCallTyper; вызывается AccessResolver).
    void typeMethodCall(Binary& n);
    /// Коэрция элемента литерала массива `[1,2,3,]` к типу элемента Array-цели (`vector<Int32>` →
    /// `std::vector<int32_t>{1,2,3}`): инициализатор-литерал адаптируется к аннотации, иначе
    /// выведенная узкая разрядность элемента (int8) не сконвертируется в целевую. Только расширение.
    void coerceArrayInitToTarget(DictLiteralNode& node, TypeId targetType);

  private:
    std::unique_ptr<ContainerTyper> m_container;
    std::unique_ptr<VarTyper> m_var;
    std::unique_ptr<CallTyper> m_call;
    std::unique_ptr<MethodCallTyper> m_method_call;
    std::unique_ptr<RefTyper> m_ref;
    std::unique_ptr<BinaryTyper> m_binary;
};

} // namespace trust
