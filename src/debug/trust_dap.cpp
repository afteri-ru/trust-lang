// -----------------------------------------------------------------------
// trust-dap — DAP-сервер для отладки trust-lang (точка входа)
//
// Запускается VSCode, читает JSON-RPC из stdin (interactive) или из TCP
// (server), пишет в stdout / TCP.
// -----------------------------------------------------------------------

#include "debug/dap_transport.h"
#include "debug/trust_debug.h"
#include "debug/trust_source.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

using json = nlohmann::json;

// ── DAP handler ──
class DapHandler {
  public:
    DapHandler(DapTransport &transport, const DapOptions &opts)
        : transport_(transport), opts_(opts), debug_(nullptr), running_(true) {}

    void handleInitialize(const json &req) {
        std::cerr << "[DAP-TRACE] handleInitialize: seq=" << req["seq"]
                  << ", client=" << req.value("arguments", json::object()).value("clientID", "?")
                  << "\n";
        sendDapResponse(transport_, req["seq"], json{{"command", "initialize"},
                                                      {"body",
                                                       {{"supportsConfigurationDoneRequest", true},
                                                        {"supportsSetVariable", false},
                                                        {"supportsConditionalBreakpoints", false},
                                                        {"supportsHitConditionalBreakpoints", false},
                                                        {"supportsFunctionBreakpoints", true},
                                                        {"supportsBreakpointLocationsRequest", true},
                                                        {"supportsStepIn", true},
                                                        {"supportsStepOut", true},
                                                        {"supportsDisassemblyRequest", true}}}});
    }

    void handleLaunch(const json &req) {
        const auto &args = req["arguments"];

        sourceFile_ = args.value("sourceFile", "");
        cppFile_ = args.value("cppFile", "");
        targetFile_ = args.value("targetFile", "");
        mapFile_ = args.value("mapFile", "");

        std::cerr << "[DAP-TRACE] handleLaunch: seq=" << req["seq"]
                  << ", sourceFile=" << sourceFile_
                  << ", cppFile=" << cppFile_
                  << ", targetFile=" << targetFile_
                  << ", mapFile=" << mapFile_
                  << "\n";

        if (targetFile_.empty()) {
            sendDapOutput(transport_, "stderr", "trust-dap: no target file specified\n");
            sendDapResponse(transport_, req["seq"], json{{"command", "launch"}, {"body", json::object()},
                                                          {"success", false}, {"message", "No target file specified"}});
            return;
        }

        if (!std::filesystem::exists(targetFile_)) {
            sendDapOutput(transport_, "stderr", "trust-dap: target not found: " + targetFile_ + "\n");
            sendDapResponse(transport_, req["seq"], json{{"command", "launch"}, {"body", json::object()},
                                                          {"success", false}, {"message", "Target file not found: " + targetFile_}});
            return;
        }

        debug_ = std::make_unique<TrustDebug>(TrustDebug::Config{opts_.lldbServerPath});

        auto source = trust::TrustSource::LoadFromBinary(targetFile_, mapFile_);
        debug_->SetSource(std::move(source));

        if (!debug_->CreateTarget(targetFile_)) {
            std::cerr << "Failed to create target: " << targetFile_ << "\n";
            sendDapResponse(transport_, req["seq"], json{{"command", "launch"}, {"body", json::object()},
                                                          {"success", false}, {"message", "Failed to create target"}});
            return;
        }

        sendDapOutput(transport_, "stdout", "trust-dap: target created for " + targetFile_ + "\n");

        if (!LaunchProcess()) {
            sendDapResponse(transport_, req["seq"], json{{"command", "launch"}, {"body", json::object()},
                                                          {"success", false}, {"message", "Failed to launch debuggee"}});
            return;
        }

        sendDapResponse(transport_, req["seq"], json{{"command", "launch"}, {"body", json::object()}});
    }

