#include "dap_protocol.h"
#include "trust_mapper.h"
#include "gdb_bridge.h"
#include <iostream>
#include <sstream>
#include <filesystem>

// Global components
static TrustMapper g_sourceMapper;
static GdbBridge g_gdbBridge;

DapServer::DapServer() : nextSeq_(1) {}

void DapServer::run() {
    // Read from stdin and dispatch until no more input
    while (true) {
        try {
            DapMessage req = readMessage();
            if (req.type == "request") {
                dispatch(req);
            }
        } catch (const std::exception& e) {
            std::cerr << "Error in DAP loop: " << e.what() << std::endl;
            break;
        }
    }
}

DapMessage DapServer::readMessage() {
    // Read Content-Length header
    std::string line;
    int contentLength = 0;
    
    while (std::getline(std::cin, line)) {
        // Remove \r if present
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        
        if (line.empty()) {
            // Empty line signals end of headers, read body
            break;
        }
        
        if (line.substr(0, 16) == "Content-Length: ") {
            contentLength = std::stoi(line.substr(16));
        }
    }
    
    if (contentLength == 0) {
        std::cerr << "No Content-Length header, ending" << std::endl;
        std::exit(0);
    }
    
    // Read body
    std::string body(contentLength, ' ');
    std::cin.read(&body[0], contentLength);
    
    std::cerr << "Received: " << body << std::endl;
    
    json j = json::parse(body);
    DapMessage msg;
    msg.seq = j.value("seq", 0);
    msg.type = j.value("type", "request");
    
    if (j.contains("command")) msg.command = j["command"];
    if (j.contains("event")) msg.event = j["event"];
    if (j.contains("request_seq")) msg.request_seq = j["request_seq"];
    if (j.contains("success")) msg.success = j["success"];
    if (j.contains("message")) msg.message = j["message"];
    if (j.contains("body")) msg.body = j["body"];
    else if (j.contains("arguments")) msg.body = j["arguments"];
    
    return msg;
}

void DapServer::sendMessage(const DapMessage& msg) {
    json j;
    j["type"] = msg.type;
    j["request_seq"] = msg.request_seq;
    if (msg.type == "request") {
        j["seq"] = nextSeq_++;
        j["command"] = msg.command;
    } else if (msg.type == "response") {
        j["seq"] = nextSeq_++;
        j["command"] = msg.command;
        j["success"] = msg.success;
        if (!msg.message.empty()) j["message"] = msg.message;
    } else if (msg.type == "event") {
        j["seq"] = nextSeq_++;
        j["event"] = msg.event;
    }
    
    if (!msg.body.empty()) {
        j["body"] = msg.body;
    }
    
    std::string jsonString = j.dump();
    std::string header = "Content-Length: " + std::to_string(jsonString.size()) + "\r\n\r\n";
    
    std::string output = header + jsonString;
    std::cout << output;
    std::cout.flush();
    
    std::cerr << "Sent: " << output << std::endl;
}

void DapServer::sendResponse(int requestSeq, int seq, const std::string& command,
                              bool success, const std::string& msg, const json& body) {
    DapMessage resp;
    resp.type = "response";
    resp.request_seq = requestSeq;
    resp.command = command;
    resp.success = success;
    resp.message = msg;
    resp.body = body;
    sendMessage(resp);
}

void DapServer::sendEvent(const std::string& event, const json& body) {
    DapMessage evt;
    evt.type = "event";
    evt.event = event;
    evt.body = body;
    sendMessage(evt);
}

void DapServer::dispatch(const DapMessage& req) {
    if (req.command == "initialize") handleInitialize(req);
    else if (req.command == "launch") handleLaunch(req);
    else if (req.command == "configurationDone") handleConfigurationDone(req);
    else if (req.command == "setBreakpoints") handleSetBreakpoints(req);
    else if (req.command == "threads") handleThreads(req);
    else if (req.command == "stackTrace") handleStackTrace(req);
    else if (req.command == "scopes") handleScopes(req);
    else if (req.command == "variables") handleVariables(req);
    else if (req.command == "next") handleNext(req);
    else if (req.command == "stepIn") handleStepIn(req);
    else if (req.command == "stepOut") handleStepOut(req);
    else if (req.command == "continue") handleContinue(req);
    else if (req.command == "pause") handlePause(req);
    else if (req.command == "evaluate") handleEvaluate(req);
    else if (req.command == "disconnect") handleDisconnect(req);
    else {
        sendResponse(req.seq, nextSeq_, req.command, false, 
                     "Unknown command: " + req.command);
    }
}

