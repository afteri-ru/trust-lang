#ifndef GDB_BRIDGE_H
#define GDB_BRIDGE_H

#include <string>
#include <vector>
#include <map>
#include <functional>
#include <mutex>
#include <optional>
#include <condition_variable>
#include <thread>

struct StackFrame {
    int frame_id;
    std::string file;
    int line;
    std::string function;
    std::string address;
};

struct Variable {
    std::string name;
    std::string value;
    std::string type;
    int variables_reference;
};

struct Breakpoint {
    int id;
    std::string file;
    int line;
    bool verified;
    int message_id;  // DAP message seq for response
};

struct StoppedEvent {
    std::string reason;       // "breakpoint", "step", "exception"
    int thread_id;
    std::string text;
    int frame_id;
};

class GdbBridge {
public:
    GdbBridge();
    ~GdbBridge();

    // Управление GDB процессом
    bool start(const std::string& programPath);
    void stop();
    
    // Команды отладки
    void pause(); // Прервать выполнение (exec-interrupt)
    bool setBreakpoint(const std::string& file, int line);
    void setBreakpoint(const std::string& file, int line, int message_id);  // Legacy
    void deleteBreakpoint(int bp_id);
    void enableBreakpoints();
    
    void run();
    void stepNext();
    void stepIn();
    void stepOut();
    void continueExecution();
    
    // Получение информации
    std::vector<int> getThreads();
    std::vector<StackFrame> getStackFrames(int thread_id);
    std::vector<Variable> getVariables(int frame_id);
    std::string evaluate(const std::string& expression, int frame_id);
    
    // Callback при событиях
    void setStoppedCallback(std::function<void(const StoppedEvent&)> cb);
    void setExitedCallback(std::function<void(int exit_code)> cb);
    
private:
    // Внутренние методы
    int nextToken();
    void sendCommand(const std::string& cmd);
    void sendCommand(int token, const std::string& cmd);
    std::string waitForResponse(int token, int timeoutMs = 5000);
    void readLoop();
    
    std::string parseMiResponse(const std::string& response);
    std::vector<StackFrame> parseStackFrames(const std::string& response);
    std::vector<Variable> parseVariables(const std::string& response);
    
    // Pipes и процесс
    int gdb_stdin_pipe_[2] = {-1, -1};
    int gdb_stdout_pipe_[2] = {-1, -1};
    int gdb_stderr_pipe_[2] = {-1, -1};
    pid_t gdb_pid_ = -1;
    
    std::string program_path_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::thread read_thread_;
    bool running_ = false;
    
    // Callbacks
    std::function<void(const StoppedEvent&)> stopped_cb_;
    std::function<void(int exit_code)> exited_cb_;
    
    // Breakpoint tracking
    int next_bp_id_ = 1;
    std::map<int, Breakpoint> breakpoints_; // gdb_bp_id -> Breakpoint
    
    // Response buffer
    std::string response_buffer_;
    
    // Synchronization for command responses
    int pending_token_ = -1;
    std::string pending_response_;
    bool response_ready_ = false;
    
    // Token counter
    int token_counter_ = 1;
};

#endif // GDB_BRIDGE_H