#include "debug/gdb_debug.h"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

// ---------------------------------------------------------------------------
// PIMPL: GDB subprocess session (GDB/MI)
// ---------------------------------------------------------------------------
struct GdbDebug::Impl {
    pid_t m_child_pid = 0;
    int m_stdin_fd = -1;
    int m_stdout_fd = -1;
    int m_seq = 1;
    std::string m_buffer; // partial line buffer
    bool m_launched = false;
    bool m_running = false;
    std::string m_console_buffer; // accumulated console output (~"..." lines)

    // Fields for DAP handlers
    std::string m_last_stop_reason; // "breakpoint" | "step" | "exception" | "exit"
    int m_last_thread_id = 1;

    struct MiResponse {
        int token = -1;       // -1 = async notification
        std::string klass;    // "done", "running", "error", "stopped"
        std::string reason;   // e.g. "breakpoint-hit", "exited", "end-stepping-range"
        std::string msg;      // error message
        std::string raw;      // full raw line
        bool isAsync = false; // true if *stopped / *running
    };

    // Start gdb subprocess
    bool Start(const std::string& gdbPath) {
        int stdinPipe[2];
        int stdoutPipe[2];

        if (pipe(stdinPipe) < 0 || pipe(stdoutPipe) < 0) {
            perror("pipe");
            return false;
        }

        m_child_pid = fork();
        if (m_child_pid < 0) {
            perror("fork");
            return false;
        }

        if (m_child_pid == 0) {
            // Child
            close(stdinPipe[1]);
            close(stdoutPipe[0]);

            if (dup2(stdinPipe[0], STDIN_FILENO) < 0) {
                _exit(1);
            }
            if (dup2(stdoutPipe[1], STDOUT_FILENO) < 0) {
                _exit(1);
            }

            for (int i = 3; i < 1024; ++i) {
                close(i);
            }

            execlp(gdbPath.c_str(), gdbPath.c_str(), "--interpreter=mi2", "-q", nullptr);
            // If execlp fails, try execvp with full path search via PATH
            {
                const char* argv[4] = {gdbPath.c_str(), "--interpreter=mi2", "-q", nullptr};
                execvp(gdbPath.c_str(), const_cast<char* const*>(argv));
            }
            perror("execlp/execvp gdb");
            _exit(1);
        }

        // Parent
        close(stdinPipe[0]);
        close(stdoutPipe[1]);
        m_stdin_fd = stdinPipe[1];
        m_stdout_fd = stdoutPipe[0];

        int flags = fcntl(m_stdout_fd, F_GETFL, 0);
        fcntl(m_stdout_fd, F_SETFL, flags | O_NONBLOCK);

        DrainOutput(100);
        return true;
    }

    int SendCommand(const std::string& cmd) {
        int token = m_seq++;
        std::string line = std::to_string(token) + cmd + "\n";
        if (write(m_stdin_fd, line.data(), line.size()) < 0) {
            return -1;
        }
        return token;
    }

    MiResponse WaitForResponse(int timeoutMs = 3000) {
        MiResponse resp;
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);

        while (std::chrono::steady_clock::now() < deadline) {
            std::string line = ReadLine(100);
            if (line.empty()) {
                continue;
            }

            resp.raw = line;
            const char* p = line.c_str();
            int token = -1;

            char* end = nullptr;
            long t = strtol(p, &end, 10);
            if (end != p && *end != 0) {
                token = static_cast<int>(t);
                p = end;
            }

            if (*p == '*') {
                resp.isAsync = true;
                resp.token = token;
                p++;
                const char* comma = strchr(p, ',');
                if (comma) {
                    resp.klass = std::string(p, comma - p);
                    ParseKeyValues(comma + 1, resp);
                    // Update last stop reason from *stopped async notification
                    if (resp.klass == "stopped") {
                        MapStopReason(resp.reason);
                    }
                } else {
                    resp.klass = p;
                }
                return resp;
            } else if (*p == '^') {
                resp.isAsync = false;
                resp.token = token;
                p++;
                const char* comma = strchr(p, ',');
                if (comma) {
                    resp.klass = std::string(p, comma - p);
                    ParseKeyValues(comma + 1, resp);
                } else {
                    resp.klass = p;
                }
                return resp;
            } else if (*p == '=' || *p == '&' || *p == '@') {
                continue; // ignore
            } else if (*p == '~') {
                AccumulateConsole(line);
                continue;
            }
        }

