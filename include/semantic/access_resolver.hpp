#pragma once

// include/semantic/access_resolver.hpp
// Компонент семантики: AccessResolver. Разделяет AnalysisContext с драйвером NameResolutionPass;
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

class AccessResolver {
  public:
    explicit AccessResolver(AnalysisContext& actx, NameResolutionPass& core)
    : m_actx(actx)
    , m_core(core) {}
    void analyzeAccess(Binary& n);
    void resolveTupleAccess(Binary& n, TypeId tupleType);
    void resolveArrayAccess(Binary& n, TypeId arrayType);
    void handleMethodCall(Binary& n);

  private:
    AnalysisContext& m_actx;
    NameResolutionPass& m_core;
};

} // namespace trust
