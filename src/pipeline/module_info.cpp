// src/pipeline/module_info.cpp
// Команда --module-info: вывод версии и экспортов скомпилированного модуля (.so/.trust).
// Данные читаются из ELF-секции .trust.sabi без dlopen (см. module_loader/module_sabi.hpp).
#include "pipeline/module_info.hpp"
#include "module_loader/module_sabi.hpp"
#include "session/context.hpp"
#include "syntax/parser.h"
#include "utils/io.hpp"
#include <string>
#include <vector>
namespace trust {

// -- --module-info: show module exports and version --
int showModuleInfo(Context& ctx, const std::string& module_path, const std::string& masks, bool verbose) {
    std::string error;
    std::optional<SabiData> data = readModuleSabi(module_path, error);
    if (!data) {
        trust::errs() << "error: " << error << "\n";
        return 1;
    }

    // Валидация: декларации обязаны быть валидным Trust-текстом (восстановление парсером языка).
    std::string decl_text;
    for (const auto& decl : data->decls) {
        decl_text += decl;
        decl_text += '\n';
    }
    Parser parser(ctx);
    parser.ParseText(decl_text, "@sabi", /*expand_module=*/false);
    if (ctx.diag().errorCount() > 0) {
        trust::errs() << "error: invalid Trust declaration in '.trust.sabi' of '" << module_path << "'\n";
        return 1;
    }

    const std::vector<std::string> decls = filterModuleDecls(data->decls, masks);
    trust::outs() << "version: " << data->compilerVersion << "\n";
    trust::outs() << "src-md5: " << data->srcRecord << "\n";
    trust::outs() << "exports: " << decls.size() << "\n";
    for (const auto& decl : decls) {
        trust::outs() << "  " << decl << "\n";
    }
    if (verbose) {
        trust::errs() << "info: module info read from " << module_path << " (section .trust.sabi, no dlopen)\n";
    }
    return 0;
}
} // namespace trust
