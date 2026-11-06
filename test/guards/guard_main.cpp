// test/guards/guard_main.cpp
// Link-guard: нижние слои (session/syntax/ast) не обращаются к ModuleLoader.
// Используются только их собственные символы; module_loader_lib НЕ линкуется.
#include "ast/ast_nodes.hpp"
#include "session/context.hpp"
#include "syntax/term/term.h"
#include "utils/string_interner.hpp"

#include <cstdio>
#include <memory>

int main() {
    // Guard строит синтетический AST-узел вне компиляции — явный процессный пул (E9).
    trust::ManualInternerScope manualInternerScope;
    trust::Context ctx;
    auto term = trust::Term::Create(trust::TermID::SEQUENCE, "guard");
    auto node = std::make_shared<trust::IdentName>("guard_name");
    const bool ok = term != nullptr && node != nullptr && ctx.moduleResolver() == nullptr;
    std::printf("no_module_loader_guard: %s\n", ok ? "ok" : "fail");
    return ok ? 0 : 1;
}