void DapServer::handleInitialize(const DapMessage& req) {
    json body;
    body["supportsConfigurationDoneRequest"] = true;
    sendResponse(req.seq, nextSeq_, "initialize", true, "", body);
}

void DapServer::handleLaunch(const DapMessage& req) {
    if (req.body.contains("program")) {
        programPath_ = req.body["program"].get<std::string>();
    }
    if (req.body.contains("sourceMap")) {
        sourceMapPath_ = req.body["sourceMap"].get<std::string>();
    }

    std::cerr << "DAP Launch: program=" << programPath_ << ", sourceMap=" << sourceMapPath_ << std::endl;

    // Load source map from ELF section or JSON file
    g_sourceMapper.load(programPath_, sourceMapPath_);

    // Start GDB with the ELF program to debug
    if (!g_gdbBridge.start(programPath_)) {
        sendResponse(req.seq, nextSeq_, "launch", false, "Failed to start GDB");
        return;
    }
    
    // Set up GDB callbacks
    g_gdbBridge.setStoppedCallback([this](const StoppedEvent& evt) {
        json body;
        body["reason"] = evt.reason;
        body["threadId"] = evt.thread_id;
        
        // Convert cpp line to trust line if possible
        if (evt.frame_id > 0) {
            auto trustInfo = g_sourceMapper.cppToTrust(programPath_, evt.frame_id);
            if (trustInfo.second > 0) {
                json source;
                source["name"] = std::filesystem::path(trustInfo.first).filename();
                source["path"] = trustInfo.first;
                body["source"] = source;
                body["line"] = trustInfo.second;
            }
        }
        
        sendEvent("stopped", body);
    });
    
    sendResponse(req.seq, nextSeq_, "launch", true, "");
}

void DapServer::handleConfigurationDone(const DapMessage& req) {
    // Send initialized event first
    sendEvent("initialized");
    
    // Now run the program
    g_gdbBridge.run();
    sendResponse(req.seq, nextSeq_, "configurationDone", true, "");
}

void DapServer::handleSetBreakpoints(const DapMessage& req) {
    json respBody;
    std::vector<json> breakpoints;
    
    if (req.body.contains("source")) {
        std::string sourcePath = req.body["source"].value("path", "");
        
        if (req.body.contains("lines") || req.body.contains("breakpoints")) {
            json lines;
            if (req.body.contains("breakpoints")) {
                lines = json::array();
                for (const auto& bp : req.body["breakpoints"]) {
                    lines.push_back(bp.value("line", 0));
                }
            } else {
                lines = req.body["lines"];
            }
            
            for (const auto& line : lines) {
                int trustLine = line.get<int>();
                auto cppMapping = g_sourceMapper.trustToCpp(sourcePath, trustLine);
                const std::string& cppFile = cppMapping.first;
                int cppLine = cppMapping.second;
                
                json bp;
                bp["line"] = trustLine;  // Return trust line, not C++ line
                bp["source"] = req.body["source"];
                
                if (cppLine > 0 && !cppFile.empty()) {
                    // Set breakpoint in GDB with C++ file and line
                    bool success = g_gdbBridge.setBreakpoint(cppFile, cppLine);
                    
                    bp["id"] = cppLine;
                    bp["verified"] = success;
                    if (!success) {
                        bp["message"] = "GDB failed to set breakpoint at " + cppFile + ":" + std::to_string(cppLine);
                    }
                } else {
                    bp["verified"] = false;
                    bp["message"] = "No mapping found for Trust line " + std::to_string(trustLine);
                }
                breakpoints.push_back(bp);
            }
        }
    }
    
    respBody["breakpoints"] = breakpoints;
    sendResponse(req.seq, nextSeq_, "setBreakpoints", true, "", respBody);
}

