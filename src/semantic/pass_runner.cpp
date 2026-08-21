#include "semantic/pass_runner.hpp"

#include "semantic/name_resolution.hpp"
#include "semantic/macro_expander.hpp"
#include "semantic/lint.hpp"
#include "semantic/symbol_collector.hpp"
#include "ast/lowering.hpp"
#include "ast/ast_nodes.hpp"
#include "diag/options.hpp"
#include "types/registry.hpp"

namespace trust {

SemanticPassRunner::SemanticPassRunner(Context& ctx)
: m_ctx(ctx)
, m_analysis(std::make_unique<AnalysisContext>(ctx)) {
}

bool SemanticPassRunner::run(std::vector<AstNodePtr>& ast_nodes) {
    // Свежий контекст на каждый запуск (чистая таблица символов и скоуп-стек).
    m_analysis = std::make_unique<AnalysisContext>(m_ctx);

    // Упорядоченный жизненный цикл типов: реестр на каждый запуск сбрасывается к
    // builtin-состоянию, чтобы пользовательские алиасы и функциональные сигнатуры
    // не накапливались между run() (SymbolTable пер-ран; типы должны быть согласованы).
    m_ctx.types().reset();

    // -- Единое ядро разрешения имён (однопроходный NameResolutionPass). --
    // Раскрытие контекст-макросов выполняет всегда-подключённый хук ContextMacroExpander
    // в том же обходе (в начале обработки каждого узла). Опциональные анализаторы
    // подключаются ПАРАЛЛЕЛЬНО к ядру. Флаг включения проверяется ОДИН раз здесь;
    // отключённый хук в список активных не попадает и его колбэки в узлах не вызываются.
    NameResolutionPass core(*m_analysis);
    // Раскрытие контекст-макросов - ВСЕГДА (обязательная часть семантики, не опциональный
    // анализатор). Подключается ПЕРВЫМ, чтобы его onNode раскрывал ContextMacro/квалификатор
    // @:: до обработки ядра.
    core.addHook(std::make_unique<ContextMacroExpander>(*m_analysis));
    if (m_ctx.opts().is_enabled(FlagKind::Lint)) {
        core.addHook(std::make_unique<LintHook>(*m_analysis));
    }
    // Сбор символов (имя+тип+диапазоны) для LSP - по флагу --Wsymbols / LSP-режим.
    if (m_ctx.opts().is_enabled(FlagKind::Symbols)) {
        core.addHook(std::make_unique<SymbolCollectorHook>(*m_analysis));
    }

    // Дожимаем finalize() даже если ядро бросило исключение на повреждённом AST
    // (allow_semantic_on_errors): собранные к этому моменту символы не теряются.
    bool crashed = false;
    try {
        core.run(ast_nodes);
    } catch (...) {
        // Анализатор упал на частичном AST (напр. null-ребёнок повреждённого узла).
        // НЕ бросаем дальше: LSP должен получить накопленные символы и диагностики.
        // finalize() ниже сбросит уже собранные хуками данные. Транспиляцию не запускаем.
        crashed = true;
    }
    core.finalize();

    // -- Lowering: последним, только при отсутствии блокирующих ошибок. --
    if (!m_analysis->hasErrors()) {
        LowerCtx lower_ctx;
        lowerBody(ast_nodes, lower_ctx);
    }

    return !crashed && !m_analysis->hasErrors();
}

SymbolIndex SemanticPassRunner::takeSymbolIndex() {
    return std::move(m_analysis->symbolIndex());
}

} // namespace trust