        resp.klass = "timeout";
        return resp;
    }

    MiResponse WaitForStop(int timeoutMs = 5000) {
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
        MiResponse t;
        t.klass = "timeout";
        return t;
    }

    void Close() {
        if (m_child_pid > 0) {
            kill(m_child_pid, SIGTERM);
            int status = 0;
            waitpid(m_child_pid, &status, 0);
            m_child_pid = 0;
        }
        if (m_stdin_fd >= 0) {
            close(m_stdin_fd);
            m_stdin_fd = -1;
        }
        if (m_stdout_fd >= 0) {
            close(m_stdout_fd);
            m_stdout_fd = -1;
        }
    }

    // Parse -stack-list-frames output
    std::vector<StackFrameInfo> ParseStackFrames(int startFrame, int maxCount) {
        std::string cmd = "-stack-list-frames " + std::to_string(startFrame) + " " + std::to_string(startFrame + maxCount - 1);
        SendCommand(cmd);
        auto resp = WaitForResponse();
        std::vector<StackFrameInfo> frames;

        if (resp.klass != "done") {
            return frames;
        }

        // Parse: ^done,stack=[frame={level="0",addr="0x...",func="main",file="test.cpp",fullname="/path/test.cpp",line="42"},...]
        const char* p = resp.raw.c_str();
        const char* stack_start = strstr(p, "stack=[");
        if (stack_start == nullptr) {
            return frames;
        }
        p = stack_start + 7; // skip "stack=["

        while (*p != '\0' && *p != ']') {
            // Skip whitespace
            while (*p == ' ' || *p == '\t') {
                p++;
            }
            if (*p == '\0' || *p == ']') {
                break;
            }

            // Expect "frame={"
            if (strncmp(p, "frame={", 7) != 0) {
                break;
            }
            p += 7;

            StackFrameInfo frame;
            while (*p != '\0' && *p != '}') {
                // Skip whitespace
                while (*p == ' ' || *p == '\t') {
                    p++;
                }
                if (*p == '\0' || *p == '}') {
                    break;
                }

                // key=value  (value may be quoted or unquoted)
                const char* eq = strchr(p, '=');
                if (eq == nullptr) {
                    break;
                }

                std::string key(p, eq - p);
                p = eq + 1;

                std::string value;
                if (*p == '"') {
                    p++; // skip opening quote
                    const char* val_end = strchr(p, '"');
                    if (val_end == nullptr) {
                        break;
                    }
                    value = std::string(p, val_end - p);
                    p = val_end + 1;
                } else {
                    const char* val_end = strchr(p, ',');
                    const char* brace_end = strchr(p, '}');
                    if (val_end == nullptr && brace_end == nullptr) {
                        break;
                    }
                    const char* end = val_end;
                    if (brace_end != nullptr && (val_end == nullptr || brace_end < val_end)) {
                        end = brace_end;
                    }
                    value = std::string(p, end - p);
                    p = end;
                }

                if (key == "func") {
                    frame.m_func = value;
                } else if (key == "file") {
                    frame.m_file = value;
                } else if (key == "line") {
                    frame.m_line = std::stoi(value);
                }

                // Skip comma separator
                if (*p == ',') {
                    p++;
                }
            }

            frames.push_back(frame);

            // Skip closing '}'
            if (*p == '}') {
                p++;
            }
            // Skip comma
            if (*p == ',') {
                p++;
            }
        }

        return frames;
    }

  private:
    void DrainOutput(int maxMs) {
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(maxMs);
        while (std::chrono::steady_clock::now() < deadline) {
            struct pollfd pfd;
            pfd.fd = m_stdout_fd;
            pfd.events = POLLIN;
            int ret = poll(&pfd, 1, 10);
            if (ret > 0 && (pfd.revents & POLLIN)) {
                char buf[4096];
                ssize_t n = read(m_stdout_fd, buf, sizeof(buf) - 1);
                if (n > 0) {
                    buf[n] = 0;
                } else {
                    break;
                }
            } else {
                break;
            }
        }
    }

    // Accumulate console output from ~"..." lines
    void AccumulateConsole(const std::string& line) {
        if (!line.empty() && line[0] == '~') {
            const char* p = line.c_str() + 1;
            // Skip leading whitespace after ~
            while (*p == ' ') {
                ++p;
            }
            if (*p == '"') {
                ++p;
                const char* end = strrchr(p, '"');
                if (end) {
                    m_console_buffer += std::string(p, end - p);
                }
            }
        }
    }

    // Map GDB/MI stop reason to DAP-compatible reason string
    void MapStopReason(const std::string& gdbReason) {
        if (gdbReason == "breakpoint-hit" || gdbReason == "watchpoint-trigger" || gdbReason == "read-watchpoint-trigger" ||
            gdbReason == "access-watchpoint-trigger" || gdbReason == "function-finished") {
            m_last_stop_reason = "breakpoint";
        } else if (gdbReason == "end-stepping-range" || gdbReason == "step-end") {
            m_last_stop_reason = "step";
        } else if (gdbReason == "signal-received") {
            m_last_stop_reason = "exception";
        } else if (gdbReason == "exited" || gdbReason == "exited-normally" || gdbReason == "exited-signalled") {
            m_last_stop_reason = "exit";
        } else {
            m_last_stop_reason = "breakpoint"; // default fallback
        }
    }

    std::string ReadLine(int timeoutMs) {
        auto newlinePos = m_buffer.find('\n');
        if (newlinePos != std::string::npos) {
            std::string line = m_buffer.substr(0, newlinePos);
            m_buffer.erase(0, newlinePos + 1);
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            return line;
        }

        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        while (std::chrono::steady_clock::now() < deadline) {
            struct pollfd pfd;
            pfd.fd = m_stdout_fd;
            pfd.events = POLLIN;
            int ret = poll(&pfd, 1, 50);
            if (ret < 0) {
                break;
            }
            if (ret > 0 && (pfd.revents & POLLIN)) {
                char buf[4096];
                ssize_t n = read(m_stdout_fd, buf, sizeof(buf) - 1);
                if (n > 0) {
                    buf[n] = 0;
                    m_buffer += buf;
                    newlinePos = m_buffer.find('\n');
                    if (newlinePos != std::string::npos) {
                        std::string line = m_buffer.substr(0, newlinePos);
                        m_buffer.erase(0, newlinePos + 1);
                        if (!line.empty() && line.back() == '\r') {
                            line.pop_back();
                        }
                        return line;
                    }
                } else {
                    break;
                }
            }
        }
        return {};
    }

    static void ParseKeyValues(const char* p, MiResponse& resp) {
        while (p && *p) {
            while (*p == ' ') {
                ++p;
            }
            if (!*p) {
                break;
            }
            const char* eq = strchr(p, '=');
            if (!eq) {
                break;
            }
            std::string key(p, eq - p);
            const char* valStart = eq + 1;
            if (*valStart == '"') {
                valStart++;
                const char* valEnd = strchr(valStart, '"');
                if (valEnd) {
                    std::string value(valStart, valEnd - valStart);
                    if (key == "reason") {
                        resp.reason = value;
                    } else if (key == "msg") {
                        resp.msg = value;
                    }
                    p = valEnd + 1;
                } else {
                    break;
                }
            } else {
                const char* valEnd = strchr(valStart, ',');
                if (valEnd) {
                    std::string value(valStart, valEnd - valStart);
                    if (key == "reason") {
                        resp.reason = value;
                    } else if (key == "msg") {
                        resp.msg = value;
                    }
                    p = valEnd + 1;
                } else {
                    std::string value(valStart);
                    if (key == "reason") {
                        resp.reason = value;
                    } else if (key == "msg") {
                        resp.msg = value;
                    }
                    break;
                }
            }
        }
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
    if (!m_impl->Start(m_cfg.gdbPath)) {
        return false;
    }
    m_impl->SendCommand("-file-exec-and-symbols " + exe);
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
    m_impl->SendCommand("-break-insert \"" + name + "\"");
    auto resp = m_impl->WaitForResponse();
    if (resp.klass == "done") {
        return parseBkptNumber(resp.raw);
    }
    return -1;
}

