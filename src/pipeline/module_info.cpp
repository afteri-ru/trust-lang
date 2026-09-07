// src/pipeline/module_info.cpp
// Команда --module-info: вывод версии и экспортов скомпилированного модуля (.so/.trust).
#include "pipeline/module_info.hpp"
#include "utils/io.hpp"
#include "runtime/module_api.h"
#include <dlfcn.h>
#include <string>
namespace trust {

// -- --module-info: show module exports and version --
int showModuleInfo(const std::string& module_path, bool verbose) {
    void* handle = dlopen(module_path.c_str(), RTLD_LAZY);
    if (!handle) {
        trust::errs() << "error: failed to load module '" << module_path << "': " << dlerror() << "\n";
        return 1;
    }
    dlerror();
    using GetExportsFn = __trust_exports (*)();
    auto get_exports = reinterpret_cast<GetExportsFn>(dlsym(handle, "__trust_get_exports"));
    const char* dlsym_error = dlerror();
    if (dlsym_error) {
        trust::errs() << "error: __trust_get_exports not found in '" << module_path << "': " << dlsym_error << "\n";
        dlclose(handle);
        return 1;
    }
    __trust_exports exports = get_exports();
    trust::outs() << "version: " << (exports.version ? exports.version : "(null)") << "\n";
    trust::outs() << "src-md5: " << (exports.srcHash ? exports.srcHash : "(null)") << "\n";
    trust::outs() << "exports: " << exports.count << "\n";
    for (int i = 0; i < exports.count; ++i) {
        trust::outs() << "  " << (exports.entries[i].name ? exports.entries[i].name : "(null)") << "\n";
    }
    if (verbose) {
        trust::errs() << "info: module loaded successfully from " << module_path << "\n";
    }
    dlclose(handle);
    return 0;
}
} // namespace trust
