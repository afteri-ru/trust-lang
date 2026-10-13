#pragma once

#include "debug/gdb/stack_frame.hpp"

#include <memory>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// GdbDebug - lightweight wrapper around GDB/MI subprocess
// Does NOT depend on LLDB, communicates with GDB via pipes (GDB/MI2 protocol)
// ---------------------------------------------------------------------------
class GdbDebug {
  public:
    struct Config {
        std::string gdbPath = "gdb"; // path to gdb binary
    };

    GdbDebug();
    explicit GdbDebug(const Config& cfg);
    ~GdbDebug();

    // --- Target ---
    bool CreateTarget(const std::string& exe);

    // --- Breakpoints ---
    int BreakpointCreateByName(const std::string& name);
    int BreakpointBySource(const std::string& file, int line);

    // --- Launch ---
    bool Launch();

    // --- Execution control ---
    enum class Event { Stop, Exit, Timeout, Error };
    Event WaitForEvent(int timeoutMs = 1000);
    void StepOver();
    void StepInto();
    void StepOut();
    void Continue();

    // --- Stack / thread ---
    std::vector<trust::debug::StackFrameInfo> getStackFrames(int startFrame = 0, int maxCount = 20);

    /// Returns the stop reason from the last *stopped async notification.
    /// Maps GDB/MI reasons to DAP: "breakpoint-hit" → "breakpoint",
    /// "end-stepping-range" → "step", "signal-received" → "exception",
    /// "exited"/"exited-normally" → "exit".
    std::string getLastStopReason() const;

    /// Always 1 (single-threaded GDB/MI).
    int getLastThreadId() const;

    /// Always 1 (GDB/MI).
    int getCurrentThreadId() const;

    // --- Variable access ---
    std::string EvaluateExpression(const std::string& expr);
    std::vector<std::string> GetVariables();

    // --- State ---
    bool IsRunning() const;
    std::string ReadStdout();

    // --- Config access ---
    const Config& GetConfig() const { return m_cfg; }

  private:
    Config m_cfg;
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
