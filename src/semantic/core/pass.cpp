// Агрегат прогона семантики: владение зонами ответственности и доступ к ним.
// Вычислительной логики нет - каждая зона живёт в своём TU.
#include "semantic/core/pass.hpp"

#include "diag/diag.hpp"

namespace trust {

AnalysisContext::AnalysisContext(Context& ctx, TypeSystem& registry)
: m_ctx(ctx)
, m_registry(registry)
, m_namespaces(ctx, m_symbols)
, m_types(ctx, m_registry, m_symbols, m_namespaces)
, m_facts(ctx, m_types) {
    // SymbolTable использует FunctionIdentity для C++-идентичности шаблонных сигнатур.
    m_symbols.setTypes(m_registry);
}

bool AnalysisContext::hasErrors() const {
    return m_ctx.diag().errorCount() > 0;
}

} // namespace trust
