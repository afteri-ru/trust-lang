#pragma once

// include/debug/mi/mi_transport.hpp
// Транспорт GDB/MI: запуск gdb --interpreter=mi2, запись команд с токенами и
// построчное чтение ответов. Не знает ни о DAP, ни о trust-lang.

#include <string>

#include <sys/types.h>

namespace trust::debug::detail {

class MiTransport {
  public:
    MiTransport() = default;
    MiTransport(const MiTransport&) = delete;
    MiTransport& operator=(const MiTransport&) = delete;
    ~MiTransport();

    /// Запускает GDB/MI-подпроцесс. false - pipe/fork/exec не удались.
    bool start(const std::string& gdb_path);

    /// Отправляет команду с очередным токеном; возвращает токен или -1 при ошибке записи.
    int sendCommand(const std::string& cmd);

    /// Читает одну строку (без '\r'/'\n'). Пустая строка - таймаут/EOF.
    std::string readLine(int timeout_ms);

    /// Закрывает подпроцесс и файловые дескрипторы (идемпотентно).
    void close();

    [[nodiscard]] int stdoutFd() const noexcept { return m_stdout_fd; }

  private:
    /// Вычитывает и отбрасывает доступный вывод в течение max_ms (стартовый drain).
    void drainOutput(int max_ms);

    pid_t m_child_pid = 0;
    int m_stdin_fd = -1;
    int m_stdout_fd = -1;
    int m_seq = 1;
    std::string m_buffer; ///< partial line buffer
};

} // namespace trust::debug::detail
