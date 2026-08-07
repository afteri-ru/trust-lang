#include "utils/io.hpp"
// Integration test for GDB via GdbDebug wrapper
//
// Usage: ./gdb_main <path_to_gdb_debuggee_binary>

#include "debug/gdb_debug.h"

#include <chrono>
#include <iostream>
#include <string>
#include <thread>

// ---------------------------------------------------------------------------
// Main test routine
// ---------------------------------------------------------------------------
static const char* sep_title = nullptr;
void sep(const char* title) {
    sep_title = title;
    trust::outs() << "\n===== " << title << " =====\n";
}

void printUsage(const char* prog) {
    trust::errs() << "Usage: " << prog << " <path_to_gdb_debuggee_binary>\n";
}

int main(int argc, const char* argv[]) {
    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }

    std::string exe = argv[1];

    // ------------------------------------------------------------------
    sep("Initialize");
    GdbDebug debug;
    trust::outs() << "  GdbDebug created\n";

    // ------------------------------------------------------------------
    sep("Create target");
    trust::outs() << "  Loading binary: " << exe << std::endl;
    bool ok = debug.CreateTarget(exe);
    trust::outs() << "  CreateTarget: " << (ok ? "OK" : "FAIL") << std::endl;
    if (!ok) {
        trust::errs() << "  FAIL: CreateTarget returned false\n";
        return 1;
    }
    trust::outs() << "  Target created\n";

    // ------------------------------------------------------------------
    sep("Breakpoint at factorial");
    int bp = debug.BreakpointCreateByName("factorial");
    trust::outs() << "  Breakpoint: " << (bp >= 0 ? "OK" : "FAIL") << " (token=" << bp << ")\n";
    if (bp < 0) {
        trust::errs() << "  FAIL: BreakpointCreateByName returned " << bp << "\n";
        return 1;
    }

    // ------------------------------------------------------------------
    sep("Launch");
    ok = debug.Launch();
    trust::outs() << "  Launch: " << (ok ? "OK" : "FAIL") << std::endl;
    if (!ok) {
        trust::errs() << "  FAIL: Launch failed\n";
        return 1;
    }
    trust::outs() << "  Hit breakpoint at factorial!\n";

    // ------------------------------------------------------------------
    sep("Step over and read variables");
    for (int i = 0; i < 3; ++i) {
        trust::outs() << "\n--- Step #" << (i + 1) << " ---\n";

        // Read variables before stepping
        auto vars = debug.GetVariables();
        trust::outs() << "  Vars:";
        for (const auto& v : vars) {
            std::string val = debug.EvaluateExpression(v);
            trust::outs() << " " << v << "=" << val;
        }
        trust::outs() << "\n";

        // Step over (blocks until stop)
        debug.StepOver();
        trust::outs() << "  Step over completed\n";
    }

    // ------------------------------------------------------------------
    sep("Continue to exit");
    debug.Continue();
    GdbDebug::Event evt = debug.WaitForEvent(5000);
    trust::outs() << "  Event after continue: ";
    if (evt == GdbDebug::Event::Exit) {
        trust::outs() << "Exit";
    } else if (evt == GdbDebug::Event::Stop) {
        trust::outs() << "Stop";
    } else if (evt == GdbDebug::Event::Timeout) {
        trust::outs() << "Timeout";
    }
    trust::outs() << "\n";

    // ------------------------------------------------------------------
    sep("Cleanup");
    trust::outs() << "  Done. GDB test passed.\n";
    return 0;
}