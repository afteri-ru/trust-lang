// Заголовки модулей и emit-вывод (tokens/ast/macros).
#include "pipeline/pipeline.hpp"
#include "pipeline/backend/emit.hpp"
#include "pipeline/paths/paths.hpp"
#include "ast/declaration_ids.hpp"
#include "module_loader/module_export.hpp"
#include "module_loader/module_loader.hpp"
#include "semantic/analysis_api.hpp"
#include "session/context.hpp"
#include "syntax/parser.h"
#include "transpiler/transpiler.hpp"
#include "utils/error.hpp"
#include "utils/utils.hpp"
#include <algorithm>
#include <fstream>
#include <optional>
#include <set>
#include <unordered_set>
namespace trust {

namespace {

/// Публикация объявления заголовком: ключ — `DeclId` (R23), не адрес узла.
void publishDeclId(std::set<DeclId>& published, const AstNodeBase& node) {
    const DeclId id = declarationIdOf(node);
    EXPECT(id.isValid() && "published declaration must carry DeclId (R23)");
    published.insert(id);
}

} // namespace
// Заголовок модуля, объявления которого публикует umbrella-`#include` (реэкспорт, алиас цели).
// Имя строит та же формула, что имя самого заголовка; дубликаты не добавляются.
void addModuleInclude(std::vector<std::string>& includes, const std::filesystem::path& buildDir, const ModuleLoader& loader, ModuleIndex owner) {
    const std::string include = moduleHeaderPath(buildDir, loader.moduleLogicalName(owner)).filename().string();
    if (std::find(includes.begin(), includes.end(), include) == includes.end()) {
        includes.push_back(include);
    }
}

std::optional<backend::HeaderArtifact> Pipeline::emitModuleHeader(const HirModule& hir, ModuleIndex moduleIdx, const std::filesystem::path& path) {
    MapperFile output_idx = m_ctx.source().add_output(path.filename().string());
    if (output_idx.isInvalid()) {
        trust::errs() << "error: failed to create output file entry: " << path.filename().string() << "\n";
        return std::nullopt;
    }
    // Экспорт-набор и определения модуля — единые факты интерфейса, посчитанные загрузчиком.
    const ModuleInterfaceFacts& interfaceFacts = loader().interfaceFacts(moduleIdx);
    std::vector<AstNodePtr> headerNodes;                   // узлы, эмитируемые в заголовок
    std::vector<const AstNodeBase*> publishedDeclarations; // определения, которые `.cppt` не повторяет
    std::set<DeclId> headerPublishedDecls;                 // всё, что заголовок объявляет (для проверки тела)
    std::unordered_set<ExportedFunctionKey, ExportedFunctionKeyHash> seenFunctions;
    std::vector<ExportEntry> headerEntries = interfaceFacts.entries;
    headerEntries.insert(headerEntries.end(), interfaceFacts.protectedEntries.begin(), interfaceFacts.protectedEntries.end());
    // Umbrella: заголовки РЕЭКСПОРТИРУЕМЫХ модулей и модулей-владельцев АЛИАСОВ интерфейса
    // (их объявления живут в собственных заголовках и здесь не дублируются). Владельцы берутся
    // из самого экспорт-набора: чужая запись несёт `node->module` модуля-владельца.
    std::vector<std::string> moduleIncludes;
    for (const ExportEntry& entry : headerEntries) {
        if (entry.node && entry.node->module.isValid() && entry.node->module != moduleIdx) {
            addModuleInclude(moduleIncludes, path.parent_path(), loader(), entry.node->module);
        }
    }
    for (const ExportEntry& entry : headerEntries) {
        if (!entry.node) {
            continue;
        }
        // Реэкспортированное имя принадлежит модулю-владельцу: его объявление публикует
        // umbrella-`#include` заголовка владельца, здесь оно не эмитится.
        if (entry.node->module != moduleIdx) {
            continue;
        }
        // Алиас имени — не объявление: собственной C++-декларации нет, но цель должна быть
        // объявлена у клиента — включаем заголовок модуля-владельца цели.
        if (entry.node->is<FuncDecl>() && entry.node->as<FuncDecl>()->aliasKind() != AliasKind::None) {
            const FuncDecl& fn = *entry.node->as<FuncDecl>();
            EXPECT(fn.aliasKind() == AliasKind::Name && "export set must not contain namespace/module alias");
            const AstNodeBase* target = fn.aliasTarget().get();
            if (target != nullptr && target->is<IdentName>() && target->as<IdentName>()->moduleRef.has_value()) {
                addModuleInclude(moduleIncludes, path.parent_path(), loader(), *target->as<IdentName>()->moduleRef);
            }
            continue;
        }
        if (entry.node->is<FuncDecl>()) {
            const FuncDecl& fn = *entry.node->as<FuncDecl>();
            const std::string_view exportSuffix = m_ctx.declCppFacts().cppSuffixOf(fn.declId());
            const ExportedFunctionKey key{entry.qualifiedName, std::string(exportSuffix)};
            if (!seenFunctions.insert(key).second) {
                continue; // forward + definition одного объявления: в заголовке одна запись
            }
            AstNodePtr definition;
            if (fn.hasBody()) {
                definition = entry.node;
            } else {
                definition = interfaceFacts.findFunctionDefinition(entry.qualifiedName, exportSuffix,
                                                                   [this](const FuncDecl& f) { return m_ctx.declCppFacts().cppSuffixOf(f.declId()); });
            }
            const FuncDecl& definitionFn = definition ? *definition->as<FuncDecl>() : fn;
            const bool bodyInHeader = bodyMustBeInModuleHeader(fn, m_ctx.attrs()) || bodyMustBeInModuleHeader(definitionFn, m_ctx.attrs());
            if (bodyInHeader) {
                if (!definition) {
                    // generic без тела уже диагностирован checkExportedGenericDeclarations.
                    const bool generic = fn.m_templateParams.has_value() && !fn.m_templateParams->empty();
                    const bool native = fn.isNativeImport();
                    if (!generic && !native) {
                        m_ctx.diag().report(Severity::Error, fn.range(),
                                            "exported constexpr function '{}' must have a body: the module header publishes its definition",
                                            entry.qualifiedName);
                    }
                    continue;
                }
                headerNodes.push_back(definition);
                publishDeclId(headerPublishedDecls, *entry.node);
                publishDeclId(headerPublishedDecls, *definition);
                publishedDeclarations.push_back(definition.get());
                continue;
            }
            headerNodes.push_back(entry.node);
            publishDeclId(headerPublishedDecls, *entry.node);
            if (definition) {
                publishDeclId(headerPublishedDecls, *definition);
            }
            continue;
        }
        const bool is_type = entry.node->kind() == ParserToken::Kind::TypeDecl;
        if (is_type) {
            publishedDeclarations.push_back(entry.node.get());
        }
        headerNodes.push_back(entry.node);
        publishDeclId(headerPublishedDecls, *entry.node);
    }
    // Umbrella уже собран (реэкспорт + владельцы алиасов).
    CodegenServices codegen = m_ctx.runServices();
    CppTranspiler transpiler(codegen, codegenModes(semantic::behavioralModesFromOptions(m_ctx.opts()), m_ctx.opts()));
    transpiler.generateHeaderToFile(hir, output_idx, moduleIdx, headerNodes, moduleIncludes, headerPublishedDecls, path.filename().string());
    if (m_ctx.diag().errorCount() > 0) {
        return std::nullopt; // диагностика уже выдана
    }
    backend::HeaderArtifact artifact;
    artifact.index = output_idx;
    artifact.path = path;
    artifact.packageHeaders.assign(transpiler.packageHeaders().begin(), transpiler.packageHeaders().end());
    artifact.linkLibs.assign(transpiler.linkLibs().begin(), transpiler.linkLibs().end());
    artifact.publishedDeclarations = std::move(publishedDeclarations);
    std::ofstream ofs(path);
    if (!ofs) {
        trust::errs() << "error: failed to write header file: " << path << "\n";
        return std::nullopt;
    }
    ofs << m_ctx.source().output_result(output_idx);
    artifact.saved = true;
    if (m_opts.verbose) {
        trust::errs() << "info: generated " << path << "\n";
    }
    return artifact;
}

int printEmitOutput(const PipelineOpts& opts, Context& ctx, const PipelineResult& result) {
    auto flags = opts.emit_flags;

    if ((flags & EmitFlags::Tokens) != EmitFlags::None) {
        EXPECT(result.astNodes.has_value() && "runAst must produce astNodes");
        for (const auto& nodePtr : *result.astNodes) {
            if (nodePtr) {
                const HasText* textNode = nodePtr->as_text();
                trust::outs() << (textNode != nullptr ? textNode->text() : std::string_view{}) << "\t" << ParserToken::name(nodePtr->kind()) << "\n";
            }
        }
        return 0;
    }

    if ((flags & EmitFlags::AST) != EmitFlags::None) {
        EXPECT(result.astNodes.has_value() && "runAst must produce astNodes");
        // AST-режим идёт без семантики (ParseAST): HIR в нём не строится, печатается дерево разбора.
        for (const auto& nodePtr : *result.astNodes) {
            if (nodePtr) {
                trust::outs() << nodePtr->dump() << "\n";
            }
        }
        return 0;
    }

    if ((flags & EmitFlags::Macros) != EmitFlags::None) {
        // Дамп макроопределений (для отладки/диагностики макропроцессора).
        for (const auto& md : ctx.macroDefs()) {
            trust::outs() << md.name << "\n";
        }
        return 0;
    }

    FAULT("unreachable: printEmitOutput called for emit-flags without matching handler");
    return 1;
}

} // namespace trust
