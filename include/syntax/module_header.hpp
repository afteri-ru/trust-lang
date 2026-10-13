// include/syntax/module_header.hpp
// Файловый заголовок модуля: статическая первая значимая конструкция `<path>;` или
// `<path> { <интерфейс> };`.
// Путь не входит в AST: загрузчик проверяет его и передаёт парсеру начало блока интерфейса
// (или конец заголовка). Блок интерфейса разбирается обычной грамматикой.
#pragma once

#include "location/module_name.hpp"

#include <cstddef>
#include <string>
#include <string_view>

namespace trust {

struct ModuleHeaderScan {
    bool found = false;
    std::string header; ///< логический путь (без блока и без `;`)
    std::string error;
    std::size_t begin = 0; ///< первый символ пути (после BOM/trivia)
    std::size_t end = 0;   ///< конец заголовка: после `;` либо позиция `{` интерфейса
    bool hasInterface = false;
    std::size_t interfaceBegin = 0; ///< `{` интерфейса (границу блока задаёт грамматика)
};

/// Сканирует BOM/shebang/`#`/`/* */` и возвращает ДИАПАЗОН ПУТИ заголовка (и позицию `{`).
/// Содержимое блока интерфейса не сканируется: его границы задаёт грамматика (первый
/// top-level `block`), запрет макросов — `Parser::m_inFileInterface`.
/// `found=false, error.empty()` - заголовка нет (первый значимый символ не `\\`);
/// `error` непустой - заголовок начат, но некорректен.
[[nodiscard]] inline ModuleHeaderScan scanModuleHeader(std::string_view text) {
    ModuleHeaderScan result;
    const std::size_t n = text.size();
    std::size_t i = 0;
    if (n >= 3 && static_cast<unsigned char>(text[0]) == 0xEF && static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF) {
        i = 3;
    }
    auto skip_trivia = [&]() {
        while (i < n) {
            const char c = text[i];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
                ++i;
                continue;
            }
            if (c == '#') {
                while (i < n && text[i] != '\n') {
                    ++i;
                }
                continue;
            }
            if (c == '/' && i + 1 < n && text[i + 1] == '*') {
                int depth = 1;
                i += 2;
                while (i < n && depth > 0) {
                    if (text[i] == '/' && i + 1 < n && text[i + 1] == '*') {
                        ++depth;
                        i += 2;
                    } else if (text[i] == '*' && i + 1 < n && text[i + 1] == '/') {
                        --depth;
                        i += 2;
                    } else {
                        ++i;
                    }
                }
                continue;
            }
            break;
        }
    };
    skip_trivia();
    if (i >= n || text[i] != '\\') {
        return result;
    }
    result.begin = i;
    const std::size_t start = i;
    while (i < n) {
        const char c = text[i];
        if (c == ';' || c == '{') {
            break;
        }
        if (c == '(') {
            // Это выражение импорта, а не файловый заголовок.
            return ModuleHeaderScan{};
        }
        if (c == '\n' || c == '\r') {
            result.end = i;
            result.error = "module header must end with ';'";
            return result;
        }
        ++i;
    }
    std::size_t path_end = i;
    while (path_end > start && (text[path_end - 1] == ' ' || text[path_end - 1] == '\t')) {
        --path_end;
    }
    result.header = std::string(text.substr(start, path_end - start));
    result.found = true;
    if (i >= n) {
        result.end = n;
        result.error = "unterminated module header";
        return result;
    }
    if (text[i] == ';') {
        result.end = i + 1;
        return result;
    }
    // Блок интерфейса: `{ ... };` - начало фиксируем, конец определяет грамматика.
    result.hasInterface = true;
    result.interfaceBegin = i;
    result.end = i;
    return result;
}

/// Кодирование логического пути в идентификатор и имя entry-функции — ЕДИНАЯ формула в
/// `location/module_name.hpp` (один encoder для @__MODULE_NAME__, entry и модульной изоляции).

/// Заменяет путь заголовка пробелами (кроме переводов строк), сохраняя номера строк.
/// Блок интерфейса НЕ маскируется: его разбирает парсер.
inline void maskModuleHeader(std::string& source) {
    const ModuleHeaderScan header = scanModuleHeader(source);
    if (!header.found) {
        return;
    }
    for (std::size_t i = header.begin; i < header.begin + header.header.size(); ++i) {
        if (source[i] != '\n' && source[i] != '\r') {
            source[i] = ' ';
        }
    }
}

} // namespace trust
