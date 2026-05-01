#include "gdb_bridge.h"
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <sstream>
#include <iostream>
#include <cstring>
#include <algorithm>

GdbBridge::GdbBridge() : pending_token_(-1), response_ready_(false), token_counter_(1) {}

GdbBridge::~GdbBridge() {
    stop();
}

bool GdbBridge::start(const std::string& programPath) {
    program_path_ = programPath;
    
    // Create pipes
    if (pipe(gdb_stdin_pipe_) != 0 || pipe(gdb_stdout_pipe_) != 0 || pipe(gdb_stderr_pipe_) != 0) {
        std::cerr << "Failed to create pipes" << std::endl;
        return false;
    }
    
    gdb_pid_ = fork();
    if (gdb_pid_ < 0) {
        std::cerr << "Failed to fork" << std::endl;
        return false;
    }
    
    if (gdb_pid_ == 0) {
        // Child process
        close(gdb_stdin_pipe_[1]);
        close(gdb_stdout_pipe_[0]);
        close(gdb_stderr_pipe_[0]);
        
        dup2(gdb_stdin_pipe_[0], STDIN_FILENO);
        dup2(gdb_stdout_pipe_[1], STDOUT_FILENO);
        dup2(gdb_stderr_pipe_[1], STDERR_FILENO);
        
        execle("/usr/bin/gdb", "gdb", "--interpreter=mi2", "--quiet",
               programPath.c_str(), nullptr, environ);
        _exit(127);
    }
    
    // Parent process
    close(gdb_stdin_pipe_[0]);
    close(gdb_stdout_pipe_[1]);
    close(gdb_stderr_pipe_[1]);
    
    running_ = true;
    read_thread_ = std::thread(&GdbBridge::readLoop, this);
    
    // Wait for GDB to start
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    std::cout << "GDB started with PID: " << gdb_pid_ << std::endl;
    return true;
}

void GdbBridge::pause() {
    // Send interrupt to GDB to pause execution
    sendCommand("-exec-interrupt");
}

