#pragma once

#include <lldb/API/SBBreakpoint.h>
#include <lldb/API/SBBreakpointLocation.h>
#include <lldb/API/SBStream.h>
#include <lldb/API/SBDebugger.h>
#include <lldb/API/SBError.h>
#include <lldb/API/SBEvent.h>
#include <lldb/API/SBFrame.h>
#include <lldb/API/SBLaunchInfo.h>
#include <lldb/API/SBListener.h>
#include <lldb/API/SBProcess.h>
#include <lldb/API/SBTarget.h>
#include <lldb/API/SBThread.h>
#include <lldb/API/SBValue.h>

#include <string>
#include <vector>
#include <memory>

namespace trust { class TrustSource; } // forward decl

class TrustDebug {
  public:
    struct Config {
        std::string lldbServerPath = ""; // empty = use system default
        lldb::LaunchFlags launchFlags = lldb::eLaunchFlagNone;
    };

    TrustDebug();
    explicit TrustDebug(const Config &cfg);
    ~TrustDebug();

    // --- Target ---
    bool CreateTarget(const std::string &exe);

    // --- Breakpoints ---
    lldb::SBBreakpoint BreakpointCreateByName(const std::string &name);
    lldb::SBBreakpoint BreakpointBySource(const std::string &file, int line);

    // --- Launch ---
    lldb::SBProcess Launch(const std::string &exe);

    // --- Execution control ---
    enum class Event { Stop, Exit, Timeout };
    Event WaitForEvent(int timeoutMs = 1000);
    void StepOver();
    void StepInto();
    void StepOut();
    void Continue();
    std::string ReadStdout();

    // --- State access ---
    lldb::SBThread GetThread() const;
    lldb::SBFrame GetFrame() const;
    lldb::SBProcess GetProcess() const;
    lldb::SBTarget GetTarget() const;

    // --- Source mapping management ---
    void SetSource(std::unique_ptr<const trust::TrustSource> source);
    const trust::TrustSource *GetSource() const;

    // --- Source-mapped variable access (1:1 if source_==nullptr) ---
    std::vector<std::string> GetVariablesBySource();
    lldb::SBValue GetValueBySource(const std::string &varName);
    bool SetValueBySource(const std::string &varName, const std::string &value);

    // --- Config access ---
    const Config &GetConfig() const { return cfg_; }

  private:
    Config cfg_;
    std::unique_ptr<const trust::TrustSource> source_;
    lldb::SBDebugger dbg_;
    lldb::SBTarget tgt_;
    lldb::SBProcess proc_;
    lldb::SBListener listener_;
};
