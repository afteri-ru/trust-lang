#pragma once

// Компонент семантики: AccessResolver. Разделяет AnalysisContext с драйвером NameResolutionPass;
// рекурсия/вызовы других компонентов и драйвера идут через NameResolutionPass (friend).

#include "semantic/core/pass.hpp"
#include "symbols/symbol_table.hpp"
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
    void resolveTensorAccess(Binary& n, TypeId tensorType);
    /// Тензорные методы `sum()/matmul(t)/reshape(dims...)` (не объявлены в реестре: сигнатура
    /// зависит от самого тензора). true - вызов обработан (успех или диагностика).
    bool resolveTensorMethodCall(Binary& n, TypeId tensorType);
    /// Индексация пользовательского типа через объявленный оператор `[]`: `a[i]` → C++ `(obj)[idx]`.
    void resolveSubscriptOperatorAccess(Binary& n, TypeId objType);
    void handleMethodCall(Binary& n);

  private:
    bool resolveObjectAccess(Binary& n, TypeId leftT);
    void resolveDictAccess(Binary& n);

  private:
    AnalysisContext& m_actx;
    NameResolutionPass& m_core;
};

} // namespace trust