    void handleSetBreakpoints(const json &req) {
        const auto &args = req["arguments"];
        const auto &source = args["source"];
        std::string path = source.value("path", "");
        if (path.empty()) {
            path = source.value("name", "");
        }

        std::string trustFile = sourceFile_.empty() ? path : sourceFile_;
        if (!path.empty()) {
            trustFile = path;
        }

        std::cerr << "[DAP-TRACE] handleSetBreakpoints: seq=" << req["seq"]
                  << ", source=" << trustFile
                  << ", hasBreakpoints=" << args.contains("breakpoints")
                  << "\n";

        std::vector<json> breakpoints;
        bool hasBreakpoints = args.contains("breakpoints");

        if (hasBreakpoints && debug_) {
            for (const auto &bp : args["breakpoints"]) {
                int trustLine = bp["line"].get<int>();
                int bpId = bp.value("id", 0);
                if (bpId == 0) {
                    bpId = nextBpId_++;
                }

                lldb::SBBreakpoint sb = debug_->BreakpointBySource(trustFile, trustLine);
                bool verified = sb.IsValid() && sb.GetNumLocations() > 0;
                if (verified) {
                    sendDapOutput(transport_, "stdout", "  BP applied: " + trustFile + ":" + std::to_string(trustLine) + "\n");
                } else {
                    sendDapOutput(transport_, "stderr", "  BP failed: " + trustFile + ":" + std::to_string(trustLine) + "\n");
                }
                sendBreakpointEvent(transport_, trustFile, trustLine, bpId, verified);
                breakpoints.push_back({{"id", bpId}, {"line", trustLine}, {"verified", verified}});
            }
        } else if (hasBreakpoints) {
            for (const auto &bp : args["breakpoints"]) {
                int trustLine = bp["line"].get<int>();
                int bpId = bp.value("id", 0);
                if (bpId == 0) {
                    bpId = nextBpId_++;
                }
                breakpoints.push_back({{"id", bpId}, {"line", trustLine}, {"verified", false}});
            }
        }

        sendDapResponse(transport_, req["seq"], json{{"command", "setBreakpoints"}, {"body", {{"breakpoints", breakpoints}}}});
    }

    void handleBreakpointLocations(const json &req) {
        const auto &args = req["arguments"];
        std::string srcPath = args["source"].value("path", sourceFile_);
        if (srcPath.empty()) {
            srcPath = sourceFile_;
        }

        int startLine = args.value("line", 1);
        int endLine = args.value("endLine", startLine);

        int numLines = 0;
        std::ifstream srcStream(srcPath);
        if (srcStream.is_open()) {
            std::string _;
            while (std::getline(srcStream, _)) {
                numLines++;
            }
        }

        if (endLine < startLine && numLines > 0) {
            endLine = numLines;
        }

        std::vector<json> locations;
        for (int l = startLine; l <= endLine; ++l) {
            locations.push_back({{"line", l}});
        }

        sendDapResponse(transport_, req["seq"], json{{"command", "breakpointLocations"}, {"body", {{"breakpoints", locations}}}});
    }

    void handleSetExceptionBreakpoints(const json &req) {
        sendDapResponse(transport_, req["seq"], json{{"command", "setExceptionBreakpoints"}, {"body", {{"exceptionBreakpointFilters", json::array()}}}});
    }

    void handleThreads(const json &req) {
        std::vector<json> threads;
        if (debug_) {
            lldb::SBProcess proc = debug_->GetProcess();
            if (proc.IsValid()) {
                uint32_t numThreads = proc.GetNumThreads();
                for (uint32_t i = 0; i < numThreads; ++i) {
                    lldb::SBThread t = proc.GetThreadAtIndex(i);
                    if (t.IsValid()) {
                        threads.push_back({{"id", t.GetIndexID()}, {"name", t.GetName() ? t.GetName() : t.GetQueueName() ? t.GetQueueName() : ""}});
                    }
                }
            }
        }
        sendDapResponse(transport_, req["seq"], json{{"command", "threads"}, {"body", {{"threads", threads}}}});
    }

    void handleConfigurationDone(const json &req) {
        sendDapResponse(transport_, req["seq"], json{{"command", "configurationDone"}, {"body", json::object()}});
    }

    void handleContinue(const json &req) {
        if (debug_) {
            debug_->Continue();
        }
        sendDapResponse(transport_, req["seq"], json{{"command", "continue"}, {"body", {{"allThreadsContinued", true}}}});
    }

    void handleStepIn(const json &req) {
        if (debug_) {
            debug_->StepInto();
        }
        sendDapResponse(transport_, req["seq"], json{{"command", "stepIn"}, {"body", json::object()}});
    }

    void handleStepOut(const json &req) {
        if (debug_) {
            debug_->StepOut();
        }
        sendDapResponse(transport_, req["seq"], json{{"command", "stepOut"}, {"body", json::object()}});
    }

    void handleNext(const json &req) {
        if (debug_) {
            debug_->StepOver();
        }
        sendDapResponse(transport_, req["seq"], json{{"command", "next"}, {"body", json::object()}});
    }

