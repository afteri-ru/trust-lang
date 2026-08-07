#pragma once

#include "debug/dap_transport.h"
#include "debug/gdb_debug.h"
#include "location/location.hpp"
#include "diag/mapper.hpp"

#include <nlohmann/json.hpp>

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using json = nlohmann::json;

// ── DAP handler ──
class DapHandler {
  public:
    DapHandler(trust::transport::Transport& transport, const DapOptions& opts);

    void handleInitialize(const json& req);
    void handleLaunch(const json& req);
    void handleSetBreakpoints(const json& req);
    void handleBreakpointLocations(const json& req);
    void handleSetExceptionBreakpoints(const json& req);
    void handleThreads(const json& req);
    void handleConfigurationDone(const json& req);
    void handleContinue(const json& req);
    void handleStepIn(const json& req);
    void handleStepOut(const json& req);
    void handleNext(const json& req);
    void handleStackTrace(const json& req);
    void handleScopes(const json& req);
    void handleVariables(const json& req);
    void handleDisconnect(const json& req);
    void handleRequest(const json& req);

    bool isRunning() const { return m_running; }
    bool hasLaunched() const { return m_launched; }

  private:
    bool launchProcess();
    void pollEvents();

    trust::transport::Transport& m_transport;
    DapOptions m_opts;

    std::string m_source_file;
    std::string m_cpp_file;
    std::string m_target_file;

    std::unique_ptr<GdbDebug> m_debug;
    std::unique_ptr<const trust::SourceMapReader> m_source_reader;
    std::thread m_event_thread;
    std::atomic<bool> m_running{true};
    std::atomic<bool> m_launched{false};
    int m_next_bp_id = 1;
};