void DapServer::handleThreads(const DapMessage& req) {
    json respBody;
    respBody["threads"] = json::array();
    
    auto threads = g_gdbBridge.getThreads();
    for (int id : threads) {
        json t;
        t["id"] = id;
        t["name"] = "Thread " + std::to_string(id);
        respBody["threads"].push_back(t);
    }
    
    if (respBody["threads"].empty()) {
        json t;
        t["id"] = 1;
        t["name"] = "Main Thread";
        respBody["threads"].push_back(t);
    }
    
    sendResponse(req.seq, nextSeq_, "threads", true, "", respBody);
}

void DapServer::handleStackTrace(const DapMessage& req) {
    json respBody;
    respBody["stackFrames"] = json::array();
    
    // Get stack frames from GDB
    int threadId = 1;
    if (req.body.contains("threadId")) {
        threadId = req.body["threadId"].get<int>();
    }
    
    auto frames = g_gdbBridge.getStackFrames(threadId);
    for (const auto& frame : frames) {
        json sf;
        sf["id"] = frame.frame_id;
        sf["name"] = frame.function.empty() ? "main" : frame.function;
        sf["line"] = frame.line;
        sf["column"] = 1;
        
        // Convert C++ file/line to trust
        auto trustInfo = g_sourceMapper.cppToTrust(programPath_, frame.line);
        if (trustInfo.second > 0) {
            json source;
            source["name"] = std::filesystem::path(trustInfo.first).filename();
            source["path"] = trustInfo.first;
            sf["source"] = source;
            sf["line"] = trustInfo.second;
        }
        
        respBody["stackFrames"].push_back(sf);
    }
    
    sendResponse(req.seq, nextSeq_, "stackTrace", true, "", respBody);
}

void DapServer::handleScopes(const DapMessage& req) {
    json respBody;
    respBody["scopes"] = json::array();
    
    json scope;
    scope["name"] = "Local";
    scope["presentationHint"] = "locals";
    scope["variablesReference"] = req.body.value("frameId", 0);
    respBody["scopes"].push_back(scope);
    
    sendResponse(req.seq, nextSeq_, "scopes", true, "", respBody);
}

void DapServer::handleVariables(const DapMessage& req) {
    json respBody;
    respBody["variables"] = json::array();
    
    int varsRef = req.body.value("variablesReference", 0);
    
    // Use GDB to get variables and map names through source mapper
    auto vars = g_gdbBridge.getVariables(varsRef);
    for (const auto& v : vars) {
        json var;
        var["name"] = v.name;
        var["value"] = v.value;
        respBody["variables"].push_back(var);
    }
    
    sendResponse(req.seq, nextSeq_, "variables", true, "", respBody);
}

void DapServer::handleNext(const DapMessage& req) {
    g_gdbBridge.stepNext();
    sendResponse(req.seq, nextSeq_, "next", true, "");
}

void DapServer::handleStepIn(const DapMessage& req) {
    g_gdbBridge.stepIn();
    sendResponse(req.seq, nextSeq_, "stepIn", true, "");
}

void DapServer::handleStepOut(const DapMessage& req) {
    g_gdbBridge.stepOut();
    sendResponse(req.seq, nextSeq_, "stepOut", true, "");
}

void DapServer::handleContinue(const DapMessage& req) {
    g_gdbBridge.continueExecution();
    
    json body;
    body["allThreadsContinued"] = true;
    sendResponse(req.seq, nextSeq_, "continue", true, "", body);
}

void DapServer::handlePause(const DapMessage& req) {
    g_gdbBridge.pause();
    
    // Send response immediately, the stopped event will come asynchronously
    sendResponse(req.seq, nextSeq_, "pause", true, "");
}

void DapServer::handleEvaluate(const DapMessage& req) {
    std::string expression;
    if (req.body.contains("expression")) {
        expression = req.body["expression"].get<std::string>();
    }
    
    int frameId = req.body.value("frameId", 0);
    std::string result = g_gdbBridge.evaluate(expression, frameId);
    
    json body;
    body["result"] = result.empty() ? "N/A" : result;
    body["variablesReference"] = 0;
    
    sendResponse(req.seq, nextSeq_, "evaluate", true, "", body);
}

void DapServer::handleDisconnect(const DapMessage& req) {
    g_gdbBridge.stop();
    
    sendEvent("terminated");
    sendResponse(req.seq, nextSeq_, "disconnect", true, "");
    
    std::exit(0);
}