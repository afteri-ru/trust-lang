#include "debug/mi/mi_parse.hpp"

#include <cstring>
#include <string>

namespace trust::debug::detail {

namespace {

// Извлекает reason="..." из хвоста MI-записи; прочие ключи сессии не нужны.
void parseReason(const char* p, MiResponse& resp) {
    while (p && *p) {
        while (*p == ' ') {
            ++p;
        }
        if (!*p) {
            break;
        }
        const char* eq = std::strchr(p, '=');
        if (!eq) {
            break;
        }
        std::string key(p, eq - p);
        const char* val_start = eq + 1;
        if (*val_start == '"') {
            ++val_start;
            const char* val_end = std::strchr(val_start, '"');
            if (!val_end) {
                break;
            }
            if (key == "reason") {
                resp.reason = std::string(val_start, val_end - val_start);
            }
            p = val_end + 1;
        } else {
            const char* val_end = std::strchr(val_start, ',');
            if (!val_end) {
                if (key == "reason") {
                    resp.reason = std::string(val_start);
                }
                break;
            }
            if (key == "reason") {
                resp.reason = std::string(val_start, val_end - val_start);
            }
            p = val_end + 1;
        }
    }
}

} // namespace

std::optional<MiResponse> parseMiResponseLine(std::string_view line) {
    if (line.empty()) {
        return std::nullopt;
    }
    std::string storage(line);
    const char* p = storage.c_str();
    // Опциональный токен ответа ("12^done"); сессия его не использует.
    while (*p >= '0' && *p <= '9') {
        ++p;
    }

    if (*p == '*' || *p == '^') {
        MiResponse resp;
        resp.isAsync = (*p == '*');
        ++p;
        const char* comma = std::strchr(p, ',');
        if (comma) {
            resp.klass = std::string(p, comma - p);
            parseReason(comma + 1, resp);
        } else {
            resp.klass = p;
        }
        return resp;
    }
    return std::nullopt;
}

std::optional<std::string> miConsoleText(std::string_view line) {
    if (line.empty() || line.front() != '~') {
        return std::nullopt;
    }
    std::string storage(line);
    const char* p = storage.c_str() + 1;
    while (*p == ' ') {
        ++p;
    }
    if (*p != '"') {
        return std::nullopt;
    }
    ++p;
    const char* end = std::strrchr(p, '"');
    if (end == nullptr) {
        return std::nullopt;
    }
    return std::string(p, end - p);
}

std::vector<StackFrameInfo> parseStackFrames(std::string_view raw) {
    std::vector<StackFrameInfo> frames;
    std::string storage(raw);
    const char* p = storage.c_str();
    const char* stack_start = std::strstr(p, "stack=[");
    if (stack_start == nullptr) {
        return frames;
    }
    p = stack_start + 7; // skip "stack=["

    while (*p != '\0' && *p != ']') {
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        if (*p == '\0' || *p == ']') {
            break;
        }
        if (std::strncmp(p, "frame={", 7) != 0) {
            break;
        }
        p += 7;

        StackFrameInfo frame;
        while (*p != '\0' && *p != '}') {
            while (*p == ' ' || *p == '\t') {
                p++;
            }
            if (*p == '\0' || *p == '}') {
                break;
            }

            const char* eq = std::strchr(p, '=');
            if (eq == nullptr) {
                break;
            }

            std::string key(p, eq - p);
            p = eq + 1;

            std::string value;
            if (*p == '"') {
                p++;
                const char* val_end = std::strchr(p, '"');
                if (val_end == nullptr) {
                    break;
                }
                value = std::string(p, val_end - p);
                p = val_end + 1;
            } else {
                const char* val_end = std::strchr(p, ',');
                const char* brace_end = std::strchr(p, '}');
                if (val_end == nullptr && brace_end == nullptr) {
                    break;
                }
                const char* stop = val_end;
                if (brace_end != nullptr && (val_end == nullptr || brace_end < val_end)) {
                    stop = brace_end;
                }
                value = std::string(p, stop - p);
                p = stop;
            }

            if (key == "func") {
                frame.m_func = value;
            } else if (key == "file") {
                frame.m_file = value;
            } else if (key == "line") {
                frame.m_line = std::stoi(value);
            }

            if (*p == ',') {
                p++;
            }
        }

        frames.push_back(frame);

        if (*p == '}') {
            p++;
        }
        if (*p == ',') {
            p++;
        }
    }

    return frames;
}

std::string mapStopReason(std::string_view gdb_reason) {
    if (gdb_reason == "breakpoint-hit" || gdb_reason == "watchpoint-trigger" || gdb_reason == "read-watchpoint-trigger" ||
        gdb_reason == "access-watchpoint-trigger" || gdb_reason == "function-finished") {
        return "breakpoint";
    }
    if (gdb_reason == "end-stepping-range" || gdb_reason == "step-end") {
        return "step";
    }
    if (gdb_reason == "signal-received") {
        return "exception";
    }
    if (gdb_reason == "exited" || gdb_reason == "exited-normally" || gdb_reason == "exited-signalled") {
        return "exit";
    }
    return "breakpoint"; // default fallback
}

} // namespace trust::debug::detail
