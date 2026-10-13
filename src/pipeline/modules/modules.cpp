// src/pipeline/modules/modules.cpp
// Модульные единицы трансляции: корневой узел, прогон модуля, выводы и файлы.
#include "pipeline/pipeline.hpp"
#include "pipeline/backend/stages.hpp"
#include "pipeline/paths/paths.hpp"
#include "ast/ast_nodes.hpp"
#include "ir/hir_module.hpp"
#include "module_loader/import_exports.hpp"
#include "module_loader/module_export.hpp"
#include "module_loader/module_loader.hpp"
#include "session/context.hpp"
#include "syntax/term/term.h"
#include "utils/utils.hpp"
#include "diag/diag.hpp"
#include <filesystem>
#include <optional>
#include <string>
#include <vector>
namespace trust {
// -- checkExportedGenericDeclarations: declaration-only форма не выражает шаблоны --

void Pipeline::checkExportedGenericDeclarations(ModuleIndex mainModuleIndex) {
    for (std::size_t i = 0; i < loader().moduleCount(); ++i) {
        const ModuleIndex idx{i};
        if (idx == mainModuleIndex || !loader().isLoaded(idx)) {
            continue;
        }
        const ModuleInterfaceFacts& facts = loader().interfaceFacts(idx);
        for (const ExportEntry& entry : facts.entries) {
            if (!entry.node || !entry.node->is<FuncDecl>()) {
                continue;
            }
            const FuncDecl& fn = *entry.node->as<FuncDecl>();
            const bool generic = fn.m_templateParams.has_value() && !fn.m_templateParams->empty();
            if (!generic || fn.m_body.has_value()) {
                continue;
            }
            // Native-импорты (`%name`) не эмитят C++-функцию: тела не требуется.
            if (fn.m_isNativeImport || fn.m_isNativeTemplateImport || fn.m_isNativeTemplateCtor) {
                continue;
            }
            // H-модель: определение обязано быть в ЭТОМ ЖЕ модуле, иначе заголовок невыразим.
            const AstNodePtr definition = facts.findFunctionDefinition(entry.qualifiedName, fn.m_cppSuffix);
            if (definition) {
                continue;
            }
            m_ctx.diag().report(Severity::Error, fn.range(),
                                "module declares generic function '{}' without a body: declaration-only form cannot express C++ templates "
                                "(add a definition in this module)",
                                entry.qualifiedName);
        }
    }
}

std::vector<AstNodePtr> Pipeline::buildModuleRoot(const std::string& moduleName, ModuleIndex idx) {
    // Root node of the module is a ModuleNode wrapping the module body. Тело модуля уже
    // сконвертировано загрузчиком (одна AST на модуль) - берём те же узлы.
    auto modTerm = Term::Create(TermID::MODULE, moduleName);
    auto mn = std::make_shared<ModuleNode>(idx, std::move(modTerm));
    EXPECT(loader().isLoaded(idx) && "module body must be converted by the loader");
    mn->m_body = loader().bodyAst(idx);
    std::vector<AstNodePtr> roots;
    roots.push_back(std::move(mn));
    return roots;
}

// -- runFrontend: ParseAST -> AST -> error-gate -> Semantic -> symbols --

Pipeline::ModuleUnitLowering Pipeline::lowerModuleUnit(ModuleIndex moduleIdx) {
    ModuleUnitLowering unit;
    // Корневой узел модуля с полным телом (определения) - отдельная единица трансляции.
    unit.astNodes = buildModuleRoot(std::string(loader().modulePath(moduleIdx)), moduleIdx);
    resolveImportExports(m_ctx, loader(), unit.astNodes); // вложенные импорты внутри модуля

    if (m_ctx.diag().errorCount() > 0) {
        return unit;
    }
    // Семантика уже выполнена в главном юните над ТЕМИ ЖЕ (разделяемыми) узлами AST:
    // модульный юнит - codegen-проекция. Повторный semantic/flow-прогон не выполняется,
    // поэтому повторной регистрации типов/символов и дублей диагностик нет.
    LoweredUnit lowered = lowerAndAnalyze(unit.astNodes, /*verify=*/true, /*runFlow=*/false);
    unit.hir = std::move(lowered.hir);
    // Ошибки верификатора HIR тоже останавливают кодогенерацию.
    if (m_ctx.diag().errorCount() > 0) {
        return unit;
    }
    EXPECT(unit.hir.has_value() && "module unit must produce HIR from analyzed AST");
    return unit;
}

std::optional<PipelineResult> Pipeline::runCppToFile(MapperFile inputFile) {
    const std::filesystem::path cpptPath = computeCpptPath(m_opts);
    // Use the real .cppt basename so the source map's output filename can be
    // resolved back to the generated file (findFile / findCppToTrust).
    MapperFile outputIdx = m_ctx.source().add_output(cpptPath.filename().string());
    if (outputIdx.isInvalid()) {
        trust::errs() << "error: failed to create output file entry\n";
        return std::nullopt;
    }

    RunRequest req;
    req.cpp = backend::CppTarget{outputIdx, cpptPath};
    PipelineResult result = run(req, inputFile);
    if (!result.isValid() || m_ctx.diag().errorCount() > 0 || !result.cpp || !result.cpp->saved) {
        return std::nullopt;
    }
    return result;
}

// -- generateModuleOutputs: артефакты исходных модулей (`.hpp` + `.cppt`) --

Pipeline::ModuleOutputs Pipeline::generateModuleOutputs(ModuleIndex mainModuleIndex) {
    namespace fs = std::filesystem;
    ModuleOutputs out;
    const fs::path build_dir = computeBuildDir(m_opts);
    for (std::size_t i = 0; i < loader().moduleCount(); ++i) {
        const ModuleIndex idx{i};
        if (idx == mainModuleIndex) {
            continue; // главный файл генерируется как основной .cppt
        }
        if (!loader().isLoaded(idx)) {
            continue; // незагруженный модуль не транслируем
        }
        // Один lowering на модуль: из него строятся И `.hpp`, И `.cppt`.
        ModuleUnitLowering unit = lowerModuleUnit(idx);
        if (!unit.hir) {
            out.ok = false; // диагностика уже выдана (frontend/verify)
            return out;
        }
        // Заголовок получает КАЖДЫЙ импортируемый модуль, включая declaration-only: его интерфейс
        // (типы, прототипы, native-декларации) — самостоятельный артефакт для клиентов.
        const fs::path headerPath = moduleHeaderPath(build_dir, loader().moduleLogicalName(idx));
        const std::optional<backend::HeaderArtifact> header = emitModuleHeader(*unit.hir, idx, headerPath);
        if (!header) {
            out.ok = false; // диагностика уже выдана
            return out;
        }
        // `@[include]`/`@[link]` заголовка нужны всегда: опубликованные там типы в `.cppt` не
        // повторяются, а у declaration-only модуля `.cppt` вообще нет.
        out.packageHeaders.insert(out.packageHeaders.end(), header->packageHeaders.begin(), header->packageHeaders.end());
        out.linkLibs.insert(out.linkLibs.end(), header->linkLibs.begin(), header->linkLibs.end());
        // Declaration-only модуль (объявления/native-импорты без тел) не даёт out-of-line
        // определений — отдельный `.cppt` не нужен и не должен попадать в SRC_MODULES.
        if (!loader().interfaceFacts(idx).hasOutOfLineDefinitions) {
            continue;
        }
        const fs::path modPath = moduleCppPath(build_dir, loader().modulePath(idx));
        MapperFile outputIdx = m_ctx.source().add_output(modPath.filename().string());
        if (outputIdx.isInvalid()) {
            trust::errs() << "error: failed to create output file entry: " << modPath.filename().string() << "\n";
            out.ok = false;
            return out;
        }
        // Единый C++-бэкенд с главным юнитом: модульный `.cppt` — проекция готовых фактов.
        // `&*header` передаёт `#include` своего заголовка и набор опубликованных им объявлений.
        backend::BackendServices services{m_ctx, m_opts, loader()};
        backend::CppArtifact artifact = backend::emitCppArtifact(
            services, backend::CodegenRequest{*unit.hir, backend::CppTarget{outputIdx, modPath}, /*entryFunctionName=*/std::nullopt, /*programRecord=*/{}, &*header});
        if (!artifact.saved) {
            out.ok = false; // диагностика уже выдана (codegen/запись)
            return out;
        }
        out.packageHeaders.insert(out.packageHeaders.end(), artifact.packageHeaders.begin(), artifact.packageHeaders.end());
        out.linkLibs.insert(out.linkLibs.end(), artifact.linkLibs.begin(), artifact.linkLibs.end());
        out.cpptPaths.push_back(modPath);
    }
    return out;
}

} // namespace trust
