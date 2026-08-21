#pragma once

#include <string>

#include "utils/transport.hpp"
#include <nlohmann/json.hpp>

// -- Опции командной строки --
struct DapOptions {
    int port = -1;          // -1 = interactive, >0 = server mode
    std::string projectDir; // рабочая директория проекта
    std::string gdbPath;    // path to gdb binary (default: "gdb")
    bool help = false;
};

static constexpr int DAP_DEFAULT_PORT = 4711;

// -- DAP Protocol helpers --
// Принимают trust::transport::Transport& (см. utils/transport.hpp)
int nextDapSeq();
void sendDapResponse(trust::transport::Transport& transport, int requestSeq, const nlohmann::json& body, bool success = true);
void sendDapEvent(trust::transport::Transport& transport, const std::string& eventName, const nlohmann::json& body);
void sendDapOutput(trust::transport::Transport& transport, const std::string& category, const std::string& output);

// -- DAP packet helpers --
nlohmann::json readDapPacket(trust::transport::Transport& transport);
void sendBreakpointEvent(trust::transport::Transport& transport, const std::string& srcPath, int line, int bpId, bool verified);

// -- CLI parsing --
DapOptions parseDapOptions(int argc, const char* argv[]);
void printUsage(const char* prog);
