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
    std::cout << "\n===== " << title << " =====\n";
}

void printUsage(const char* prog) {
    std::cerr << "Usage: " << prog << " <path_to_gdb_debuggee_binary>\n";
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
    std::cout << "  GdbDebug created\n";

    // ------------------------------------------------------------------
    sep("Create target");
    std::cout << "  Loading binary: " << exe << std::endl;
    bool ok = debug.CreateTarget(exe);
    std::cout << "  CreateTarget: " << (ok ? "OK" : "FAIL") << std::endl;
    if (!ok) {
        std::cerr << "  FAIL: CreateTarget returned false\n";
        return 1;
    }
    std::cout << "  Target created\n";

    // ------------------------------------------------------------------
    sep("Breakpoint at factorial");
    int bp = debug.BreakpointCreateByName("factorial");
    std::cout << "  Breakpoint: " << (bp >= 0 ? "OK" : "FAIL") << " (token=" << bp << ")\n";
    if (bp < 0) {
        std::cerr << "  FAIL: BreakpointCreateByName returned " << bp << "\n";
        return 1;
    }

    // ------------------------------------------------------------------
    sep("Launch");
    ok = debug.Launch();
    std::cout << "  Launch: " << (ok ? "OK" : "FAIL") << std::endl;
    if (!ok) {
        std::cerr << "  FAIL: Launch failed\n";
        return 1;
    }
    std::cout << "  Hit breakpoint at factorial!\n";

    // ------------------------------------------------------------------
    sep("Step over and read variables");
    for (int i = 0; i < 3; ++i) {
        std::cout << "\n--- Step #" << (i + 1) << " ---\n";

        // Read variables before stepping
        auto vars = debug.GetVariables();
        std::cout << "  Vars:";
        for (const auto& v : vars) {
            std::string val = debug.EvaluateExpression(v);
            std::cout << " " << v << "=" << val;
        }
        std::cout << "\n";

        // Step over (blocks until stop)
        debug.StepOver();
        std::cout << "  Step over completed\n";
    }

    // ------------------------------------------------------------------
    sep("Continue to exit");
    debug.Continue();
    GdbDebug::Event evt = debug.WaitForEvent(5000);
    std::cout << "  Event after continue: ";
    if (evt == GdbDebug::Event::Exit)
        std::cout << "Exit";
    else if (evt == GdbDebug::Event::Stop)
        std::cout << "Stop";
    else if (evt == GdbDebug::Event::Timeout)
        std::cout << "Timeout";
    std::cout << "\n";

    // ------------------------------------------------------------------
    sep("Cleanup");
    std::cout << "  Done. GDB test passed.\n";
    return 0;
}