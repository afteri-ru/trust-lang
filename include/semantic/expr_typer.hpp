#pragma once

// include/semantic/expr_typer.hpp
// Компонент семантики: ExprTyper. Разделяет AnalysisContext с драйвером NameResolutionPass;
// рекурсия/вызовы других компонентов и драйвера идут через NameResolutionPass (friend).

#include "semantic/pass.hpp"
#include "analysis/symbol_table.hpp"
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
    /// Оператор сравнения типов (<~ / ~~ / ~~~): валидация операндов, разрешение типа-цели и
    /// статическая свёртка результата (m_typeCheckConst). Реализация - assign_typer.cpp.
    TypeId typeCheckBinaryResult(Binary& b, TypeId lt, TypeId rt);
    /// Типизация ссылочных выражений (RefMakeExpr/RefTakeExpr) -
    /// реализована в ref_expr_typer.cpp (отдельная зона ответственности).
    void typeRefExpr(AstNodeBase* node);
    void typeExpr(AstNodeBase* node);
    /// Обработчики семейств узлов-выражений (диспетчеризация из typeExpr; реализация -
    /// expr_node_typer.cpp, отдельная зона ответственности).
    void typeFillingNode(const Sequence& f);
    void typeBinaryNode(Binary& b);
    void typeAppendStmt(Binary& b);
    void typeLambdaNode(FuncDecl& f);
    void typeVarDeclNode(VarDecl& v);
    void typeInferredVarDecl(VarDecl& v);
    void typeExplicitVarDecl(VarDecl& v);
    void typeForwardVarDecl(VarDecl& v);
    void checkWithBindingCapture(const VarDecl& v);
    void typeLiteralNode(Literal& lit);
    void typeCallNode(CallExpr& call);
    void checkFormatArgs(CallExpr& call);
    void checkFormatStringArgs(CallExpr& call);
    /// Многоточие в аргументах вызова (`... expr ...` / `...`): структурные правила + РАЗВОРОТ
    /// (материализация) списка аргументов - единый примитив semantic/ellipsis.
    void analyzeCallFilling(CallExpr& call);
    void widenInferredTarget(const AstNodeBase* lhs, TypeId result);
    /// Предупреждение о повторном захвате (`*ref`) ОДНОГО объекта в одном выражении (риск
    /// самоблокировки для синхронизированных ссылок: shared_timed_mutex не рекурсивен).
    void checkDoubleCapture(const Binary& b);
    void checkAssignmentNarrowing(const AstNodeBase* valueNode, TypeId sourceType, TypeId targetType, std::string_view targetName);
    /// Коэрция элемента литерала массива `[1,2,3,]` к типу элемента Array-цели (`vector<Int32>` →
    /// `std::vector<int32_t>{1,2,3}`): инициализатор-литерал адаптируется к аннотации, иначе
    /// выведенная узкая разрядность элемента (int8) не сконвертируется в целевую. Только расширение.
    void coerceArrayInitToTarget(DictLiteralNode& node, TypeId targetType);

  private:
    AnalysisContext& m_actx;
    NameResolutionPass& m_core;
};

} // namespace trust
