#pragma once

// Чистый разбор строк GDB/MI (без процессов и состояния сессии): записи ^/*/~/,
// кадры стека (-stack-list-frames) и маппинг stop reason в DAP-совместимые значения.

#include "debug/gdb/stack_frame.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace trust::debug::detail {

/// Разобранная строка GDB/MI: `^done,...` либо `*stopped,...`.
struct MiResponse {
    std::string klass;    ///< "done", "running", "error", "stopped"
    std::string reason;   ///< e.g. "breakpoint-hit", "exited", "end-stepping-range"
    std::string raw;      ///< full raw line
    bool isAsync = false; ///< true if *stopped / *running
};

/// Разбирает строку MI-записи. nullopt - строка не является ^/*-записью
/// (консольный вывод `~`, потоковые `=`/`&`/`@`, пустая/битая строка).
[[nodiscard]] std::optional<MiResponse> parseMiResponseLine(std::string_view line);

/// Текст консольного вывода из `~"..."` (без кавычек). nullopt - строка не консольная.
[[nodiscard]] std::optional<std::string> miConsoleText(std::string_view line);

/// Разбирает `^done,stack=[frame={...},...]` в кадры. Пустой/невалидный ввод - пустой вектор.
[[nodiscard]] std::vector<StackFrameInfo> parseStackFrames(std::string_view raw);

/// GDB/MI stop reason -> DAP-совместимая строка ("breakpoint" | "step" | "exception" | "exit").
[[nodiscard]] std::string mapStopReason(std::string_view gdb_reason);

} // namespace trust::debug::detail
