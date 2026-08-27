#include "diag/registry.hpp"

#include "diag/base_diags.hpp"

#include <mutex>
#include <vector>

namespace trust {

namespace {
std::mutex g_registry_mutex;
std::vector<std::function<void(Options&)>>& callbacks() {
    static std::vector<std::function<void(Options&)>> v;
    return v;
}
} // namespace

void registerDiagnostics(const std::function<void(Options&)>& fn) {
    std::lock_guard<std::mutex> lock(g_registry_mutex);
    callbacks().push_back(fn);
}

void applyRegisteredDiagnostics(Options& opts) {
    // Базовые (diag-владение) диагностики: Deprecated и ParseError (общая, используется и
    // transpiler, и types). Остальные диагностики/флаги регистрируют сами компоненты.
    opts.add(diag::DiagId::Deprecated);
    opts.add(diag::DiagId::ParseError);

    std::lock_guard<std::mutex> lock(g_registry_mutex);
    for (const auto& fn : callbacks()) {
        fn(opts);
    }
}

} // namespace trust
