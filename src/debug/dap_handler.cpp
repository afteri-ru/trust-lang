// -----------------------------------------------------------------------
// dap_handler.cpp — реализация DapHandler (DAP-логика)
// -----------------------------------------------------------------------

#include "debug/dap_handler.hpp"
#include "debug/gdb_debug.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <system_error>

using json = nlohmann::json;

// ── DapHandler ──

DapHandler::DapHandler(trust::transport::Transport& transport, const DapOptions& opts)
: m_transport(transport)
, m_opts(opts)
, m_debug(nullptr)
, m_running(true) {
}

// ── helpers are now in SourceMapReader: findFile, isTrustFileExt, calcCppToTrustLine ──

// ── DAP command handlers ──

void DapHandler::handleInitialize(const json& req) {
    std::cerr << "[DAP-TRACE] handleInitialize: seq=" << req["seq"] << ", client=" << req.value("arguments", json::object()).value("clientID", "?") << "\n";
    sendDapResponse(m_transport, req["seq"],
                    json{{"command", "initialize"},
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

void DapHandler::handleLaunch(const json& req) {
    const auto& args = req["arguments"];

    m_source_file = args.value("sourceFile", "");
    m_cpp_file = args.value("cppFile", "");
    m_target_file = args.value("targetFile", "");

    std::cerr << "[DAP-TRACE] handleLaunch: seq=" << req["seq"] << ", sourceFile=" << m_source_file << ", cppFile=" << m_cpp_file
              << ", targetFile=" << m_target_file << "\n";

    if (m_target_file.empty()) {
        sendDapOutput(m_transport, "stderr", "trust-dap: no target file specified\n");
        sendDapResponse(m_transport, req["seq"], json{{"command", "launch"}, {"body", json::object()}, {"message", "No target file specified"}}, false);
        return;
    }

    if (!std::filesystem::exists(m_target_file)) {
        sendDapOutput(m_transport, "stderr", "trust-dap: target not found: " + m_target_file + "\n");
        sendDapResponse(m_transport, req["seq"], json{{"command", "launch"}, {"body", json::object()}, {"message", "Target file not found: " + m_target_file}},
                        false);
        return;
    }

    // Load embedded source map from ELF .debug_trust_map section
    m_source_reader = trust::SourceMapReader::fromElf(m_target_file);
    if (!m_source_reader) {
        std::cerr << "No embedded source map found in: " << m_target_file << " (debugging without mapping)\n";
    } else {
        std::cerr << "Loaded embedded source map from: " << m_target_file << "\n";
    }

    // Use gdbPath from launch arguments (passed from VSCode config), fallback to CLI option or default
    std::string gdb_path = args.value("gdbPath", "");
    if (gdb_path.empty()) {
        gdb_path = m_opts.gdbPath.empty() ? "gdb" : m_opts.gdbPath;
    }
    std::cerr << "[DAP-TRACE] Using gdb: " << gdb_path << "\n";
    m_debug = std::make_unique<GdbDebug>(GdbDebug::Config{gdb_path});

    if (!m_debug->CreateTarget(m_target_file)) {
        std::cerr << "Failed to create target: " << m_target_file << "\n";
        sendDapResponse(m_transport, req["seq"],
                        json{{"command", "launch"}, {"body", json::object()}, {"success", false}, {"message", "Failed to create target"}});
        return;
    }

    sendDapOutput(m_transport, "stdout", "trust-dap: target created for " + m_target_file + "\n");

    // Don't launch process yet — wait for configurationDone so breakpoints can be set first.
    sendDapResponse(m_transport, req["seq"], json{{"command", "launch"}, {"body", json::object()}});
}

void DapHandler::handleSetBreakpoints(const json& req) {
    const auto& args = req["arguments"];
    const auto& source = args["source"];
    std::string path = source.value("path", "");
    if (path.empty()) {
        path = source.value("name", "");
    }

    bool is_trust_file = trust::SourceMapReader::isTrustFileExt(path);

    std::vector<json> breakpoints;
    bool has_breakpoints = args.contains("breakpoints");

    if (has_breakpoints && m_debug) {
        for (const auto& bp : args["breakpoints"]) {
            int line = bp["line"].get<int>();
            int bp_id = bp.value("id", 0);
            if (bp_id == 0) {
                bp_id = m_next_bp_id++;
            }

            bool verified = false;

            if (is_trust_file && m_source_reader) {
                auto mapping = m_source_reader->findTrustToCpp(path, line);
                if (mapping.has_value()) {
                    std::string cpp_file(m_source_reader->filename(mapping->begin.fileIdx()));
                    int cpp_line = static_cast<int>(m_source_reader->line(mapping->begin));

                    int bp_num = m_debug->BreakpointBySource(cpp_file, cpp_line);
                    verified = (bp_num != -1);

                    if (verified) {
                        sendDapOutput(m_transport, "stdout",
                                      "  BP applied: " + path + ":" + std::to_string(line) + " -> " + cpp_file + ":" + std::to_string(cpp_line) + "\n");
                    } else {
                        sendDapOutput(m_transport, "stderr", "  BP failed: " + path + ":" + std::to_string(line) + "\n");
                    }
                } else {
                    sendDapOutput(m_transport, "stderr", "  BP failed (no mapping): " + path + ":" + std::to_string(line) + "\n");
                }
            } else {
                int bp_num = m_debug->BreakpointBySource(path, line);
                verified = (bp_num != -1);
            }

            sendBreakpointEvent(m_transport, path, line, bp_id, verified);
            breakpoints.push_back({{"id", bp_id}, {"line", line}, {"verified", verified}});
        }
    } else if (has_breakpoints) {
        for (const auto& bp : args["breakpoints"]) {
            int line = bp["line"].get<int>();
            int bp_id = bp.value("id", 0);
            if (bp_id == 0) {
                bp_id = m_next_bp_id++;
            }
            breakpoints.push_back({{"id", bp_id}, {"line", line}, {"verified", false}});
        }
    }

    sendDapResponse(m_transport, req["seq"], json{{"command", "setBreakpoints"}, {"body", {{"breakpoints", breakpoints}}}});
}

void DapHandler::handleBreakpointLocations(const json& req) {
    const auto& args = req["arguments"];
    std::string src_path = args["source"].value("path", m_source_file);
    if (src_path.empty()) {
        src_path = m_source_file;
    }

    bool is_trust_file = trust::SourceMapReader::isTrustFileExt(src_path);

    int start_line = args.value("line", 1);
    int end_line = args.value("endLine", start_line);

    int num_lines = 0;
    if (is_trust_file && m_source_reader) {
        trust::ReaderFile idx = m_source_reader->findFile(src_path);
        if (idx.isValid()) {
            num_lines = static_cast<int>(m_source_reader->lineCount(idx));
        }
    }

    if (num_lines <= 0) {
        std::ifstream src_stream(src_path);
        if (src_stream.is_open()) {
            std::string _;
            while (std::getline(src_stream, _)) {
                num_lines++;
            }
        }
    }

    if (end_line < start_line && num_lines > 0) {
        end_line = num_lines;
    }

    std::vector<json> locations;
    if (is_trust_file && m_source_reader) {
        trust::ReaderFile idx = m_source_reader->findFile(src_path);
        for (int l = start_line; l <= end_line; ++l) {
            if (idx.isValid()) {
                auto line_start = m_source_reader->loc_from_line(idx, l);
                if (line_start.isValid()) {
                    auto ranges = m_source_reader->findRangesByLine(idx, l);
                    if (!ranges.empty()) {
                        locations.push_back({{"line", l}});
                    }
                }
            } else {
                locations.push_back({{"line", l}});
            }
        }
    } else {
        for (int l = start_line; l <= end_line; ++l) {
            locations.push_back({{"line", l}});
        }
    }

    sendDapResponse(m_transport, req["seq"], json{{"command", "breakpointLocations"}, {"body", {{"breakpoints", locations}}}});
}

void DapHandler::handleSetExceptionBreakpoints(const json& req) {
    sendDapResponse(m_transport, req["seq"], json{{"command", "setExceptionBreakpoints"}, {"body", {{"exceptionBreakpointFilters", json::array()}}}});
}

void DapHandler::handleThreads(const json& req) {
    std::vector<json> threads;
    if (m_debug) {
        int tid = m_debug->getCurrentThreadId();
        threads.push_back({{"id", tid}, {"name", "main"}});
    }
    sendDapResponse(m_transport, req["seq"], json{{"command", "threads"}, {"body", {{"threads", threads}}}});
}

void DapHandler::handleConfigurationDone(const json& req) {
    // Launch the process now — all breakpoints have been set by VSCode.
    if (!m_launched) {
        m_launched = true;
        if (!launchProcess()) {
            std::cerr << "Failed to launch process after configurationDone\n";
        }
    }
    sendDapResponse(m_transport, req["seq"], json{{"command", "configurationDone"}, {"body", json::object()}});
}

void DapHandler::handleContinue(const json& req) {
    if (m_debug) {
        m_debug->Continue();
    }
    sendDapResponse(m_transport, req["seq"], json{{"command", "continue"}, {"body", {{"allThreadsContinued", true}}}});
}

void DapHandler::handleStepIn(const json& req) {
    if (m_debug) {
        m_debug->StepInto();
    }
    sendDapResponse(m_transport, req["seq"], json{{"command", "stepIn"}, {"body", json::object()}});
}

void DapHandler::handleStepOut(const json& req) {
    if (m_debug) {
        m_debug->StepOut();
    }
    sendDapResponse(m_transport, req["seq"], json{{"command", "stepOut"}, {"body", json::object()}});
}

void DapHandler::handleNext(const json& req) {
    if (m_debug) {
        m_debug->StepOver();
    }
    sendDapResponse(m_transport, req["seq"], json{{"command", "next"}, {"body", json::object()}});
}

void DapHandler::handleStackTrace(const json& req) {
    const auto& args = req["arguments"];
    int start_frame = args.value("startFrame", 0);
    int levels = args.value("levels", 20);

    std::string source_kind = args.value("sourceKind", "src");
    bool want_cpp_source = (source_kind == "cpp");

    std::vector<json> stack_frames;

    if (m_debug) {
        auto frames = m_debug->getStackFrames(start_frame, levels);
        for (size_t i = 0; i < frames.size(); ++i) {
            const auto& frame = frames[i];

            std::string display_path = frame.m_file;
            int display_line = frame.m_line;

            std::string trust_path;
            int trust_line = frame.m_line;

            if (!frame.m_file.empty() && m_source_reader) {
                auto mapped = m_source_reader->calcCppToTrustLine(frame.m_file, frame.m_line);
                if (mapped.has_value()) {
                    trust_path = mapped->first;
                    trust_line = mapped->second;
                }
            }

            if (want_cpp_source) {
                display_path = frame.m_file;
                display_line = frame.m_line;
            } else {
                if (!trust_path.empty()) {
                    display_path = trust_path;
                    display_line = trust_line;
                } else {
                    display_path = frame.m_file;
                    display_line = frame.m_line;
                }
            }

            stack_frames.push_back({{"id", static_cast<int>(i)},
                                    {"name", frame.m_func},
                                    {"source", {{"name", display_path}, {"path", display_path}}},
                                    {"line", display_line},
                                    {"column", 0}});
        }
    }

    sendDapResponse(m_transport, req["seq"],
                    json{{"command", "stackTrace"}, {"body", {{"stackFrames", stack_frames}, {"totalFrames", static_cast<int>(stack_frames.size())}}}});
}

void DapHandler::handleScopes(const json& req) {
    sendDapResponse(m_transport, req["seq"],
                    json{{"command", "scopes"}, {"body", {{"scopes", {{{"name", "Local"}, {"variablesReference", 1}, {"expensive", false}}}}}}});
}

void DapHandler::handleVariables(const json& req) {
    const auto& args = req["arguments"];
    int ref = args.value("variablesReference", 0);

    // Получаем текущий C++ location из первого stack frame для корректного NameMap-поиска
    trust::SourceMapReader::Location cppLoc;
    if (m_source_reader && m_debug) {
        auto frames = m_debug->getStackFrames(0, 1);
        if (!frames.empty()) {
            const auto& frame = frames[0];
            trust::ReaderFile cppIdx = m_source_reader->findFile(frame.m_file);
            if (cppIdx.isValid()) {
                cppLoc = m_source_reader->loc_from_line(cppIdx, static_cast<size_t>(frame.m_line));
            }
        }
    }

    std::vector<json> vars;
    if (ref == 1 && m_debug) {
        auto cpp_vars = m_debug->GetVariables();
        for (const auto& var_name : cpp_vars) {
            std::string display_name = var_name;

            if (m_source_reader && cppLoc.isValid()) {
                auto trust_name = m_source_reader->getTrustName(cppLoc, var_name);
                if (trust_name.has_value()) {
                    display_name = trust_name->fromName;
                }
            }

            std::string value_str = m_debug->EvaluateExpression(var_name);
            vars.push_back({{"name", display_name}, {"value", value_str}, {"variablesReference", 0}});
        }
    }

    sendDapResponse(m_transport, req["seq"], json{{"command", "variables"}, {"body", {{"variables", vars}}}});
}

void DapHandler::handleDisconnect(const json& req) {
    m_running = false;
    if (m_event_thread.joinable()) {
        // Ждём завершения потока pollEvents с таймаутом 2 секунды
        // Если поток не завершился — detach, чтобы избежать deadlock при зависшем GDB
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (m_event_thread.joinable() && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        if (m_event_thread.joinable()) {
            std::cerr << "[DAP-TRACE] pollEvents thread did not finish in 2s, detaching\n";
            m_event_thread.detach();
        } else {
            m_event_thread.join();
        }
    }
    sendDapResponse(m_transport, req["seq"], json{{"command", "disconnect"}, {"body", json::object()}});
}

void DapHandler::handleRequest(const json& req) {
    std::string command = req.value("command", "");
    std::string req_type = req.value("type", "");
    if (command != "continue" && command != "stackTrace") {
        std::cerr << "[DAP-TRACE] handleRequest: type=" << req_type << ", command=" << command << ", seq=" << req["seq"] << "\n";
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
        sendDapResponse(m_transport, req["seq"], json{{"command", command}, {"message", "unsupported command: " + command}}, false);
    }
}

// ── Process management ──

bool DapHandler::launchProcess() {
    if (!m_debug) {
        sendDapOutput(m_transport, "stderr", "trust-dap: no debug target, cannot launch\n");
        return false;
    }
    if (!m_debug->Launch()) {
        std::cerr << "Failed to launch: " << m_target_file << "\n";
        return false;
    }

    sendDapOutput(m_transport, "stdout", "trust-dap: launched " + m_target_file + "\n");
    m_event_thread = std::thread(&DapHandler::pollEvents, this);
    return true;
}

void DapHandler::pollEvents() {
    while (m_running) {
        GdbDebug::Event evt = m_debug->WaitForEvent(500);

        if (evt == GdbDebug::Event::Stop) {
            std::string stop_reason = m_debug->getLastStopReason();
            int thread_id = m_debug->getLastThreadId();

            sendDapEvent(m_transport, "stopped", {{"reason", stop_reason}, {"threadId", thread_id}, {"allThreadsStopped", true}});
        } else if (evt == GdbDebug::Event::Exit) {
            sendDapEvent(m_transport, "exited", {{"exitCode", 0}});
            sendDapEvent(m_transport, "terminated", json::object());
            m_running = false;
        }
    }
}