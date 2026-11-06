#pragma once

// Анализатор деструктуризации: разбор целей (dict/tuple/rest), их типы и регистрация.
// Выделен из DeclAnalyzer.

#include "semantic/core/pass.hpp"

#include <cstddef>

namespace trust {

class NameResolutionPass;

class DestructureAnalyzer {
  public:
    DestructureAnalyzer(AnalysisContext& actx, NameResolutionPass& core)
    : m_actx(actx)
    , m_core(core) {
    }

    void analyzeDestructure(DestructureDecl& node);

  private:
    AnalysisContext& m_actx;
    NameResolutionPass& m_core;

    bool collectDestructureSlots(const DestructureDecl& node, size_t& elementSlots, bool& hasRest);
    void analyzeDestructureTuple(DestructureDecl& node, TypeId tupleType);
    bool restTargetNameAllowed(IdentName& t, bool isSpreadDict, const AstNodeBase* source);
    void declareDestructureTarget(IdentName& t, bool isRest, TypeId type);
    TypeId explicitTargetType(const DestructureDecl::Target& target, TypeId fallback);
    void assignDestructureTarget(IdentName& t, bool isRest);
};

} // namespace trust
