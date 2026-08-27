#pragma once

// include/semantic/decl_analyzer.hpp
// Компонент семантики: DeclAnalyzer. Разделяет AnalysisContext с драйвером NameResolutionPass;
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

class DeclAnalyzer {
  public:
    explicit DeclAnalyzer(AnalysisContext& actx, NameResolutionPass& core)
    : m_actx(actx)
    , m_core(core) {}
    void analyzeVarDecl(VarDecl& var_node);
    void analyzeTypeDecl(Binary& binary_node);
    void analyzeEnumDecl(Binary& binary_node);
    void analyzeVariantDecl(Binary& binary_node);
    void analyzeFuncDecl(FuncDecl& func_node);
    void declareFuncParams(FuncDecl& func_node);
    bool collectDestructureSlots(const DestructureDecl& node, size_t& elementSlots, bool& hasRest);
    void analyzeDestructure(DestructureDecl& node);
    void analyzeDestructureTuple(DestructureDecl& node, TypeId tupleType);
    std::string normalizeLocalSigil(HasText& node, MapperRange range, bool isLocal);
    std::string canonicalTargetName(const HasText& t) const;
    bool restTargetNameAllowed(HasText& t, bool isSpreadDict, const AstNodeBase* source);
    void declareDestructureTarget(HasText& t, bool isRest, TypeId type);
    TypeId explicitTargetType(const DestructureDecl& node, size_t i, TypeId fallback);
    void assignDestructureTarget(DestructureDecl& node, size_t i, HasText& t, bool isRest);

  private:
    AnalysisContext& m_actx;
    NameResolutionPass& m_core;
};

} // namespace trust
