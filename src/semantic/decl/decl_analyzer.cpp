// src/semantic/decl/decl_analyzer.cpp
// Фасад анализа объявлений: делегирование профильным анализаторам.
#include "semantic/decl/decl_analyzer.hpp"
#include "semantic/decl/var_decl_analyzer.hpp"
#include "semantic/decl/func_decl_analyzer.hpp"
#include "semantic/decl/destructure_analyzer.hpp"
#include "semantic/core/name_resolution.hpp"

namespace trust {

DeclAnalyzer::DeclAnalyzer(AnalysisContext& actx, NameResolutionPass& core)
: m_actx(actx)
, m_core(core)
, m_var(std::make_unique<VarDeclAnalyzer>(actx, core))
, m_func(std::make_unique<FuncDeclAnalyzer>(actx, core))
, m_destructure(std::make_unique<DestructureAnalyzer>(actx, core)) {
}

DeclAnalyzer::~DeclAnalyzer() = default;

void DeclAnalyzer::analyzeVarDecl(VarDecl& var_node) {
    m_var->analyzeVarDecl(var_node);
}
void DeclAnalyzer::analyzeFuncDecl(FuncDecl& func_node) {
    m_func->analyzeFuncDecl(func_node);
}
void DeclAnalyzer::validateFuncDefaults(const FuncDecl& func_node) {
    m_func->validateFuncDefaults(func_node);
}
void DeclAnalyzer::declareFuncParams(FuncDecl& func_node) {
    m_func->declareFuncParams(func_node);
}
void DeclAnalyzer::analyzeLambdaCaptures(FuncDecl& func_node) {
    m_func->analyzeLambdaCaptures(func_node);
}
void DeclAnalyzer::analyzeDestructure(DestructureDecl& node) {
    m_destructure->analyzeDestructure(node);
}

} // namespace trust
