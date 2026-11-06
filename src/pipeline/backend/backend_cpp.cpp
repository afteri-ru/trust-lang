#include "pipeline/backend/stages.hpp"

#include "location/module_name.hpp"
#include "pipeline/backend/checks.hpp"
#include "pipeline/run/run.hpp"
#include "pipeline/sourcemap/source_map.hpp"
#include "module_loader/module_loader.hpp"
#include "session/run_services.hpp"
#include "driver/codegen_args.hpp"
#include "transpiler/transpiler.hpp"
#include "utils/error.hpp"
#include "utils/utils.hpp"

#include <filesystem>
#include <string>

namespace trust {
namespace backend {

bool cppStageRequested(const BackendRequest& req) noexcept {
    return req.cpp.has_value();
}

CppArtifact emitCppArtifact(BackendServices& services, const CodegenRequest& request) {
    EXPECT(!request.target.index.isInvalid() && "CppTarget::index must be a valid output file");
    // Кодоген читает только HIR и материализованные в узлах факты: таблица символов анализа
    // (и любые её fallback-и) в бэкенд не передаётся.
    CodegenServices codegen = services.services;
    CppTranspiler transpiler(codegen, request.modes);
    if (request.entryFunctionName) {
        transpiler.configureSingleFile(true, *request.entryFunctionName);
    }
    if (request.moduleHeader != nullptr) {
        transpiler.configureModuleUnit(request.moduleHeader->path.filename().string(), request.moduleHeader->publishedDeclarations);
    }
    transpiler.generateToFile(request.hir, request.target.index);
    reportUnhandledAttributes(services.services, request.hir);

    CppArtifact artifact;
    artifact.index = request.target.index;
    artifact.path = request.target.path;
    artifact.packageHeaders.assign(transpiler.packageHeaders().begin(), transpiler.packageHeaders().end());
    artifact.linkLibs.assign(transpiler.linkLibs().begin(), transpiler.linkLibs().end());
    artifact.entryParams = transpiler.entryParams();
    artifact.sawEntry = transpiler.sawEntry();
    if (services.services.diag().errorCount() > 0) {
        return artifact;
    }
    if (!request.target.path.empty()) {
        if (!saveCppAndEmbedSourceMap(services.services, artifact.index, request.target.path, services.opts.verbose, request.programRecord,
                                      /*embed_source_map=*/services.opts.embed_source_map, codegenArgsRecord(services.opts))) {
            return artifact;
        }
        if (services.opts.verbose) {
            trust::errs() << "info: generated " << request.target.path << "\n";
        }
        artifact.saved = true;
    }
    return artifact;
}

StageOutcome runCppStage(BackendServices& services, const BackendRequest& req, StageOutputs& out) {
    EXPECT(req.hir != nullptr && "cpp stage requires HIR");
    EXPECT(req.cpp.has_value() && "cpp stage requested without CppTarget");

    const std::optional<std::string> entry_function_name =
        services.opts.single_file ? std::optional<std::string>(moduleEntryIdent(services.loader.moduleLogicalName(req.mainModuleIndex))) : std::nullopt;
    std::string program_record;
    if (!req.cpp->path.empty()) {
        program_record = buildProgramRecord(std::filesystem::path(services.opts.input_file), services.loader, req.mainModuleIndex, services.opts);
    }
    out.cpp = emitCppArtifact(services, CodegenRequest{*req.hir, *req.cpp, entry_function_name, std::move(program_record), nullptr, req.modes});
    const bool write_failed = !req.cpp->path.empty() && !out.cpp->saved;
    return (services.services.diag().errorCount() > 0 || write_failed) ? StageOutcome::Error : StageOutcome::Done;
}

} // namespace backend
} // namespace trust
