#include "pipeline/pipeline.hpp"
#include "pipeline/run/phases.hpp"
#include "pipeline/run/target_options.hpp"
#include "pipeline/dsl/dsl.hpp"
#include "module_loader/module_loader.hpp"
#include "semantic/analysis_api.hpp"
#include "session/context.hpp"
#include "target/target.hpp"
#include "types/type_read.hpp"
#include "types/type_system.hpp"
#include "utils/error.hpp"
#include "utils/string_interner.hpp"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace trust {

// Документирующие комментарии к объявлениям привязываются ГРАММАТИКОЙ к терму-идентификатора
// (term->m_docs, см. include/syntax/grammar/parser.y.in: attachLeadingDoc/attachTrailingDoc) и переносятся
// в узел объявления TermToAstConverter::convert → AstNodeBase::documentation. SymbolCollectorHook
// читает их в finalize. Отдельный AST-обход (moduleDocMap/attachDocumentation) не требуется.
// Prelude-файлы читаются из пакета ресурсов `share/trust/stdlib.trust`
// (package::readResourcePackageEntry). Локального встраивания здесь НЕТ: иначе
// содержимое дублировалось бы с провайдером и с LSP (src/lsp/catalog/builtin_catalog.cpp использует
// тот же аксессор).

Pipeline::Pipeline(Context& ctx, const PipelineOpts& opts)
: m_ctx(ctx)
, m_stringsScope(ctx.strings())
, m_opts(opts) {
    m_ctx.diag().setMinSeverity(opts.quiet ? Severity::Error : Severity::Remark);
    // Pipeline владеет ModuleLoader и TypeSystem и внедряет их в Context
    // (невладеющие указатели), чтобы diag не зависел от module_loader и types.
    m_loader = std::make_unique<ModuleLoader>(m_ctx);
    m_ctx.setModuleResolver(m_loader.get());
    m_types = std::make_shared<TypeSystem>(m_ctx.diag(), m_ctx.opts());
    m_ctx.setTypes(m_types.get());
    // Целевой контракт прогона (целевые C++-факты вне языковой модели): владеет Pipeline, порт — Context.
    // Значения считаются ОДИН раз здесь и хранятся у владельца контракта (R37/E20).
    m_target = std::make_unique<TargetContract>(targetInfoFromOptions(m_opts));
    m_ctx.setTarget(m_target.get());
    seedIntrinsicCppFacts();
}

void Pipeline::seedIntrinsicCppFacts() {
    // Seed целевых C++-фактов встроенных интринсиков (C++-имя/include) из статической таблицы
    // компонента `target` — единственный источник целевых фактов этих типов (R1/R37). Привязка
    // «trust-имя → TypeId» — здесь (таблица `target` знает только имена).
    const TypeReadModel types = m_ctx.readTypes();
    for (const BuiltinCppSeedEntry& entry : builtinCppSeed()) {
        const std::optional<TypeId> id = types.findType(entry.name);
        if (!id.has_value()) {
            continue;
        }
        CppTypeFacts facts;
        facts.cppName = std::string(entry.cppName);
        for (const std::string_view include : entry.includes) {
            facts.preprocIncludes.emplace_back(include);
        }
        m_target->typeCppFacts().seed(types.store().cppFactsKeyOf(types.store().desugaredTypeId(*id)), std::move(facts));
    }
}

Pipeline::~Pipeline() = default;

const TargetContract& Pipeline::targetContract() const noexcept {
    return *m_target;
}

void Pipeline::loadDslMacros() {
    trust::loadDslMacros(m_ctx, m_opts);
}

std::vector<std::string> Pipeline::effectiveKeywords() {
    return trust::effectiveKeywords(m_ctx, m_opts);
}

PipelineResult Pipeline::run(const RunRequest& req, MapperFile inputFile) {
    // Per-run владельцы (TargetContract, SourceMapWriter, ModuleLoader) создаются конструктором
    // и не сбрасываются вместе с TypeSystem; второй прогон того же Pipeline дал бы устаревшие
    // целевые факты и повторные source-map записи. Новый прогон — новый Pipeline/Context.
    EXPECT(!m_hasRun && "Pipeline is single-compilation: create a new Pipeline for another run");
    m_hasRun = true;
    // Бэкенды кодируют уже разрешённые имена/типы: без семантики они бессмысленны.
    EXPECT((!req.smt || hasStep(req.steps, FrontendSteps::Semantic)) && "SMT backend requires the Semantic step");
    EXPECT((!req.cpp || hasStep(req.steps, FrontendSteps::Semantic)) && "C++ backend requires the Semantic step");
    // Все тексты AST/HIR/символов этой компиляции интернируются в пул строк Context
    // (per-compilation), а не в процессный interner: память освобождается вместе с Context/результатом.
    // Активный пул установлен на всё время жизни Pipeline (см. m_stringsScope).
    // Runner живёт до конца прогона, чтобы разрешённая семантикой таблица символов (TypeId) была
    // доступна кодогенерации (проброс в CppTranspiler).
    SemanticPassRunner runner(m_ctx, *m_types, m_opts.allow_semantic_on_errors);
    PhaseRun run;
    run.req = req;
    // Единственная точка прогона: фазы исполняются декларативной таблицей PIPELINE_PHASE_LIST
    // (см. pipeline/run/phases.hpp); гейты остановки — внутри фаз (PhaseRun::stopped).
    runPhases(runner, inputFile, run);
    PipelineResult result = std::move(run.result);
    result.mainModuleIndex = run.mainModuleIndex;
    // Результат разделяет владение пулом строк: AST/HIR/SymbolIndex остаются валидны
    // после разрушения Context.
    result.strings = m_ctx.stringsPool();
    return result;
}

} // namespace trust