    void handleStackTrace(const json &req) {
        const auto &args = req["arguments"];
        int startFrame = args.value("startFrame", 0);
        int levels = args.value("levels", 20);

        std::vector<json> stackFrames;

        if (debug_) {
            lldb::SBProcess proc = debug_->GetProcess();
            if (proc.IsValid()) {
                lldb::SBThread thread = proc.GetSelectedThread();
                if (!thread.IsValid()) {
                    thread = proc.GetThreadAtIndex(0);
                }

                if (thread.IsValid()) {
                    int numFrames = thread.GetNumFrames();
                    int endFrame = std::min(startFrame + levels, numFrames);
                    for (int i = startFrame; i < endFrame; ++i) {
                        lldb::SBFrame frame = thread.GetFrameAtIndex(i);
                        if (!frame.IsValid())
                            break;

                        lldb::SBLineEntry lineEntry = frame.GetLineEntry();
                        int cppLine = lineEntry.GetLine();
                        const char *cppFileCStr = lineEntry.GetFileSpec().GetFilename();
                        std::string cppFile = cppFileCStr ? cppFileCStr : "";

                        if (i == startFrame) {
                            lastCppFile_ = cppFile.empty() ? cppFile_ : cppFile;
                            lastCppLine_ = cppLine;
                        }

                        std::string trustFile;
                        int trustLine = cppLine;
                        if (!cppFile.empty() && debug_->GetSource()) {
                            auto trustLoc = debug_->GetSource()->nearestCppToTrust(cppFile, cppLine);
                            if (trustLoc.has_value()) {
                                trustFile = trustLoc->first;
                                trustLine = trustLoc->second;
                            }
                        }

                        if (trustFile.empty()) {
                            trustFile = sourceFile_;
                        }

                        stackFrames.push_back({{"id", i},
                                               {"name", frame.GetFunctionName() ? frame.GetFunctionName() : ""},
                                               {"source", {{"name", trustFile}, {"path", trustFile}}},
                                               {"line", trustLine},
                                               {"column", 0}});
                    }
                }
            }
        }

        sendDapResponse(transport_, req["seq"],
                        json{{"command", "stackTrace"}, {"body", {{"stackFrames", stackFrames}, {"totalFrames", static_cast<int>(stackFrames.size())}}}});
    }

    void handleScopes(const json &req) {
        sendDapResponse(transport_, req["seq"],
                        json{{"command", "scopes"}, {"body", {{"scopes", {{{"name", "Local"}, {"variablesReference", 1}, {"expensive", false}}}}}}});
    }

    void handleVariables(const json &req) {
        const auto &args = req["arguments"];
        int ref = args.value("variablesReference", 0);

        std::vector<json> vars;
        if (ref == 1 && debug_) {
            auto cppVars = debug_->GetVariablesBySource();
            for (const auto &v : cppVars) {
                lldb::SBValue val = debug_->GetValueBySource(v);
                std::string valueStr;
                if (val.IsValid()) {
                    const char *s = val.GetValue();
                    valueStr = s ? s : "(null)";
                } else {
                    valueStr = "(null)";
                }
                vars.push_back({{"name", v}, {"value", valueStr}, {"variablesReference", 0}});
            }
        }

        sendDapResponse(transport_, req["seq"], json{{"command", "variables"}, {"body", {{"variables", vars}}}});
    }

    void handleDisconnect(const json &req) {
        running_ = false;
        if (eventThread_.joinable()) {
            eventThread_.join();
        }
        sendDapResponse(transport_, req["seq"], json{{"command", "disconnect"}, {"body", json::object()}});
    }

    void handleRequest(const json &req) {
        std::string command = req.value("command", "");
        std::string reqType = req.value("type", "");
        if (command != "continue" && command != "stackTrace") {
            std::cerr << "[DAP-TRACE] handleRequest: type=" << reqType
                      << ", command=" << command
                      << ", seq=" << req["seq"]
                      << "\n";
        }

        if (command == "initialize") {
            handleInitialize(req);
        } else if (command == "launch") {
            handleLaunch(req);
        } else if (command == "configurationDone") {
            handleConfigurationDone(req);
        } else if (command == "setBreakpoints") {
            handleSetBreakpoints(req);
        } else if (command == "breakpointLocations") {
            handleBreakpointLocations(req);
        } else if (command == "continue") {
            handleContinue(req);
        } else if (command == "next") {
            handleNext(req);
        } else if (command == "stepIn") {
            handleStepIn(req);
        } else if (command == "stepOut") {
            handleStepOut(req);
        } else if (command == "stackTrace") {
            handleStackTrace(req);
        } else if (command == "scopes") {
            handleScopes(req);
        } else if (command == "variables") {
            handleVariables(req);
        } else if (command == "setExceptionBreakpoints") {
            handleSetExceptionBreakpoints(req);
        } else if (command == "threads") {
            handleThreads(req);
        } else if (command == "disconnect") {
            handleDisconnect(req);
        } else {
            sendDapResponse(transport_, req["seq"], json{{"command", command}, {"message", "unsupported command: " + command}}, false);
        }
    }

