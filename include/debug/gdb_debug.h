#pragma once

#include <string>
#include <vector>
#include <memory>

// ---------------------------------------------------------------------------
// GdbDebug — lightweight wrapper around GDB/MI subprocess
// Does NOT depend on LLDB, communicates with GDB via pipes (GDB/MI2 protocol)
// ---------------------------------------------------------------------------
class GdbDebug {
  public:
    struct Config {
        std::string gdbPath = "gdb"; // path to gdb binary
    };

    /// Represents a single stack frame returned by GDB/MI -stack-list-frames.
    struct StackFrameInfo {
        std::string m_func;
        std::string m_file;
        int m_line = 0;
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
    /// Returns up to maxCount frames starting from startFrame.
    std::vector<StackFrameInfo> getStackFrames(int startFrame = 0, int maxCount = 20);

    /// Returns the stop reason from the last *stopped async notification.
    /// Maps GDB/MI reasons to DAP: "breakpoint-hit" → "breakpoint",
    /// "end-stepping-range" → "step", "signal-received" → "exception",
    /// "exited"/"exited-normally" → "exit".
    std::string getLastStopReason() const;

    /// Returns thread-id from the last stop event (always 1 for single-threaded).
    int getLastThreadId() const;

    /// Returns current thread-id (always 1 via GDB/MI).
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
