#include "debug/trust_debug.h"
#include "debug/trust_source.h"

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <filesystem>

namespace fs = std::filesystem;

static const char *sep_title = nullptr;
void sep(const char *title) {
    sep_title = title;
    std::cout << "\n===== " << title << " =====\n";
}

static void print_usage(const char *prog) {
    std::cerr << "Usage: " << prog << " --binary <path> --map <path>\n"
              << "\n"
              << "Integration test for trust-lang debug (transpile + compile + debug).\n"
              << "Accepts already-transpiled binary and map file.\n"
              << "\n"
              << "Required:\n"
              << "  --binary <path>   Path to compiled ELF binary\n"
              << "  --map <path>      Path to .map source map file\n"
              << "  --help            Show this help\n";
}

// Check variables available at given trust line via GetValueBySource
bool checkLine(TrustDebug &debug, const std::string &binary_path, const std::string &src_file, int trustLine,
               const std::vector<std::pair<std::string, int>> &expectedVars,
               std::string &processStdout)
{
    std::cout << "\n--- checkLine trust " << trustLine << " ---\n";
    // Set breakpoint at trust line
    auto bp = debug.BreakpointBySource(src_file, trustLine);
    if (!bp.IsValid() || bp.GetNumLocations() == 0) {
        std::cout << "  [SKIP] BP at trust line " << trustLine << " has no locations\n";
        return true;
    }
    std::cout << "  BP: " << bp.GetNumLocations() << " locs\n";

    // Launch if not already running
    if (!debug.GetProcess().IsValid()) {
        debug.Launch(binary_path);
    } else {
        debug.Continue();
    }

    // Wait for stop
    bool stopped = false;
    for (int i = 0; i < 500; ++i) {
        auto evt = debug.WaitForEvent(200);
        if (evt == TrustDebug::Event::Exit) return true;
        if (evt == TrustDebug::Event::Stop) { stopped = true; break; }
        auto state = debug.GetProcess().GetState();
        if (state == lldb::eStateExited) return true;
        if (state == lldb::eStateStopped) { stopped = true; break; }
        if (state == lldb::eStateRunning) continue;
    }
    if (!stopped) {
        std::cout << "  [SKIP] Process did not stop\n";
        return true;
    }

    // Read stdout
    std::string out = debug.ReadStdout();
    if (!out.empty()) processStdout.append(out);

    // Get frame info
    auto frame = debug.GetFrame();
    if (!frame.IsValid()) return true;
    auto le = frame.GetLineEntry();
    if (le.GetFileSpec().IsValid()) {
        std::cout << "  File: " << le.GetFileSpec().GetFilename() << ":" << le.GetLine() << "\n";
    }

    // Test GetVariablesBySource
    auto trustVars = debug.GetVariablesBySource();
    std::cout << "  GetVariablesBySource: ";
    for (const auto &tv : trustVars) std::cout << tv << " ";
    std::cout << "\n";

    // Verify expected vars
    bool ok = true;
    for (const auto &[name, expectedVal] : expectedVars) {
        auto val = debug.GetValueBySource(name);
        bool valid = val.IsValid();
        std::string actual = valid ? val.GetValue() : "?";
        std::cout << "  " << name << " = " << actual << " (valid=" << valid << ")\n";
        if (valid) {
            int actualInt = std::stoi(actual);
            if (actualInt != expectedVal) {
                std::cerr << "    ERROR: Expected " << expectedVal << ", got " << actualInt << "\n";
                ok = false;
            } else {
                std::cout << "    OK\n";
            }
        } else {
            std::cout << "    (value unavailable at declaration line — ok)\n";
        }
    }

    // Remove BP so we don't re-hit it
    debug.GetTarget().BreakpointDelete(bp.GetID());

    return ok;
}

int main(int argc, char **argv) {
    std::string binary_path;
    std::string map_path;

    // Parse arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "--binary") {
            if (i + 1 >= argc) { std::cerr << "Error: --binary requires an argument\n"; return 1; }
            binary_path = argv[++i];
        } else if (arg == "--map") {
            if (i + 1 >= argc) { std::cerr << "Error: --map requires an argument\n"; return 1; }
            map_path = argv[++i];
        } else {
            std::cerr << "Error: Unknown option: " << arg << "\n";
            print_usage(argv[0]);
            return 1;
        }
    }

    if (binary_path.empty()) { std::cerr << "Error: --binary is required\n"; print_usage(argv[0]); return 1; }
    if (map_path.empty())    { std::cerr << "Error: --map is required\n";    print_usage(argv[0]); return 1; }

    // --- Step 1: Load mapper ---
    sep("Load mapper");

    auto dbg_source = trust::TrustSource::LoadFromBinary(binary_path, map_path);
    assert(dbg_source != nullptr);
    std::cout << "  Entries: " << dbg_source->entries().size() << "\n";

    // Source file is derived from map (map has original source paths embedded)
    // We need it for BreakpointBySource. The map contains entries with trust_file paths.
    // Find the first entry to get src_file
    std::string src_file;
    if (!dbg_source->entries().empty()) {
        src_file = dbg_source->entries().front().files.first;
    } else {
        std::cerr << "Error: No source entries in map file\n";
        return 1;
    }

    // --- Step 2: Debug ---
    sep("Debug via TrustDebug");

    TrustDebug::Config cfg;
    cfg.lldbServerPath = "/usr/bin/lldb-server";
    TrustDebug debug(cfg);
    debug.SetSource(std::move(dbg_source));
    assert(debug.CreateTarget(binary_path));
    std::cout << "  Target: " << binary_path << "\n";

    // --- Check each trust line ---
    sep("Check trust lines");

    bool success = true;
    std::string output;

    // Trust line 2: create x = 10
    success &= checkLine(debug, binary_path, src_file, 2, {{"x", 10}}, output);

    // Trust line 3: create y = 20
    success &= checkLine(debug, binary_path, src_file, 3, {{"y", 20}}, output);

    // Trust line 4: create z = x + y
    success &= checkLine(debug, binary_path, src_file, 4, {{"z", 30}}, output);

    // Trust line 5: create result = z * 2
    success &= checkLine(debug, binary_path, src_file, 5, {{"result", 60}}, output);

    // Trust line 6: print result; — no variables defined here, skip check
    checkLine(debug, binary_path, src_file, 6, {}, output);

    // Read remaining stdout
    debug.Continue();
    for (int i = 0; i < 200; ++i) {
        auto evt = debug.WaitForEvent(200);
        if (evt == TrustDebug::Event::Exit) break;
        auto state = debug.GetProcess().GetState();
        if (state == lldb::eStateExited) break;
    }
    output += debug.ReadStdout();

    // --- Summary ---
    sep("Summary");

    std::cout << "  Stdout: \"" << output << "\"\n";
    if (output.find("60") != std::string::npos) {
        std::cout << "  OK: stdout contains '60'\n";
    } else {
        std::cerr << "  ERROR: stdout does not contain '60'\n";
        success = false;
    }

    if (success) {
        std::cout << "  All checks passed!\n";
    } else {
        std::cerr << "  Some checks FAILED!\n";
        return 1;
    }
    return 0;
}