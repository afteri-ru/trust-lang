#include "lsp/transpile.h"
#include "diag/diag.hpp"
#include "utils/strings.hpp"

#include <cctype>
#include <charconv>
#include <filesystem>
#include <format>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace trust {
namespace lsp {
namespace {
using trust::utils::is_number;
using trust::utils::tokenize;
using trust::utils::trim;

// Диагностическая категория для ошибок синтаксиса в транспиляторе
static constexpr OptKind kSyntaxError = OptKind::ParseError;

// Replaces known variable names with cpp_<name> prefix in expression text
std::string replace_vars(std::string_view expr, const std::unordered_set<std::string>& vars) {
    std::string result;
    size_t i = 0;
    while (i < expr.size()) {
        if (std::isalpha(static_cast<unsigned char>(expr[i])) || expr[i] == '_') {
            size_t start = i;
            while (i < expr.size() && (std::isalnum(static_cast<unsigned char>(expr[i])) || expr[i] == '_'))
                ++i;
            std::string_view word = expr.substr(start, i - start);
            if (vars.find(std::string(word)) != vars.end())
                result += "cpp_" + std::string(word);
            else
                result.append(word.data(), word.size());
        } else {
            result += expr[i];
            ++i;
        }
    }
    return result;
}

// ── пара (statement, offset от начала raw_line) ──
using StmtSlot = std::pair<std::string_view, size_t>;

// ── тряска строки (выделение statement'ов через ';') ──
// Возвращает вектор (trimmed statement, offset от начала raw_line)
std::vector<StmtSlot> split_statements(std::string_view raw_line) {
    std::vector<StmtSlot> stmts;
    size_t pos = 0;
    while (pos < raw_line.size()) {
        auto sc = raw_line.find(';', pos);
        if (sc == std::string::npos) {
            std::string_view seg = trim(raw_line.substr(pos));
            if (!seg.empty())
                stmts.emplace_back(seg, seg.data() - raw_line.data());
            break;
        }
        std::string_view seg = trim(raw_line.substr(pos, sc - pos));
        if (!seg.empty())
            stmts.emplace_back(seg, seg.data() - raw_line.data());
        pos = sc + 1;
    }
    return stmts;
}

// ── Обработать один statement (create/print/assign/if/while) в C++ код ──
// Возвращает C++ текст (без точки с запятой) или пустую строку, если statement не распознан
std::string process_statement(std::string_view stmt, std::unordered_set<std::string>& declared_vars) {
    auto tokens = tokenize(stmt);
    if (tokens.empty())
        return {};

    if (tokens[0] == "create") {
        if (tokens.size() < 4 || tokens[2] != "=") {
            return {};
        } else {
            std::string var_name(tokens[1]);
            std::string rhs;
            for (size_t j = 3; j < tokens.size(); ++j) {
                if (j > 3)
                    rhs += " ";
                rhs.append(tokens[j].data(), tokens[j].size());
            }
            rhs = replace_vars(rhs, declared_vars);
            declared_vars.insert(var_name);
            return "int cpp_" + var_name + " = " + rhs;
        }
    } else if (tokens[0] == "print") {
        std::string cout_line = "std::cout";
        for (size_t j = 1; j < tokens.size(); ++j) {
            std::string_view arg = tokens[j];
            if (!is_number(arg) && declared_vars.find(std::string(arg)) != declared_vars.end())
                cout_line += " << cpp_" + std::string(arg);
            else
                cout_line += " << " + std::string(arg);
        }
        return cout_line;
    } else if (tokens[0] == "while") {
        // while COND { ... } — однострочный while
        // COND — числовой литерал или имя переменной → static_cast<bool>(cpp_<var>)
        if (tokens.size() < 4 || tokens[2] != "{") {
            return {};
        }
        // ── Condition ──
        std::string condition(tokens[1]);
        if (declared_vars.find(condition) != declared_vars.end()) {
            condition = "cpp_" + condition;
        } else if (!is_number(condition)) {
            return {};
        }
        std::string cond_str = "static_cast<bool>(" + condition + ")";

        // ── Тело цикла: tokens[3]..end (до закрывающей } или до конца) ──
        // Ищем закрывающую }
        size_t close_brace = tokens.size();
        for (size_t j = 3; j < tokens.size(); ++j) {
            if (tokens[j] == "}") {
                close_brace = j;
                break;
            }
        }
        if (close_brace == 3) {
            return {}; // пустое тело
        }

        std::string body;
        for (size_t j = 3; j < close_brace; ++j) {
            if (j > 3)
                body += " ";
            body.append(tokens[j].data(), tokens[j].size());
        }

        // Тело может содержать print
        std::string body_cpp;
        if (body.rfind("print", 0) == 0) {
            // Это print
            auto body_tokens = tokenize(body);
            body_cpp = "std::cout";
            for (size_t j = 1; j < body_tokens.size(); ++j) {
                std::string_view arg = body_tokens[j];
                if (!is_number(arg) && declared_vars.find(std::string(arg)) != declared_vars.end())
                    body_cpp += " << cpp_" + std::string(arg);
                else
                    body_cpp += " << " + std::string(arg);
            }
        } else {
            // Просто выражение (возможно, с cpp_-префиксом)
            body_cpp = replace_vars(body, declared_vars);
        }

        return "while (" + cond_str + ") { " + body_cpp + "; }";
    } else if (tokens[0] == "if") {
        // if COND then ACTION [else ACTION]
        // COND — числовой литерал или имя переменной → static_cast<bool>(cpp_<var>)
        // ACTION — print <args>
        if (tokens.size() < 4 || tokens[2] != "then") {
            return {};
        }

        // ── Condition ──
        std::string condition(tokens[1]);
        if (declared_vars.find(condition) != declared_vars.end()) {
            condition = "cpp_" + condition;
        } else if (!is_number(condition)) {
            return {}; // не число и не объявленная переменная
        }
        std::string cond_str = "static_cast<bool>(" + condition + ")";

        // ── Найти "else" ──
        size_t else_idx = tokens.size();
        for (size_t j = 3; j < tokens.size(); ++j) {
            if (tokens[j] == "else") {
                else_idx = j;
                break;
            }
        }

        // ── Then-ветка (tokens[3]..else_idx-1) — должен быть print ──
        if (else_idx <= 3 || tokens[3] != "print") {
            return {};
        }
        std::string then_code = "std::cout";
        for (size_t j = 4; j < else_idx; ++j) {
            std::string_view arg = tokens[j];
            if (!is_number(arg) && declared_vars.find(std::string(arg)) != declared_vars.end())
                then_code += " << cpp_" + std::string(arg);
            else
                then_code += " << " + std::string(arg);
        }

        if (else_idx < tokens.size()) {
            // ── Else-ветка (else_idx+1..end) — должен быть print ──
            if (else_idx + 1 >= tokens.size() || tokens[else_idx + 1] != "print") {
                return {};
            }
            std::string else_code = "std::cout";
            for (size_t j = else_idx + 2; j < tokens.size(); ++j) {
                std::string_view arg = tokens[j];
                if (!is_number(arg) && declared_vars.find(std::string(arg)) != declared_vars.end())
                    else_code += " << cpp_" + std::string(arg);
                else
                    else_code += " << " + std::string(arg);
            }
            return "if (" + cond_str + ") { " + then_code + "; } else { " + else_code + "; }";
        } else {
            return "if (" + cond_str + ") { " + then_code + "; }";
        }
    } else { // assignment
        if (tokens.size() < 3 || tokens[1] != "=") {
            return {};
        } else {
            std::string var_name(tokens[0]);
            std::string rhs;
            for (size_t j = 2; j < tokens.size(); ++j) {
                if (j > 2)
                    rhs += " ";
                rhs.append(tokens[j].data(), tokens[j].size());
            }

            if (declared_vars.find(var_name) == declared_vars.end()) {
                return {};
            } else {
                rhs = replace_vars(rhs, declared_vars);
                return "cpp_" + var_name + " = " + rhs;
            }
        }
    }
}

} // anonymous namespace

// ── Транспиляция мини-языка в C++ ──
//
// Потоково обрабатывает trustCode строка за строкой, без предварительного
// разбиения на массив строк. Использует Context::makeLoc/makeRange,
// mapStart/mapStop для построения source map (trust ↔ cpp).
// Ошибки сообщаются через ctx.report() (через OptKind::ParseError).
// Возвращает пару (trustIdx, cppIdx) всегда — даже при ошибках.
std::pair<MapperFile, MapperFile> transpile(std::string_view trustCode, std::string_view trustFileName, std::string_view cppFileName, Context& ctx) {
    // Регистрируем входной и выходной файлы
    auto trustIdx = ctx.add_source(std::string(trustFileName), std::string(trustCode));
    auto cppIdx = ctx.add_output(std::string(cppFileName));

    // Преамбула: сначала выводим preamble в m_source, чтобы mapStart получил корректный offset
    ctx.output_append(cppIdx, "#include <iostream>\n\nint main() {\n");

    std::unordered_set<std::string> declared_vars;
    std::unordered_map<std::string, std::string> macros;
    std::unordered_map<std::string, MapperRange> macroDefRanges; // имя → range в trust-файле (body)

    // Потоковая обработка: идём по trustCode символам
    size_t line_start = 0; // offset начала текущей строки (0-based)
    for (size_t i = 0; i <= trustCode.size(); ++i) {
        if (i == trustCode.size() || trustCode[i] == '\n') {
            // Строка от line_start до i (i — позиция \n или конец)
            std::string_view raw_line(trustCode.data() + line_start, i - line_start);
            std::string_view line = trim(raw_line);

            // ── Проверка, является ли строка определением макроса ──
            bool is_macro_def = false;
            if (!line.empty() && line.size() >= 5 && line.substr(0, 5) == "macro" && (line.size() == 5 || line[5] == ' ' || line[5] == '\t')) {
                is_macro_def = true;
                // ── Определение макроса: macro имя <любой текст до конца строки> ──
                auto tokens = tokenize(line);
                if (tokens.size() < 2) {
                    ctx.report(ctx.makeRange(MapperLocation::makeLoc(trustIdx, static_cast<uint32_t>(line_start + 1)),
                                             MapperLocation::makeLoc(trustIdx, static_cast<uint32_t>(i + 1))),
                               kSyntaxError, "macro definition requires a name");
                    line_start = i + 1;
                    continue;
                }
                std::string macro_name(tokens[1]);
                size_t name_start = 5;
                while (name_start < line.size() && (line[name_start] == ' ' || line[name_start] == '\t'))
                    ++name_start;
                size_t after_name = name_start + macro_name.size();
                std::string macro_body;
                if (after_name < line.size()) {
                    size_t body_start = after_name;
                    while (body_start < line.size() && (line[body_start] == ' ' || line[body_start] == '\t'))
                        ++body_start;
                    macro_body = std::string(line.substr(body_start));
                }
                macros[macro_name] = macro_body;

                // Сохраняем range определения макроса (тело) для addMacroMapping
                if (!macro_body.empty()) {
                    size_t bodyOffset = line.find(macro_body);
                    if (bodyOffset != std::string::npos) {
                        uint32_t bodyBegin = static_cast<uint32_t>(line_start + bodyOffset);
                        uint32_t bodyEnd = static_cast<uint32_t>(bodyBegin + macro_body.size());
                        macroDefRanges[macro_name] = ctx.makeRange(MapperLocation::makeLoc(trustIdx, bodyBegin), MapperLocation::makeLoc(trustIdx, bodyEnd));
                    }
                }
            }

            if (!line.empty() && line[0] != '#' && !is_macro_def) {
                // ── Обычная строка (не макрос, не комментарий) ──
                auto stmts = split_statements(raw_line);
                if (!stmts.empty()) {
                    struct StmtEntry {
                        size_t trustOffset;    // offset от начала trust-строки
                        size_t trustLen;       // длина trust-части
                        std::string cppText;   // C++ код без ";\n"
                        bool hasNameMapping;   // true если нужно addNameMapping для этой записи
                        std::string trustName; // trust-имя для NameMapping
                        std::string cppName;   // cpp-имя для NameMapping
                    };
                    std::vector<StmtEntry> entries;

                    for (const auto& slot : stmts) {
                        auto stmt = slot.first;
                        size_t stmtOffset = slot.second;

                        if (stmt.empty())
                            continue;

                        // ── Проверка: является ли первый токен именем макроса? ──
                        auto tokens = tokenize(stmt);
                        if (tokens.empty())
                            continue;

                        std::string cpp_code;

                        if (macros.find(std::string(tokens[0])) != macros.end()) {
                            // Использование макроса — подставляем тело
                            const std::string& macro_name = std::string(tokens[0]);
                            const std::string& macro_body = macros[macro_name];
                            auto sub_stmts = split_statements(macro_body);
                            for (const auto& sub_slot : sub_stmts) {
                                auto sub_code = process_statement(sub_slot.first, declared_vars);
                                if (!sub_code.empty()) {
                                    entries.push_back({stmtOffset, stmt.size(), sub_code, false, "", ""});
                                } else {
                                    uint32_t stmtBegin = static_cast<uint32_t>(line_start + stmtOffset) + 1;
                                    uint32_t stmtEnd = stmtBegin + static_cast<uint32_t>(stmt.size());
                                    ctx.report(ctx.makeRange(MapperLocation::makeLoc(trustIdx, stmtBegin), MapperLocation::makeLoc(trustIdx, stmtEnd)),
                                               kSyntaxError, "invalid statement in macro '{}': '{}'", macro_name, sub_slot.first);
                                }
                            }

                            // addMacroMapping: связываем тело макроса (trust range всего statement)
                            // с определением макроса
                            auto defIt = macroDefRanges.find(macro_name);
                            if (defIt != macroDefRanges.end()) {
                                uint32_t bodyBegin = static_cast<uint32_t>(line_start + stmtOffset);
                                uint32_t bodyEnd = static_cast<uint32_t>(bodyBegin + stmt.size());
                                MapperRange bodyRange = ctx.makeRange(MapperLocation::makeLoc(trustIdx, bodyBegin), MapperLocation::makeLoc(trustIdx, bodyEnd));
                                ctx.addMacroMapping(bodyRange, defIt->second);
                            }
                        } else {
                            // Обычный create/print/assign/if/while
                            cpp_code = process_statement(stmt, declared_vars);
                            if (!cpp_code.empty()) {
                                // Определяем, нужно ли addNameMapping для create/assign
                                bool hasNameMapping = false;
                                std::string trustName;
                                std::string cppName;
                                if (tokens[0] == "create") {
                                    hasNameMapping = true;
                                    trustName = std::string(tokens[1]);
                                    cppName = "cpp_" + trustName;
                                } else if (tokens.size() >= 3 && tokens[1] == "=" && declared_vars.find(std::string(tokens[0])) != declared_vars.end()) {
                                    hasNameMapping = true;
                                    trustName = std::string(tokens[0]);
                                    cppName = "cpp_" + trustName;
                                }
                                entries.push_back({stmtOffset, stmt.size(), std::move(cpp_code), hasNameMapping, trustName, cppName});
                            } else {
                                // Сообщаем об ошибке через diag
                                uint32_t stmtBegin = static_cast<uint32_t>(line_start + stmtOffset) + 1;
                                uint32_t stmtEnd = stmtBegin + static_cast<uint32_t>(stmt.size());

                                if (tokens[0] == "macro") {
                                    ctx.report(ctx.makeRange(MapperLocation::makeLoc(trustIdx, stmtBegin), MapperLocation::makeLoc(trustIdx, stmtEnd)),
                                               kSyntaxError, "macro definition must start at the beginning of the line");
                                } else if (tokens.size() >= 3 && tokens[1] == "=" && declared_vars.find(std::string(tokens[0])) == declared_vars.end()) {
                                    ctx.report(ctx.makeRange(MapperLocation::makeLoc(trustIdx, stmtBegin), MapperLocation::makeLoc(trustIdx, stmtEnd)),
                                               kSyntaxError, "undeclared variable '{}'", std::string(tokens[0]));
                                } else {
                                    ctx.report(ctx.makeRange(MapperLocation::makeLoc(trustIdx, stmtBegin), MapperLocation::makeLoc(trustIdx, stmtEnd)),
                                               kSyntaxError, "invalid syntax: '{}'", std::string(stmt));
                                }
                            }
                        }
                    }

                    if (entries.empty())
                        continue;

                    // ── Выводим каждый statement отдельно ──
                    for (size_t j = 0; j < entries.size(); ++j) {
                        bool last = (j + 1 == entries.size());

                        int trustBegin = static_cast<int>(line_start + entries[j].trustOffset);
                        int trustEnd = trustBegin + static_cast<int>(entries[j].trustLen);

                        MapperRange trustRange = ctx.mapStart(trustIdx, trustBegin + 1, trustEnd + 1, cppIdx);
                        ctx.output_append(cppIdx, entries[j].cppText);
                        auto cpp_map = ctx.mapStop(trustRange);
                        (void)cpp_map;

                        // Если нужно добавить NameMapping для create/assign
                        if (entries[j].hasNameMapping) {
                            // trustRange — это input range, cpp_map — output range
                            // Добавляем упрощённое NameMapping на весь range
                            ctx.addNameMapping(trustRange, cpp_map, entries[j].trustName, entries[j].cppName);
                        }

                        ctx.output_append(cppIdx, ";");
                        ctx.output_append(cppIdx, last ? "\n" : " ");
                    }
                }
            }

            // Переходим к следующей строке
            line_start = i + 1;
        }
    }

    // Закрывающие строки
    ctx.output_append(cppIdx, "return 0;\n}\n");

    return std::make_pair(trustIdx, cppIdx);
}
} // namespace lsp
} // namespace trust