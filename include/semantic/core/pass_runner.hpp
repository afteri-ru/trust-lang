#pragma once

// Оркестратор проходов семантики (pass-менеджер).
// Выполняет обязательное ядро всегда; опциональные проходы - по feature-флагам.
// Граница фаз: runner выполняет ТОЛЬКО семантику над неизменяемой AST. Производные
// представления и их потребители (HIR, flow-анализы DA/borrow) принадлежат `Pipeline`
// (semantic → buildHir → verify(HIR) → flow → codegen).

#include "semantic/core/pass.hpp"
#include "ast/ast_nodes.hpp"

#include <memory>
#include <string_view>
#include <vector>

namespace trust {

class TypeSystem;

class DiagnosticEngine;

/// Политика внутренней ошибки анализатора (ICE).
/// recover=true (LSP: allow_semantic_on_errors) - Severity::Error и продолжение с частичным AST;
/// recover=false (CLI) - Severity::Fatal + backtrace (report(Fatal) бросает FatalError,
/// который ловится в composition root и даёт код возврата 1).
void reportInternalError(DiagnosticEngine& diag, bool recover, std::string_view what);

class SemanticPassRunner {
  public:
    /// recoverOnInternalError - см. reportInternalError (передаётся из PipelineOpts::allow_semantic_on_errors).
    /// `registry` — WRITER-ссылка реестра типов (владелец — `Pipeline`); единственный канал записи (E1).
    SemanticPassRunner(Context& ctx, TypeSystem& registry, bool recoverOnInternalError = false);

    /// Запускает проходы семантики над неизменяемой AST. HIR и flow-анализы здесь НЕ
    /// выполняются: их запускает `Pipeline` после семантики (см. контракт фаз в заголовке).
    /// @return false при блокирующих ошибках обязательного ядра (транспиляцию нельзя запускать).
    bool run(std::vector<AstNodePtr>& ast_nodes);

    /// true — прогон прерван внутренней ошибкой (ICE). Представление после ICE недостоверно:
    /// HIR по такому AST не строится.
    [[nodiscard]] bool crashed() const noexcept { return m_crashed; }

    /// Доступ к контексту анализа (для тестов: таблица символов).
    const AnalysisContext& analysis() const { return *m_analysis; }

    /// Индекс объявлений текущего прогона. Единственная точка досборки после run(): Pipeline
    /// добавляет макросы из Context до `takeAnalysis()` (модель после этого неизменяема).
    [[nodiscard]] SymbolIndex& symbolIndex() { return m_analysis->state().symbolIndex(); }

    /// Передаёт владение контекстом анализа в `SemanticModel` (persistent-таблица живёт в
    /// результате прогона). Перемещать `AnalysisContext` как значение нельзя: его зоны держат
    /// ссылки на `SymbolTable`.
    [[nodiscard]] std::unique_ptr<AnalysisContext> takeAnalysis();

    /// Имена подключённых хуков в порядке колбэков (состояние после последнего run):
    /// контракт порядка для тестов и трассировки.
    [[nodiscard]] const std::vector<std::string_view>& activeHooks() const noexcept { return m_activeHooks; }

  private:
    Context& m_ctx;
    TypeSystem& m_registry;
    bool m_recoverOnInternalError = false;
    std::unique_ptr<AnalysisContext> m_analysis;
    bool m_crashed = false;
    std::vector<std::string_view> m_activeHooks;
};

} // namespace trust
