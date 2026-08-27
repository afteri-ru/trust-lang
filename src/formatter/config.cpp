#include "formatter/format.hpp"

#include "utils/file_io.hpp"

#include <cctype>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace trust::formatter {

namespace {

// Разбивает строку `Key: Value` на ключ и значение. Пустая строка/комментарий/`---` → {false,..}.
struct ParsedLine {
    bool ok = false;
    std::string key;
    std::string value;
};

ParsedLine splitKeyValue(std::string_view line) {
    ParsedLine out;
    size_t i = 0;
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) {
        ++i;
    }
    if (i >= line.size()) {
        return out; // пустая строка
    }
    if (line[i] == '#' || (line[i] == '-' && i + 1 < line.size() && line[i + 1] == '-')) {
        return out; // комментарий / `---`
    }
    const size_t colon = line.find(':', i);
    if (colon == std::string_view::npos) {
        out.key = "?";
        out.value = std::string(line.substr(i));
        out.ok = true; // ошибку ключа обработаем по отсутствию ':'
        return out;
    }
    out.key = std::string(line.substr(i, colon - i));
    size_t vs = colon + 1;
    while (vs < line.size() && (line[vs] == ' ' || line[vs] == '\t')) {
        ++vs;
    }
    size_t ve = line.size();
    while (ve > vs && (line[ve - 1] == ' ' || line[ve - 1] == '\t' || line[ve - 1] == '\r')) {
        --ve;
    }
    out.value = std::string(line.substr(vs, ve - vs));
    out.ok = true;
    return out;
}

bool parseBool(const std::string& v, bool& out) {
    if (v == "true" || v == "True" || v == "TRUE" || v == "Always") {
        out = true;
        return true;
    }
    if (v == "false" || v == "False" || v == "FALSE" || v == "Never") {
        out = false;
        return true;
    }
    return false;
}

bool parseInt(const std::string& v, int& out) {
    if (v.empty()) {
        return false;
    }
    for (char c : v) {
        if (!std::isdigit(static_cast<unsigned char>(c)) && c != '-') {
            return false;
        }
    }
    try {
        out = std::stoi(v);
        return true;
    } catch (...) {
        return false;
    }
}

// Список имён через запятую (без пробелов; допускается ведущий '@').
void parseKeywordList(const std::string& value, std::vector<std::string>& out) {
    size_t i = 0;
    while (i < value.size()) {
        const size_t j = value.find(',', i);
        const size_t end = (j == std::string::npos) ? value.size() : j;
        std::string entry = value.substr(i, end - i);
        size_t s = 0;
        while (s < entry.size() && (entry[s] == ' ' || entry[s] == '\t')) {
            ++s;
        }
        size_t e = entry.size();
        while (e > s && (entry[e - 1] == ' ' || entry[e - 1] == '\t')) {
            --e;
        }
        entry = entry.substr(s, e - s);
        if (!entry.empty()) {
            out.push_back(std::move(entry));
        }
        i = (j == std::string::npos) ? value.size() : j + 1;
    }
}

} // namespace