int GdbDebug::BreakpointBySource(const std::string& file, int line) {
    m_impl->SendCommand("-break-insert \"" + file + "\":" + std::to_string(line));
    auto resp = m_impl->WaitForResponse();
    if (resp.klass == "done") {
        return parseBkptNumber(resp.raw);
    }
    return -1;
}

bool GdbDebug::Launch() {
    m_impl->SendCommand("-exec-run");
    // Don't wait for stop synchronously — *stopped will arrive asynchronously via WaitForEvent.
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
    m_impl->SendCommand("-exec-next");
    m_impl->WaitForStop();
}

void GdbDebug::StepInto() {
    m_impl->SendCommand("-exec-step");
    m_impl->WaitForStop();
}

void GdbDebug::StepOut() {
    m_impl->SendCommand("-exec-finish");
    m_impl->WaitForStop();
}

void GdbDebug::Continue() {
    m_impl->m_running = true;
    m_impl->SendCommand("-exec-continue");
}

std::vector<GdbDebug::StackFrameInfo> GdbDebug::getStackFrames(int startFrame, int maxCount) {
    return m_impl->ParseStackFrames(startFrame, maxCount);
}

std::string GdbDebug::getLastStopReason() const {
    return m_impl->m_last_stop_reason;
}

int GdbDebug::getLastThreadId() const {
    return m_impl->m_last_thread_id;
}

int GdbDebug::getCurrentThreadId() const {
    // GDB/MI single process, single thread model — always 1
    return 1;
}

std::string GdbDebug::EvaluateExpression(const std::string& expr) {
    m_impl->SendCommand("-data-evaluate-expression " + expr);
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
    m_impl->SendCommand("-stack-list-variables --simple-values");
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
    pfd.fd = m_impl->m_stdout_fd;
    pfd.events = POLLIN;
    while (poll(&pfd, 1, 0) > 0 && (pfd.revents & POLLIN)) {
        char buf[4096];
        ssize_t n = read(m_impl->m_stdout_fd, buf, sizeof(buf) - 1);
        if (n <= 0) {
            break;
        }
    }
    std::string result;
    std::swap(result, m_impl->m_console_buffer);
    return result;
}