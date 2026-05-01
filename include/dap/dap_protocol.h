#ifndef DAP_PROTOCOL_H
#define DAP_PROTOCOL_H

#include <string>
#include <vector>
#include <map>
#include <optional>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// DAP Message
struct DapMessage {
    int seq = 0;
    std::string type;         // "request" | "response" | "event"
    std::string command;      // request type
    bool success = true;
    std::string message;      // error message
    std::string event;        // event type
    json body;                // message body
    int request_seq = 0;      // for responses
};

// DAP Capabilities
struct DapCapabilities {
    bool supportsConfigurationDoneRequest = true;
    bool supportsFunctionBreakpoints = false;
    bool supportsConditionalBreakpoints = false;
    bool supportsHitConditionalBreakpoints = false;
    bool supportsEvaluateForHovers = false;
    bool exceptionBreakpointFilters = false;
    bool supportsStepBack = false;
    bool supportsSetVariable = true;
    bool supportsRestartFrame = false;
    bool supportsGotoTargetsRequest = false;
    bool supportsClipboardContext = false;
    bool supportsCompletionsRequest = false;
    bool supportsModulesRequest = false;
    bool supportsReadMemoryRequest = false;
    bool supportsDisassembleRequest = false;
    bool supportsCancelRequest = false;
};

class DapServer {
public:
    DapServer();
    
    // Main loop: read from stdin, dispatch, write to stdout
    void run();
    
    // Send a response message
    void sendResponse(int requestSeq, int seq, const std::string& command, 
                      bool success, const std::string& msg, const json& body = {});
    
    // Send an event
    void sendEvent(const std::string& event, const json& body = {});

private:
    // Read message from stdin (with Content-Length parsing)
    DapMessage readMessage();
    
    // Send message to stdout (with Content-Length header)
    void sendMessage(const DapMessage& msg);
    
    // Dispatch command
    void dispatch(const DapMessage& req);
    
    // Request handlers
    void handleInitialize(const DapMessage& req);
    void handleLaunch(const DapMessage& req);
    void handleConfigurationDone(const DapMessage& req);
    void handleSetBreakpoints(const DapMessage& req);
    void handleThreads(const DapMessage& req);
    void handleStackTrace(const DapMessage& req);
    void handleScopes(const DapMessage& req);
    void handleVariables(const DapMessage& req);
    void handleNext(const DapMessage& req);
    void handleStepIn(const DapMessage& req);
    void handleStepOut(const DapMessage& req);
    void handleContinue(const DapMessage& req);
    void handlePause(const DapMessage& req);
    void handleEvaluate(const DapMessage& req);
    void handleDisconnect(const DapMessage& req);
    
    int nextSeq_;
    std::string programPath_;
    std::string sourceMapPath_;
};

#endif // DAP_PROTOCOL_H