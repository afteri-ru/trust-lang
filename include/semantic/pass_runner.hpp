#pragma once

// include/semantic/pass_runner.hpp
// Оркестратор проходов семантики (pass-менеджер).
// Выполняет обязательное ядро всегда; опциональные проходы — по feature-флагам;
// lowering — последним при отсутствии блокирующих ошибок.

#include "semantic/pass.hpp"
#include "ast/ast_nodes.hpp"

#include <memory>
#include <vector>

namespace trust {

class SemanticPassRunner {
  public:
    explicit SemanticPassRunner(Context& ctx);

    /// Запускает проходы семантики. Мутирует ast_nodes (lowering вставляет узлы).
    /// @return false при блокирующих ошибках обязательного ядра (транспиляцию нельзя запускать).
    bool run(std::vector<AstNodePtr>& ast_nodes);

    /// Доступ к контексту анализа (для тестов: таблица символов).
    const AnalysisContext& analysis() const { return *m_analysis; }

    /// Забирает индекс собранных символов (перемещением) — для LSP/потребителей.
    SymbolIndex takeSymbolIndex();

  private:
    Context& m_ctx;
    std::unique_ptr<AnalysisContext> m_analysis;
};

} // namespace trust