    bool isRunning() const { return running_; }

  private:
    bool LaunchProcess() {
        if (!debug_) {
            sendDapOutput(transport_, "stderr", "trust-dap: no debug target, cannot launch\n");
            return false;
        }
        lldb::SBProcess proc = debug_->Launch(targetFile_);
        if (!proc.IsValid()) {
            std::cerr << "Failed to launch: " << targetFile_ << "\n";
            return false;
        }

        sendDapOutput(transport_, "stdout", "trust-dap: launched " + targetFile_ + "\n");
        eventThread_ = std::thread(&DapHandler::pollEvents, this);
        return true;
    }

    void pollEvents() {
        while (running_) {
            TrustDebug::Event evt = debug_->WaitForEvent(500);

            if (evt == TrustDebug::Event::Stop) {
                lldb::SBThread thread = debug_->GetThread();
                int threadId = 0;
                if (thread.IsValid()) {
                    threadId = thread.GetIndexID();
                }

                std::string stopReason = "breakpoint";
                if (thread.IsValid()) {
                    switch (thread.GetStopReason()) {
                    case lldb::eStopReasonBreakpoint:
                        stopReason = "breakpoint";
                        break;
                    case lldb::eStopReasonPlanComplete:
                        stopReason = "step";
                        break;
                    case lldb::eStopReasonException:
                        stopReason = "exception";
                        break;
                    default:
                        stopReason = "unknown";
                        break;
                    }
                }

                sendDapEvent(transport_, "stopped", {{"reason", stopReason}, {"threadId", threadId}, {"allThreadsStopped", true}});
            } else if (evt == TrustDebug::Event::Exit) {
                sendDapEvent(transport_, "exited", {{"exitCode", 0}});
                sendDapEvent(transport_, "terminated", json::object());
                running_ = false;
            }
        }
    }

    DapTransport &transport_;
    DapOptions opts_;

    std::string sourceFile_;
    std::string cppFile_;
    std::string targetFile_;
    std::string mapFile_;

    std::unique_ptr<TrustDebug> debug_;
    std::thread eventThread_;
    std::atomic<bool> running_;
    int nextBpId_ = 1;
    std::string lastCppFile_;
    int lastCppLine_ = -1;
};

// ── Main ──
int main(int argc, const char *argv[]) {
    DapOptions opts = parseDapOptions(argc, argv);

    if (opts.help) {
        printUsage(argv[0]);
        return 0;
    }

    // ═══ Server mode ═══
    if (opts.port > 0) {
        int serverFd = createTcpServer(opts.port);
        if (serverFd < 0) {
            return 1;
        }

        int clientFd = acceptConnection(serverFd);
        ::close(serverFd);

        if (clientFd < 0) {
            return 1;
        }

        auto transport = std::make_unique<TcpTransport>(clientFd);
        DapHandler handler(*transport, opts);

        while (handler.isRunning()) {
            json req = readDapPacket(*transport);
            if (req.is_null() || req.empty()) {
                break;
            }
            if (req.value("type", "") != "request") {
                continue;
            }
            handler.handleRequest(req);
        }

        return 0;
    }

    // ═══ Interactive mode (stdin/stdout) ═══
    std::cerr << "trust-dap: starting in interactive mode\n"
              << "  project-dir: " << (opts.projectDir.empty() ? "(cwd)" : opts.projectDir) << "\n";

    auto transport = std::make_unique<StdioTransport>();
    DapHandler handler(*transport, opts);

    while (handler.isRunning()) {
        json req = readDapPacket(*transport);
        if (req.is_null() || req.empty()) {
            break;
        }
        if (req.value("type", "") != "request") {
            continue;
        }
        handler.handleRequest(req);
    }

    std::cerr << "trust-dap: exiting\n";
    return 0;
}