#include "debug/gdb/gdb_debug.h"

#include "debug/mi/mi_parse.hpp"
#include "debug/mi/mi_transport.hpp"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include <poll.h>
#include <unistd.h>

// ---------------------------------------------------------------------------
// PIMPL: GDB session (GDB/MI) - состояние сессии поверх MI-транспорта и
// чистого разбора строк. Транспорт - debug/mi/mi_transport.hpp, разбор -
// debug/mi/mi_parse.hpp.
// ---------------------------------------------------------------------------
struct GdbDebug::Impl {
    trust::debug::detail::MiTransport m_mi;

    bool m_launched = false;
    bool m_running = false;
    std::string m_console_buffer; // accumulated console output (~"..." lines)

    // Fields for DAP handlers
    std::string m_last_stop_reason; // "breakpoint" | "step" | "exception" | "exit"

    trust::debug::detail::MiResponse WaitForResponse(int timeoutMs = 3000) {
        trust::debug::detail::MiResponse resp;
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);

        while (std::chrono::steady_clock::now() < deadline) {
            std::string line = m_mi.readLine(100);
            if (line.empty()) {
                continue;
            }

            auto parsed = trust::debug::detail::parseMiResponseLine(line);
            if (!parsed) {
                if (auto text = trust::debug::detail::miConsoleText(line)) {
                    m_console_buffer += *text;
                }
                continue;
            }

            resp = std::move(*parsed);
            resp.raw = line;
            // Update last stop reason from *stopped async notification
            if (resp.isAsync && resp.klass == "stopped") {
                m_last_stop_reason = trust::debug::detail::mapStopReason(resp.reason);
            }
            return resp;
        }

        resp.klass = "timeout";
        return resp;
    }

    trust::debug::detail::MiResponse WaitForStop(int timeoutMs = 5000) {
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        while (std::chrono::steady_clock::now() < deadline) {
            auto resp = WaitForResponse(500);
            if (resp.isAsync && resp.klass == "stopped") {
                return resp;
            }
            if (resp.klass == "timeout") {
                return resp;
            }
        }
        trust::debug::detail::MiResponse t;
        t.klass = "timeout";
        return t;
    }

    void Close() { m_mi.close(); }

    // Parse -stack-list-frames output
    std::vector<trust::debug::StackFrameInfo> ParseStackFrames(int startFrame, int maxCount) {
        std::string cmd = "-stack-list-frames " + std::to_string(startFrame) + " " + std::to_string(startFrame + maxCount - 1);
        m_mi.sendCommand(cmd);
        auto resp = WaitForResponse();
        if (resp.klass != "done") {
            return {};
        }

        return trust::debug::detail::parseStackFrames(resp.raw);
    }
};

// ---------------------------------------------------------------------------
// GdbDebug public API
// ---------------------------------------------------------------------------
GdbDebug::GdbDebug()
: GdbDebug(Config{}) {
}

GdbDebug::GdbDebug(const Config& cfg)
: m_cfg(cfg)
, m_impl(std::make_unique<Impl>()) {
}

GdbDebug::~GdbDebug() {
    if (m_impl) {
        m_impl->Close();
    }
}

bool GdbDebug::CreateTarget(const std::string& exe) {
    if (!m_impl->m_mi.start(m_cfg.gdbPath)) {
        return false;
    }
    m_impl->m_mi.sendCommand("-file-exec-and-symbols " + exe);
    auto resp = m_impl->WaitForResponse();
    return resp.klass == "done";
}

static int parseBkptNumber(const std::string& raw) {
    const char* numStart = strstr(raw.c_str(), "number=\"");
    if (!numStart) {
        return -1;
    }
    numStart += 8; // skip "number=\""
    char* end = nullptr;
    long n = strtol(numStart, &end, 10);
    if (end == numStart) {
        return -1;
    }
    return static_cast<int>(n);
}

int GdbDebug::BreakpointCreateByName(const std::string& name) {
    m_impl->m_mi.sendCommand("-break-insert \"" + name + "\"");
    auto resp = m_impl->WaitForResponse();
    if (resp.klass == "done") {
        return parseBkptNumber(resp.raw);
    }
    return -1;
}

int GdbDebug::BreakpointBySource(const std::string& file, int line) {
    m_impl->m_mi.sendCommand("-break-insert \"" + file + "\":" + std::to_string(line));
    auto resp = m_impl->WaitForResponse();
    if (resp.klass == "done") {
        return parseBkptNumber(resp.raw);
    }
    return -1;
}