FormatConfig parseConfig(std::string_view text) {
    FormatConfig cfg;
    size_t pos = 0;
    while (pos < text.size()) {
        const size_t nl = text.find('\n', pos);
        const size_t end = (nl == std::string_view::npos) ? text.size() : nl;
        const std::string_view line = text.substr(pos, end - pos);
        if (nl == std::string_view::npos) {
            pos = text.size(); // последняя строка без перевода — заканчиваем после неё
        } else {
            pos = nl + 1;
        }

        if (line.empty()) {
            continue;
        }
        const ParsedLine pl = splitKeyValue(line);
        if (!pl.ok) {
            continue; // комментарий/пусто/`---`
        }
        if (pl.key == "?") {
            cfg.ok = false;
            cfg.error = "malformed line (expected 'Key: Value'): " + pl.value;
            return cfg;
        }

        if (pl.key == "IndentWidth") {
            if (!parseInt(pl.value, cfg.opts.tab_size) || cfg.opts.tab_size < 1) {
                cfg.ok = false;
                cfg.error = "IndentWidth: expected positive integer, got '" + pl.value + "'";
                return cfg;
            }
        } else if (pl.key == "UseTabs") {
            bool useTabs = false;
            if (!parseBool(pl.value, useTabs)) {
                cfg.ok = false;
                cfg.error = "UseTabs: expected true/false (or Always/Never), got '" + pl.value + "'";
                return cfg;
            }
            cfg.opts.use_spaces = !useTabs; // UseTabs=true → отступ табами (не пробелами)
        } else if (pl.key == "ColumnLimit") {
            if (!parseInt(pl.value, cfg.opts.max_line_width) || cfg.opts.max_line_width < 0) {
                cfg.ok = false;
                cfg.error = "ColumnLimit: expected non-negative integer, got '" + pl.value + "'";
                return cfg;
            }
        } else if (pl.key == "InsertFinalNewline") {
            if (!parseBool(pl.value, cfg.opts.insert_final_newline)) {
                cfg.ok = false;
                cfg.error = "InsertFinalNewline: expected true/false, got '" + pl.value + "'";
                return cfg;
            }
        } else if (pl.key == "Keywords") {
            parseKeywordList(pl.value, cfg.opts.keywords);
        } else {
            cfg.ok = false;
            cfg.error = "unknown format option '" + pl.key + "'";
            return cfg;
        }
    }
    cfg.ok = true;
    return cfg;
}

FormatConfig loadConfig(const std::string& path) {
    auto data = trust::utils::FileIO::read<std::vector<char>>(path);
    if (!data) {
        FormatConfig cfg;
        cfg.ok = false;
        cfg.error = "cannot read config file: " + path;
        return cfg;
    }
    std::string text(data->data(), data->size());
    FormatConfig cfg = parseConfig(text);
    if (!cfg.ok) {
        cfg.error = path + ": " + cfg.error;
    }
    return cfg;
}

std::string findConfig(std::string_view startDir) {
    namespace fs = std::filesystem;
    fs::path dir(startDir.empty() ? fs::current_path() : fs::path(startDir));
    if (!fs::exists(dir)) {
        dir = dir.parent_path();
    }
    fs::path current = fs::absolute(dir);
    while (!current.empty()) {
        const fs::path candidate = current / ".trust-format";
        if (fs::is_regular_file(candidate)) {
            return candidate.string();
        }
        if (current == current.parent_path()) {
            break;
        }
        current = current.parent_path();
    }
    return {};
}

std::string dumpConfig(const FormatOptions& opts) {
    std::string out;
    out += "# Trust format configuration (.trust-format)\n";
    out += "# All options shown with their effective (or default) values.\n";
    out += "---\n";
    out += "# Width of the indentation, in spaces.\n";
    out += "IndentWidth: " + std::to_string(opts.tab_size) + "\n";
    out += "# Indent with tabs instead of spaces: true / false / Always / Never.\n";
    out += "UseTabs: " + std::string(opts.use_spaces ? "false" : "true") + "\n";
    out += "# Maximum line width for inline lists (0 = no limit).\n";
    out += "ColumnLimit: " + std::to_string(opts.max_line_width) + "\n";
    out += "# Ensure a single trailing newline at the end of the file.\n";
    out += "InsertFinalNewline: " + std::string(opts.insert_final_newline ? "true" : "false") + "\n";
    out += "# Macro names allowed without the '@' sigil (comma-separated, no spaces).\n";
    out += "Keywords:";
    bool first = true;
    for (const auto& k : opts.keywords) {
        out += first ? " " : ",";
        out += k;
        first = false;
    }
    out += "\n";
    return out;
}

std::vector<std::string> splitKeywordList(std::string_view list) {
    std::vector<std::string> out;
    parseKeywordList(std::string(list), out);
    return out;
}

} // namespace trust::formatter
