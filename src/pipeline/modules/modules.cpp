// Модульные единицы трансляции: корневой узел, прогон модуля, выводы и файлы.
#include "pipeline/pipeline.hpp"
#include "ast/declaration_ids.hpp"
#include "pipeline/backend/stages.hpp"
#include "pipeline/paths/paths.hpp"
#include "ast/ast_nodes.hpp"
#include "ir/hir_module.hpp"
#include "module_loader/import_exports.hpp"
#include "module_loader/module_export.hpp"
#include "module_loader/module_loader.hpp"
#include "semantic/analysis_api.hpp"
#include "session/context.hpp"
#include "syntax/term/term.h"
#include "transpiler/transpiler.hpp"
#include "utils/utils.hpp"
#include "diag/diag.hpp"
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
namespace trust {

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
            if (!generic || fn.hasBody()) {
                continue;
            }
            // Native-импорты (`:= %cpp ...`) не эмитят C++-функцию: тела не требуется.
            if (fn.isNativeImport()) {
                continue;
            }
            // H-модель: определение обязано быть в ЭТОМ ЖЕ модуле, иначе заголовок невыразим.
            const AstNodePtr definition = facts.findFunctionDefinition(entry.qualifiedName, m_ctx.declCppFacts().cppSuffixOf(fn.declId()),
                                                                       [this](const FuncDecl& f) { return m_ctx.declCppFacts().cppSuffixOf(f.declId()); });
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

namespace {

/// C++-ключ определения (для детекции коллизий) либо пусто для «не определение».
/// multiple definition дают только определения; для функций ключ включает identity-суффикс —
/// перегрузки одной функции имеют разные C++-имена и коллизией не являются.
[[nodiscard]] std::optional<std::string> cppDefinitionKey(const AstNodePtr& node, std::string_view qualifiedName, const DeclCppFacts& facts) {
    if (node->is<FuncDecl>()) {
        const auto& fn = *node->as<FuncDecl>();
        if (!fn.hasBody() || fn.aliasKind() != AliasKind::None || fn.bindsNativeCpp()) {
            return std::nullopt;
        }
        return std::string(qualifiedName) + std::string(facts.cppSuffixOf(fn.declId()));
    }
    if (node->is<VarDecl>()) {
        return node->as<VarDecl>()->m_initializer != nullptr ? std::optional<std::string>(qualifiedName) : std::nullopt;
    }
    // struct/class с телом (не forward): повторное определение в C++ — ODR-нарушение.
    if (node->kind() == ParserToken::Kind::TypeDecl && isRecordTypeDecl(node, /*wantBody=*/true)) {
        return std::string(qualifiedName);
    }
    return std::nullopt;
}

} // namespace

void Pipeline::checkCrossModuleCollisions() {
    std::unordered_map<std::string, ModuleIndex> ownerOf;
    for (std::size_t i = 0; i < loader().moduleCount(); ++i) {
        const ModuleIndex idx{i};
        if (!loader().isLoaded(idx)) {
            continue;
        }
        for (const ExportEntry& entry : loader().interfaceFacts(idx).entries) {
            if (!entry.node) {
                continue;
            }
            const std::optional<std::string> key = cppDefinitionKey(entry.node, entry.qualifiedName, m_ctx.declCppFacts());
            if (!key.has_value()) {
                continue;
            }
            // Владелец объявления, а не модуль-фасад: реэкспорт не должен считаться вторым определением.
            const ModuleIndex owner = entry.node->module.isValid() ? entry.node->module : idx;
            const auto [it, inserted] = ownerOf.emplace(*key, owner);
            if (inserted || it->second == owner) {
                continue;
            }
            m_ctx.diag().report(Severity::Error, entry.node->range(),
                                "exported name '{}' is defined in multiple modules ({} and {}): generated C++ names are module-independent, "
                                "so the definitions collide; move one into a namespace ('ns:: {{ ... }}')",
                                entry.qualifiedName, loader().moduleLogicalName(it->second), loader().moduleLogicalName(owner));
        }
    }
}

std::vector<AstNodePtr> Pipeline::buildModuleRoot(ModuleIndex idx) {
    // Root node of the module is a ModuleNode wrapping the module body. Тело модуля уже
    // сконвертировано загрузчиком (одна AST на модуль) - берём те же узлы.
    // Имя корня — ЛОГИЧЕСКАЯ идентичность модуля (заголовок/stem), НЕ путь ФС: путь зависит от
    // cwd и сделал бы dump/диагностики нестабильными (относительные каталоги запрещены).
    auto modTerm = Term::Create(TermID::MODULE, std::string(loader().moduleLogicalName(idx)));
    auto mn = std::make_shared<ModuleNode>(idx, std::move(modTerm));
    EXPECT(loader().isLoaded(idx) && "module body must be converted by the loader");
    mn->m_body = loader().bodyAst(idx);
    std::vector<AstNodePtr> roots;
    roots.push_back(std::move(mn));
    // Идентичность объявлений (R23): назначает владелец представления — детерминированно, в порядке
    // объявлений модуля-владельца (идемпотентно; порядок загрузки независимых модулей на id не влияет).
    assignDeclarationIds(roots, loader().moduleLogicalName(idx), &m_ctx.declCppFacts());
    return roots;
}

Pipeline::ModuleUnitLowering Pipeline::lowerModuleUnit(ModuleIndex moduleIdx) {
    ModuleUnitLowering unit;
    // Корневой узел модуля с полным телом (определения) - отдельная единица трансляции.
    unit.astNodes = buildModuleRoot(moduleIdx);
    resolveImportExports(m_ctx, loader(), unit.astNodes); // вложенные импорты внутри модуля

    if (m_ctx.diag().errorCount() > 0) {
        return unit;
    }
    // Семантика уже выполнена в главном юните над ТЕМИ ЖЕ (разделяемыми) узлами AST:
    // модульный юнит - codegen-проекция. Повторный semantic/flow-прогон не выполняется,
    // поэтому повторной регистрации типов/символов и дублей диагностик нет.
    // ЕДИНСТВЕННАЯ точка построения HIR — `buildLoweredHir` (та же, что у LowerHir-фазы).
    unit.hir = buildLoweredHir(unit.astNodes, /*verify=*/true);
    // Ошибки верификатора HIR тоже останавливают кодогенерацию (hir остаётся пустым).
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

std::string Pipeline::moduleLogicalName(ModuleIndex idx) const {
    return std::string(loader().moduleLogicalName(idx));
}

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
        backend::BackendServices services{m_ctx.runServices(), m_opts, loader()};
        backend::CppArtifact artifact = backend::emitCppArtifact(
            services, backend::CodegenRequest{*unit.hir, backend::CppTarget{outputIdx, modPath}, /*entryFunctionName=*/std::nullopt,
                                              /*programRecord=*/{}, &*header, codegenModes(semantic::behavioralModesFromOptions(m_ctx.opts()), m_ctx.opts())});
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
