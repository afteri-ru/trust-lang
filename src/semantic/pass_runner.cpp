#include "semantic/pass_runner.hpp"

#include "semantic/name_resolution.hpp"
#include "semantic/macro_expander.hpp"
#include "semantic/lint.hpp"
#include "semantic/stack_check.hpp"
#include "semantic/stack_check_infer.hpp"
#include "semantic/symbol_collector.hpp"
#include "semantic/nativeref.hpp"
#include "semantic/borrow_check.hpp"
#include "semantic/ref_cycle.hpp"
#include "ast/lowering.hpp"
#include "ast/ast_nodes.hpp"
#include "diag/diag.hpp"
#include "diag/options.hpp"
#include "types/registry.hpp"
#include <exception>

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
    if (m_ctx.opts().is_enabled(semantic::FlagKind::Lint)) {
        core.addHook(std::make_unique<LintHook>(*m_analysis));
    }
    // Контроль переполнения стека для рекурсивных функций: только в режимах recursion/auto.
    {
        const auto scm = semantic::stackCheckModeFromOptions(m_ctx.opts());
        if (scm == semantic::StackCheckMode::kRecursion || scm == semantic::StackCheckMode::kAuto) {
            core.addHook(std::make_unique<StackCheckInferHook>(*m_analysis));
        }
    }
    // Сбор символов (имя+тип+диапазоны) для LSP - по флагу --Wsymbols / LSP-режим.
    if (m_ctx.opts().is_enabled(semantic::FlagKind::Symbols)) {
        core.addHook(std::make_unique<SymbolCollectorHook>(*m_analysis));
    }
    // Отслеживание инвалидации зависимых (атрибут @[borrowed], -Wborrowed=).
    // Всегда подключён: решает по структурным признакам и диагностику выдаёт только для
    // отслеживаемых сущностей; поведение (error|warning|ignore) - из -Wborrowed=.
    core.addHook(std::make_unique<NativeRefHook>(*m_analysis));

    // Статический borrow-checker (регионы/займы умных ссылок) - ВСЕГДА подключён (гарантия
    // языка / модель памяти). Флага включения/выключения нет; настраиваются только уровни
    // отдельных диагностик (`-Wborrow-<name>=<sev>`).
    core.addHook(std::make_unique<BorrowCheckHook>(*m_analysis));

    // Статический анализатор рекурсивных/циклических ссылок в полях классов - ВСЕГДА подключён
    // (гарантия модели памяти, REFType.md §11.11). Настраиваются только уровни диагностик
    // (`-Wrecursive-shared`/`-Wrecursive-unique`/`-Wrecursive-value`, default Error).
    core.addHook(std::make_unique<RefCycleHook>(*m_analysis));

    // -- Capture «$^ = результат последней операции» (простой случай): ПРЕ-семантическое
    //    структурное переписывание пары [оператор-выражение E / декларация x:=E; sink с $^] -> обычный
    //    код. Неподдержанные обращения `$^` проход сообщает САМ (в момент выявления, здесь известен
    //    предыдущий сиблинг) и заменяет лист `$^` на ErrorExpr-заглушку (семантика не дублирует ошибку).
    {
        LowerCtx lower_ctx;
        lower_ctx.ctx = &m_ctx; // AttrPool (для потенциальной пометки временных readonly/const)
        captureLastResult(ast_nodes, lower_ctx);
    }

    // Дожимаем finalize() даже если ядро бросило исключение на повреждённом AST
    // (allow_semantic_on_errors): собранные к этому моменту символы не теряются.
    // Исключение НЕ глотается молча: выдаётся диагностика (иначе пользователь/LSP
    // не узнает о внутренней ошибке анализатора).
    bool crashed = false;
    try {
        core.run(ast_nodes);
    } catch (const std::exception& e) {
        // Анализатор упал на частичном AST (напр. null-ребёнок повреждённого узла).
        // НЕ бросаем дальше: LSP должен получить накопленные символы и диагностики.
        m_ctx.diag().report(Severity::Error, MapperRange{}, "internal error during semantic analysis: {}", e.what());
        crashed = true;
    } catch (...) {
        m_ctx.diag().report(Severity::Error, MapperRange{}, "internal error during semantic analysis (unknown exception)");
        crashed = true;
    }
    core.finalize();

    // -- Lowering: последним, только при отсутствии блокирующих ошибок. --
    if (!m_analysis->hasErrors()) {
        LowerCtx lower_ctx;
        lower_ctx.ctx = &m_ctx; // AttrPool для пометки синтезированных временных readonly/const
        lowerBody(ast_nodes, lower_ctx);
    }

    return !crashed && !m_analysis->hasErrors();
}

SymbolIndex SemanticPassRunner::takeSymbolIndex() {
    return std::move(m_analysis->symbolIndex());
}

} // namespace trust
