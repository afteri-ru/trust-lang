#include "debug/mi/mi_transport.hpp"

#include <cerrno>
#include <chrono>

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

namespace trust::debug::detail {

MiTransport::~MiTransport() {
    close();
}

bool MiTransport::start(const std::string& gdb_path) {
    int stdin_pipe[2];
    int stdout_pipe[2];

    if (pipe(stdin_pipe) < 0 || pipe(stdout_pipe) < 0) {
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
        ::close(stdin_pipe[1]);
        ::close(stdout_pipe[0]);

        if (dup2(stdin_pipe[0], STDIN_FILENO) < 0) {
            _exit(1);
        }
        if (dup2(stdout_pipe[1], STDOUT_FILENO) < 0) {
            _exit(1);
        }

        for (int i = 3; i < 1024; ++i) {
            ::close(i);
        }

        execlp(gdb_path.c_str(), gdb_path.c_str(), "--interpreter=mi2", "-q", nullptr);
        // If execlp fails, try execvp with full path search via PATH
        {
            const char* argv[4] = {gdb_path.c_str(), "--interpreter=mi2", "-q", nullptr};
            execvp(gdb_path.c_str(), const_cast<char* const*>(argv));
        }
        perror("execlp/execvp gdb");
        _exit(1);
    }

    // Parent
    ::close(stdin_pipe[0]);
    ::close(stdout_pipe[1]);
    m_stdin_fd = stdin_pipe[1];
    m_stdout_fd = stdout_pipe[0];

    int flags = fcntl(m_stdout_fd, F_GETFL, 0);
    fcntl(m_stdout_fd, F_SETFL, flags | O_NONBLOCK);

    drainOutput(100);
    return true;
}

int MiTransport::sendCommand(const std::string& cmd) {
    int token = m_seq++;
    std::string line = std::to_string(token) + cmd + "\n";
    if (write(m_stdin_fd, line.data(), line.size()) < 0) {
        return -1;
    }
    return token;
}

std::string MiTransport::readLine(int timeout_ms) {
    auto newline_pos = m_buffer.find('\n');
    if (newline_pos != std::string::npos) {
        std::string line = m_buffer.substr(0, newline_pos);
        m_buffer.erase(0, newline_pos + 1);
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        return line;
    }

    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
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
                newline_pos = m_buffer.find('\n');
                if (newline_pos != std::string::npos) {
                    std::string line = m_buffer.substr(0, newline_pos);
                    m_buffer.erase(0, newline_pos + 1);
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

void MiTransport::drainOutput(int max_ms) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(max_ms);
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

void MiTransport::close() {
    if (m_child_pid > 0) {
        kill(m_child_pid, SIGTERM);
        int status = 0;
        waitpid(m_child_pid, &status, 0);
        m_child_pid = 0;
    }
    if (m_stdin_fd >= 0) {
        ::close(m_stdin_fd);
        m_stdin_fd = -1;
    }
    if (m_stdout_fd >= 0) {
        ::close(m_stdout_fd);
        m_stdout_fd = -1;
    }
}

} // namespace trust::debug::detail