bool GdbDebug::Launch() {
    m_impl->m_mi.sendCommand("-exec-run");
    // Don't wait for stop synchronously - *stopped will arrive asynchronously via WaitForEvent.
    // The process may stop at main (breakpoint) or exit immediately if no breakpoints.
    m_impl->m_launched = true;
    m_impl->m_running = true;
    return true;
}

GdbDebug::Event GdbDebug::WaitForEvent(int timeoutMs) {
    if (!m_impl->m_launched) {
        return Event::Error;
    }
    auto resp = m_impl->WaitForStop(timeoutMs);
    if (resp.klass == "timeout") {
        return Event::Timeout;
    }
    if (resp.isAsync && resp.klass == "stopped") {
        // Update thread info from *stopped if available
        if (!resp.reason.empty()) {
            m_impl->m_last_stop_reason = resp.reason;
        }
        if (resp.reason == "exited" || resp.reason == "exited-normally" || resp.reason == "exited-signalled") {
            m_impl->m_running = false;
            return Event::Exit;
        }
        return Event::Stop;
    }
    return Event::Timeout;
}

void GdbDebug::StepOver() {
    m_impl->m_mi.sendCommand("-exec-next");
    m_impl->WaitForStop();
}

void GdbDebug::StepInto() {
    m_impl->m_mi.sendCommand("-exec-step");
    m_impl->WaitForStop();
}

void GdbDebug::StepOut() {
    m_impl->m_mi.sendCommand("-exec-finish");
    m_impl->WaitForStop();
}

void GdbDebug::Continue() {
    m_impl->m_running = true;
    m_impl->m_mi.sendCommand("-exec-continue");
}

std::vector<trust::debug::StackFrameInfo> GdbDebug::getStackFrames(int startFrame, int maxCount) {
    return m_impl->ParseStackFrames(startFrame, maxCount);
}

std::string GdbDebug::getLastStopReason() const {
    return m_impl->m_last_stop_reason;
}

int GdbDebug::getLastThreadId() const {
    // GDB/MI single process, single thread model - always 1
    return 1;
}

int GdbDebug::getCurrentThreadId() const {
    // GDB/MI single process, single thread model - always 1
    return 1;
}

std::string GdbDebug::EvaluateExpression(const std::string& expr) {
    m_impl->m_mi.sendCommand("-data-evaluate-expression " + expr);
    auto resp = m_impl->WaitForResponse();
    if (resp.klass == "done") {
        // Parse value from ^done,value="XXX"
        const char* p = resp.raw.c_str();
        const char* val = strstr(p, "value=\"");
        if (val) {
            val += 7;
            const char* end = strchr(val, '"');
            if (end) {
                return std::string(val, end - val);
            }
        }
        return resp.raw;
    }
    return {};
}

std::vector<std::string> GdbDebug::GetVariables() {
    m_impl->m_mi.sendCommand("-stack-list-variables --simple-values");
    auto resp = m_impl->WaitForResponse();
    if (resp.klass == "done") {
        std::vector<std::string> vars;
        const char* p = resp.raw.c_str();
        const char* markers = strstr(p, "variables=[");
        if (markers) {
            p = markers + 10; // skip "variables=["
            while (*p && *p != ']') {
                const char* nameEq = strstr(p, "name=\"");
                if (!nameEq) {
                    break;
                }
                nameEq += 6;
                const char* nameEnd = strchr(nameEq, '"');
                if (!nameEnd) {
                    break;
                }
                vars.push_back(std::string(nameEq, nameEnd - nameEq));
                p = nameEnd + 1;
            }
        }
        return vars;
    }
    return {};
}

bool GdbDebug::IsRunning() const {
    return m_impl->m_running;
}

std::string GdbDebug::ReadStdout() {
    struct pollfd pfd;
    pfd.fd = m_impl->m_mi.stdoutFd();
    pfd.events = POLLIN;
    while (poll(&pfd, 1, 0) > 0 && (pfd.revents & POLLIN)) {
        char buf[4096];
        ssize_t n = read(m_impl->m_mi.stdoutFd(), buf, sizeof(buf) - 1);
        if (n <= 0) {
            break;
        }
    }
    std::string result;
    std::swap(result, m_impl->m_console_buffer);
    return result;
}