void GdbBridge::stop() {
    if (!running_) return;
    
    running_ = false;
    
    // First, close stdin to wake up read thread (read returns 0 EOF)
    if (gdb_stdin_pipe_[1] > 0) {
        close(gdb_stdin_pipe_[1]);
        gdb_stdin_pipe_[1] = -1;
    }
    
    // Also send interrupt if running with -interpreter=mi to ensure GDB stops
    if (gdb_pid_ > 0) {
        kill(gdb_pid_, SIGINT);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    
    // Wait for read thread to finish
    if (read_thread_.joinable()) {
        read_thread_.join();
    }
    
    // Kill GDB if still running
    if (gdb_pid_ > 0) {
        kill(gdb_pid_, SIGTERM);
        waitpid(gdb_pid_, nullptr, WNOHANG);
        gdb_pid_ = -1;
    }
    
    // Close remaining pipes
    if (gdb_stdout_pipe_[0] > 0) close(gdb_stdout_pipe_[0]);
    if (gdb_stderr_pipe_[0] > 0) close(gdb_stderr_pipe_[0]);
    
    gdb_stdout_pipe_[0] = -1;
    gdb_stderr_pipe_[0] = -1;
}

int GdbBridge::nextToken() {
    return token_counter_++;
}

void GdbBridge::sendCommand(const std::string& cmd) {
    if (gdb_stdin_pipe_[1] < 0) return;
    
    std::string fullCmd = cmd + "\n";
    write(gdb_stdin_pipe_[1], fullCmd.c_str(), fullCmd.size());
}

void GdbBridge::sendCommand(int token, const std::string& cmd) {
    if (gdb_stdin_pipe_[1] < 0) return;
    
    std::string fullCmd = std::to_string(token) + cmd + "\n";
    write(gdb_stdin_pipe_[1], fullCmd.c_str(), fullCmd.size());
}

std::string GdbBridge::waitForResponse(int token, int timeoutMs) {
    std::unique_lock<std::mutex> lock(mutex_);
    pending_token_ = token;
    response_ready_ = false;
    pending_response_.clear();
    
    // Wait for response or timeout
    bool gotResponse = cv_.wait_for(lock, std::chrono::milliseconds(timeoutMs), [this]() {
        return response_ready_;
    });
    
    if (gotResponse) {
        return pending_response_;
    }
    return "";
}

void GdbBridge::readLoop() {
    char buffer[4096];
    std::string lineBuffer;
    
    while (running_) {
        ssize_t bytesRead = read(gdb_stdout_pipe_[0], buffer, sizeof(buffer) - 1);
        if (bytesRead <= 0) {
            if (bytesRead < 0 && errno == EINTR) continue;
            break;
        }
        buffer[bytesRead] = '\0';
        lineBuffer += buffer;
        
        // Process complete lines
        size_t newlinePos;
        while ((newlinePos = lineBuffer.find('\n')) != std::string::npos) {
            std::string line = lineBuffer.substr(0, newlinePos);
            lineBuffer.erase(0, newlinePos + 1);
            
            // Parse MI2 response
            if (line.empty()) continue;
            
            // Check for async events
            if (line[0] == '*') {
                // Async event (stopped, running, etc.)
                if (line.find("*stopped") != std::string::npos) {
                    StoppedEvent evt;
                    // Parse reason
                    size_t reasonPos = line.find("reason=\"");
                    if (reasonPos != std::string::npos) {
                        size_t endPos = line.find('"', reasonPos + 8);
                        evt.reason = line.substr(reasonPos + 8, endPos - reasonPos - 8);
                    }
                    // Parse thread-id
                    size_t threadPos = line.find("thread-id=\"");
                    if (threadPos != std::string::npos) {
                        size_t endPos = line.find('"', threadPos + 11);
                        evt.thread_id = std::stoi(line.substr(threadPos + 11, endPos - threadPos - 11));
                    }
                    
                    // Parse frame
                    size_t framePos = line.find("frame={");
                    if (framePos != std::string::npos) {
                        size_t filePos = line.find("fullname=\"", framePos);
                        if (filePos != std::string::npos) {
                            size_t endPos = line.find('"', filePos + 10);
                            evt.text = line.substr(filePos + 10, endPos - filePos - 10);
                        }
                        size_t lineNumPos = line.find("line=\"", framePos);
                        if (lineNumPos != std::string::npos) {
                            size_t endPos = line.find('"', lineNumPos + 6);
                            evt.frame_id = std::stoi(line.substr(lineNumPos + 6, endPos - lineNumPos - 6));
                        }
                    }
                    
                    if (stopped_cb_) {
                        stopped_cb_(evt);
                    }
                }
                else if (line.find("*running") != std::string::npos) {
                    // Continue event
                }
                else if (line.find("*stopped") != std::string::npos || 
                         line.find("*running") != std::string::npos) {
                    // Already handled above
                }
                else if (line.find("=thread-exited") != std::string::npos ||
                         line.find("=thread-created") != std::string::npos) {
                    // Thread events
                }
                else if (line.find("=exit") != std::string::npos) {
                    if (exited_cb_) exited_cb_(0);
                }
            }
            else if (line[0] == '~' || line[0] == '@') {
                // Console/target output - ignore
            }
            else if (line.find("^done") != std::string::npos || 
                     line.find("^running") != std::string::npos ||
                     line.find("^connected") != std::string::npos ||
                     line.find("^exit") != std::string::npos ||
                     line.find("^error") != std::string::npos) {
                // Sync response - check if it has a token prefix
                std::cout << "GDB sync: " << line.substr(0, 80) << std::endl;
                
                // Check if this matches our pending token
                std::lock_guard<std::mutex> lock(mutex_);
                size_t tokenEnd = line.find_first_not_of("0123456789");
                if (tokenEnd != std::string::npos && tokenEnd > 0) {
                    int token = std::stoi(line.substr(0, tokenEnd));
                    if (token == pending_token_ && !response_ready_) {
                        pending_response_ = line;
                        response_ready_ = true;
                        cv_.notify_one();
                    }
                }
            }
            else if (line == "(gdb)" || line.find("(gdb)") != std::string::npos) {
                // (gdb) prompt - ignore
            }
            else {
                // Response with token
                // e.g., "1^done,..."
                size_t tokenEnd = line.find_first_not_of("0123456789");
                if (tokenEnd != std::string::npos && tokenEnd > 0) {
                    int token = std::stoi(line.substr(0, tokenEnd));
                    std::cout << "GDB response[#" << token << "]: " 
                              << line.substr(tokenEnd, 80) << std::endl;
                    
                    // Check if this matches our pending token
                    std::lock_guard<std::mutex> lock(mutex_);
                    if (token == pending_token_ && !response_ready_) {
                        pending_response_ = line;
                        response_ready_ = true;
                        cv_.notify_one();
                    }
                }
            }
        }
    }
}

bool GdbBridge::setBreakpoint(const std::string& file, int line) {
    int token = nextToken();
    std::stringstream ss;
    ss << "-break-insert -f " << file << ":" << line;
    
    std::string response = waitForResponse(token, 5000);
    
    // Check if response indicates success
    if (response.find("^done") != std::string::npos) {
        // Parse breakpoint number from response
        // Format: token^done,bkpt={number="1",...}
        size_t bkptPos = response.find("bkpt={");
        if (bkptPos != std::string::npos) {
            size_t numPos = response.find("number=\"", bkptPos);
            if (numPos != std::string::npos) {
                size_t endPos = response.find('"', numPos + 8);
                if (endPos != std::string::npos) {
                    int bpNum = std::stoi(response.substr(numPos + 8, endPos - numPos - 8));
                    std::cerr << "Breakpoint set: gdb_id=" << bpNum << " at " << file << ":" << line << std::endl;
                    return true;
                }
            }
        }
        std::cerr << "Breakpoint set (no number parsed): " << file << ":" << line << std::endl;
        return true;
    } else if (response.find("^error") != std::string::npos) {
        std::cerr << "Failed to set breakpoint at " << file << ":" << line << " - " << response << std::endl;
        return false;
    }
    
    // Timeout - return true as pending
    std::cerr << "Timeout setting breakpoint at " << file << ":" << line << std::endl;
    return true; // Assume success if no error received
}

void GdbBridge::setBreakpoint(const std::string& file, int line, int message_id) {
    setBreakpoint(file, line);
}

void GdbBridge::deleteBreakpoint(int bp_id) {
    std::stringstream ss;
    ss << "-break-delete " << bp_id;
    sendCommand(ss.str());
}

void GdbBridge::enableBreakpoints() {
    sendCommand("-break-enable");
}

void GdbBridge::run() {
    sendCommand("-exec-run");
}

void GdbBridge::stepNext() {
    sendCommand("-exec-next");
}

void GdbBridge::stepIn() {
    sendCommand("-exec-step");
}

void GdbBridge::stepOut() {
    sendCommand("-exec-finish");
}

void GdbBridge::continueExecution() {
    sendCommand("-exec-continue");
}

std::vector<int> GdbBridge::getThreads() {
    // Send thread-info command
    sendCommand("-thread-info");
    // TODO: Parse response
    return {1}; // Simplified: always return thread 1
}

std::vector<StackFrame> GdbBridge::getStackFrames(int thread_id) {
    std::vector<StackFrame> frames;
    
    std::stringstream ss;
    ss << "-thread-select " << thread_id;
    sendCommand(ss.str());
    
    ss.str("");
    ss << "-stack-list-frames 0 20";
    sendCommand(ss.str());
    
    // TODO: Parse response from GDB stdout
    // For now, return simplified frame
    StackFrame f;
    f.frame_id = 0;
    f.line = 1;
    frames.push_back(f);
    
    return frames;
}

std::vector<Variable> GdbBridge::getVariables(int frame_id) {
    std::vector<Variable> vars;
    
    sendCommand("-stack-select-frame " + std::to_string(frame_id));
    sendCommand("-stack-list-variables --values all-values");
    
    // TODO: Parse response
    return vars;
}

std::string GdbBridge::evaluate(const std::string& expression, int frame_id) {
    std::stringstream ss;
    ss << "-data-evaluate-expression \"" << expression << "\"";
    sendCommand(ss.str());
    
    // TODO: Parse response
    return "";
}

void GdbBridge::setStoppedCallback(std::function<void(const StoppedEvent&)> cb) {
    stopped_cb_ = cb;
}

void GdbBridge::setExitedCallback(std::function<void(int exit_code)> cb) {
    exited_cb_ = cb;
}