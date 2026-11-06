#include "semantic/core/pass_runner.hpp"

#include "semantic/typer/arg_casts.hpp"
#include "semantic/core/name_resolution.hpp"
#include "semantic/core/macro_expander.hpp"
#include "semantic/verify/verify_typed_ast.hpp"
#include "semantic/verify/lint.hpp"
#include "semantic/ref/stack_check_infer.hpp"
#include "semantic/core/symbol_collector.hpp"
#include "semantic/ref/nativeref.hpp"
#include "semantic/ref/ref_cycle.hpp"
#include "semantic/core/capture_last_result.hpp"
#include "ast/ast_nodes.hpp"
#include "diag/diag.hpp"
#include "types/type_system.hpp"
#include "utils/utils.hpp"
#include <exception>
#include <utility>
#include <vector>

namespace trust {

void reportInternalError(DiagnosticEngine& diag, bool recover, std::string_view what) {
    if (!recover) {
        // Fatal печатает диагностику и бросает FatalError (composition root -> код 1).
        diag.report(Severity::Fatal, MapperRange{}, "internal compiler error during semantic analysis: {}\n{}", what, utils::backtrace_string());
    }
    diag.report(Severity::Error, MapperRange{}, "internal error during semantic analysis: {}", what);
}

SemanticPassRunner::SemanticPassRunner(Context& ctx, TypeSystem& registry, bool recoverOnInternalError)
: m_ctx(ctx)
, m_registry(registry)
, m_recoverOnInternalError(recoverOnInternalError)
, m_analysis(std::make_unique<AnalysisContext>(ctx, registry)) {
}

bool SemanticPassRunner::run(std::vector<AstNodePtr>& ast_nodes) {
    // Свежий контекст на каждый запуск (чистая таблица символов и скоуп-стек).
    m_analysis = std::make_unique<AnalysisContext>(m_ctx, m_registry);
    m_crashed = false;

    // Упорядоченный жизненный цикл типов: реестр на каждый запуск сбрасывается к
    // builtin-состоянию, чтобы пользовательские алиасы и функциональные сигнатуры
    // не накапливались между run() (SymbolTable пер-ран; типы должны быть согласованы).
    m_registry.reset();

    // Реестр хуков: порядок в списке = порядок колбэков ядра. Условие подключения объявляет
    // сам хук (`isEnabled`) - раннер условий реализации не содержит.
    NameResolutionPass core(*m_analysis);
    m_activeHooks.clear();
    std::vector<std::unique_ptr<InlineAnalysisHook>> hooks;
    hooks.reserve(6);
    // ContextMacroExpander ПЕРВЫЙ: раскрывает ContextMacro/квалификатор @:: до обработки ядра
    // (обязательная часть семантики, не опциональный анализатор).
    hooks.push_back(std::make_unique<ContextMacroExpander>(*m_analysis));
    hooks.push_back(std::make_unique<LintHook>(*m_analysis));
    hooks.push_back(std::make_unique<StackCheckInferHook>(*m_analysis));
    hooks.push_back(std::make_unique<SymbolCollectorHook>(*m_analysis));
    // Нативные (сырые) C++-ссылки: D3–D7 (инвалидация @[borrowed]/weak — flow-анализ BorrowFlowPass).
    hooks.push_back(std::make_unique<NativeRefHook>(*m_analysis));
    // Гарантия модели памяти (REFType.md §11.11): включён всегда, настраиваются только severity.
    hooks.push_back(std::make_unique<RefCycleHook>(*m_analysis));
    for (auto& hook : hooks) {
        if (!hook->isEnabled()) {
            continue;
        }
        m_activeHooks.push_back(hook->name());
        core.addHook(std::move(hook));
    }

    // captureLastResult: структурное переписывание пары [оператор-выражение E / декларация x:=E;
    // sink с $^] -> обычный код. Неподдержанные обращения `$^` проход сообщает САМ (в момент
    // выявления, здесь известен предыдущий сиблинг) и заменяет лист `$^` на ErrorExpr-заглушку.
    {
        CaptureLastResultContext capture_ctx;
        capture_ctx.ctx = &m_ctx; // AttrPool/diagnostics for the pre-semantic rewrite
        captureLastResult(ast_nodes, capture_ctx);
    }

    // Дожимаем finalize() даже если ядро бросило исключение на повреждённом AST
    // (allow_semantic_on_errors): собранные к этому моменту символы не теряются.
    // Политика ICE: CLI - Fatal + backtrace (report бросает FatalError), LSP - Error и
    // продолжение, чтобы отдать накопленные символы/диагностики.
    bool crashed = false;
    try {
        core.run(ast_nodes);
    } catch (const std::exception& e) {
        reportInternalError(m_ctx.diag(), m_recoverOnInternalError, e.what());
        crashed = true;
    } catch (...) {
        reportInternalError(m_ctx.diag(), m_recoverOnInternalError, "unknown exception");
        crashed = true;
    }
    if (!crashed) {
        // Решения о неявных конверсиях фиксируются на узле ДО lowering/кодогена:
        // эмиттеры печатают приведение по argCasts, не пересчитывая его из сигнатуры.
        materializeArgCasts(ast_nodes, m_ctx);
    }
    core.finalize();

    // Согласованность модели семантики с типизированным AST проверяется только на УСПЕШНОМ
    // прогоне: в LSP (recover=true) частичный AST даёт законные «дыры» в фактах, это не ICE.
    if (!crashed && !m_analysis->hasErrors()) {
        if (const auto violation = verifyTypedAst(m_registry, ast_nodes)) {
            reportInternalError(m_ctx.diag(), m_recoverOnInternalError, *violation);
        }
    }

    // HIR и flow-анализы выполняет `Pipeline` (фазы: semantic → buildHir → verify → flow).
    // Runner завершает семантику: AST остаётся неизменяемой (кроме ErrorExpr-recovery), факты
    // материализованы в полях узлов; после ICE представление недостоверно и HIR не строится.
    m_crashed = crashed;
    return !crashed && !m_analysis->hasErrors();
}

std::unique_ptr<AnalysisContext> SemanticPassRunner::takeAnalysis() {
    return std::move(m_analysis);
}

} // namespace trust
