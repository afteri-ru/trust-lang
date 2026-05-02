#include "debug/trust_debug.h"
#include "debug/trust_source.h"

#include <cassert>
#include <cstdlib>
#include <iostream>
#include <string>

// ---------------------------------------------------------------------------
// Main — тестирование TrustDebug без DAP (1:1 режим)
// ---------------------------------------------------------------------------
static const char *sep_title = nullptr;
void sep(const char *title) {
    sep_title = title;
    std::cout << "\n===== " << title << " =====\n";
}

int main(int argc, const char *argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <binary_path>\n"
                  << "  <binary_path>  Path to the compiled lldb_debuggee binary\n";
        return 1;
    }
    std::string exe = argv[1];

    sep("Initialize");
    TrustDebug::Config cfg;
    cfg.lldbServerPath = "/usr/bin/lldb-server";
    // cfg.sourceMapper = nullptr; // 1:1 режим по умолчанию
    TrustDebug debug(cfg);

    sep("Target");
    assert(debug.CreateTarget(exe));
    std::cout << "  " << exe << "\n";

    sep("Breakpoints");
    auto bp1 = debug.BreakpointCreateByName("factorial");
    std::cout << "  BP#'factorial': " << bp1.GetNumLocations() << " locs\n";

    sep("Launch");
    debug.Launch(exe);

    sep("Event loop");
    int stop_count = 0;
    bool stepping = false;     // true = мы сделали step over и ждём завершения
    int max_iterations = 1000; // защита от зависания

    while (max_iterations-- > 0) {
        // Ждём событие с таймаутом 200ms
        TrustDebug::Event evt = debug.WaitForEvent(200);

        if (evt == TrustDebug::Event::Exit) {
            auto proc = debug.GetProcess();
            std::cout << "\n  Process exited with code: " << proc.GetExitStatus() << "\n";
            break;
        }

        if (evt == TrustDebug::Event::Timeout) {
            auto state = debug.GetProcess().GetState();
            if (state == lldb::eStateExited) {
                std::cout << "\n  Process exited (detected by poll).\n";
                break;
            }
            if (state == lldb::eStateRunning)
                continue;
            std::cout << "  Unexpected state (timeout): " << state << "\n";
            break;
        }

        // evt == Stop
        ++stop_count;
        auto thr = debug.GetThread();
        lldb::StopReason reason = thr.GetStopReason();
        std::cout << "\n--- Stop #" << stop_count << " (reason=" << reason << ") ---\n";

        // Flush stdout
        std::string out = debug.ReadStdout();
        if (!out.empty()) {
            std::cout << out;
        }

        if (stepping) {
            sep("Continue");
            debug.Continue();
            stepping = false;
        } else {
            sep("Variables");
            auto src_vars = debug.GetVariablesBySource();
            std::cout << "  Vars:";
            for (const auto &v : src_vars) {
                std::cout << " " << v;
                auto val = debug.GetValueBySource(v);
                if (val.IsValid()) {
                    std::cout << "=" << val.GetValue();
                }
            }
            std::cout << "\n";
            sep("Step over");
            debug.StepOver();
            stepping = true;
        }
    }

    if (max_iterations <= 0) {
        std::cerr << "\n  ERROR: Event loop exceeded iteration limit (process hung)\n";
        return 1;
    }

    sep("Cleanup");
    std::cout << "  Total stops: " << stop_count << "\n";
    std::cout << "Done.\n";
    return 0;
}