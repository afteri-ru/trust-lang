#include "diag/context.hpp"
#include "diag/diag.hpp"
#include "diag/options.hpp"

#include <iostream>
#include <memory>

using namespace trust;

int main(int argc, char* argv[]) {
    std::cout << "=== Diag Demo ===\n";

    Context ctx;
    SourceIdx src = ctx.add_source("demo.cpp",
        "int main() {\n"
        "    int x = foo();\n"
        "    return 0;\n"
        "}\n"
    );
    ctx.diag().setMinSeverity(Severity::Remark);
    ctx.diag().setOutput(&std::cout);

    auto line_start = ctx.loc_from_line(src, 2);
    int foo_offset = line_start.offset() + 12;
    auto foo_begin = SourceLoc::make(src, foo_offset);
    auto foo_end = SourceLoc::make(src, foo_offset + 3);
    ctx.diag().report({foo_begin, foo_end}, Severity::Warning, "unused variable '{}'", "foo");
    ctx.diag().report({foo_begin, foo_end}, Severity::Error, "unexpected token '{}'", "foo");
    ctx.diag().report(foo_begin, Severity::Note, "did you mean '{}'?", "bar");

    auto invalid_loc = SourceLoc::invalid();
    ctx.diag().report(invalid_loc, Severity::Fatal, "internal error");

    std::cout << "Errors: " << ctx.diag().errorCount()
              << ", Warnings: " << ctx.diag().warningCount() << "\n";

    std::cout << "\n=== Options Demo ===\n";

    ctx.opts().add_option(OptKind::UnusedVar);
    ctx.opts().add_option(OptKind::Deprecated, Severity::Error);
    ctx.opts().add_option(OptKind::All);

    std::cout << "After parsing:\n";
    for (const char* name : {"unused-var", "deprecated", "all"}) {
        auto sev = ctx.opts().get(name);
        std::cout << "  " << name << ": ";
        if (sev.has_value()) {
            switch (*sev) {
                case Severity::Fatal:   std::cout << "fatal"; break;
                case Severity::Error:   std::cout << "error"; break;
                case Severity::Warning: std::cout << "warning"; break;
                case Severity::Note:    std::cout << "note"; break;
                case Severity::Remark:  std::cout << "remark"; break;
            }
        } else {
            std::cout << "ignore";
        }
        std::cout << "\n";
    }

    std::span<char*> args_full(argv, argc);
    auto remaining = ctx.opts().parse_argv(args_full.subspan(1));

    std::cout << "Positional args:";
    for (const char* a : remaining) std::cout << " " << a;
    std::cout << '\n';

    std::cout << "\n=== Push/Pop Demo ===\n";
    ctx.opts().push();
    ctx.opts().set(OptKind::UnusedVar, Severity::Fatal);
    std::cout << "  unused-var: " << (ctx.opts().severity(OptKind::UnusedVar) == Severity::Fatal ? "fatal" : "other") << '\n';

    ctx.opts().pop();
    std::cout << "  unused-var after pop: "
               << (ctx.opts().severity(OptKind::UnusedVar) == Severity::Fatal ? "fatal" : "restored") << '\n';

    std::cout << "\n=== Combined: Options with Diag ===\n";
    std::cout << "Trying to set unknown option (triggers diag error):\n";
    try {
        ctx.opts().set("unknown-option", Severity::Error);
    } catch (const std::invalid_argument&) {
        std::cout << "  Exception caught (diag is printed above)\n";
    }

    return 0;
}