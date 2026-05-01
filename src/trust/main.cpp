#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <map>
#include <vector>
#include <regex>

struct LineMapping {
    int trust_line;
    int cpp_line;
    std::vector<std::string> trust_vars;
    std::vector<std::string> cpp_vars;
};

struct SourceEntry {
    std::string trust_file;
    std::string cpp_file;
    std::vector<LineMapping> mappings;
};

// JSON escaping helper
std::string json_escape(const std::string& s) {
    std::string result;
    for (char c : s) {
        switch (c) {
            case '"': result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\n': result += "\\n"; break;
            case '\t': result += "\\t"; break;
            case '\r': result += "\\r"; break;
            default: result += c;
        }
    }
    return result;
}

// Parse a single DSL variable/token
std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    size_t end = s.find_last_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    return s.substr(start, end - start + 1);
}

// Extract variable names from a DSL expression, ignoring string literals
std::vector<std::string> extract_variables(const std::string& expr) {
    std::vector<std::string> vars;
    // Remove string literals first
    std::string cleaned;
    bool in_string = false;
    char string_char = 0;
    bool escaped = false;
    for (size_t i = 0; i < expr.size(); ++i) {
        char c = expr[i];
        if (escaped) { escaped = false; continue; }
        if (c == '\\' && in_string) { escaped = true; continue; }
        if ((c == '"' || c == '\'') && !in_string) { in_string = true; string_char = c; cleaned += ' '; continue; }
        if (c == string_char && in_string) { in_string = false; cleaned += ' '; continue; }
        if (in_string) { cleaned += ' '; continue; }
        cleaned += c;
    }
    
    std::regex var_regex("[a-zA-Z_][a-zA-Z0-9_]*");
    auto begin = std::sregex_iterator(cleaned.begin(), cleaned.end(), var_regex);
    auto end = std::sregex_iterator();
    for (auto it = begin; it != end; ++it) {
        const std::string& s = it->str();
        // Skip known function names and keywords
        if (s != "create" && s != "call" && s != "printf" && s != "int" && s != "double" && s != "float" && s != "char") {
            bool found = false;
            for (const auto& v : vars) {
                if (v == s) { found = true; break; }
            }
            if (!found) {
                vars.push_back(s);
            }
        }
    }
    return vars;
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <input.trust> <output.cpp> [output.map.json]" << std::endl;
        return 1;
    }

    std::string trust_file = argv[1];
    std::string cpp_file = argv[2];
    std::string map_json_file;
    if (argc >= 4) {
        map_json_file = argv[3];
    } else {
        map_json_file = "map.json";
    }

    // Read Trust file
    std::ifstream infile(trust_file);
    if (!infile.is_open()) {
        std::cerr << "Error: Cannot open " << trust_file << std::endl;
        return 1;
    }

    std::vector<std::string> trust_lines;
    std::string line;
    while (std::getline(infile, line)) {
        trust_lines.push_back(line);
    }
    infile.close();

    // Translate Trust to C++
    std::vector<LineMapping> mappings;
    std::vector<std::string> cpp_lines;
    int cpp_line_counter = 0;

    // Header
    cpp_lines.push_back("// Generated from Trust by trust");
    cpp_lines.push_back("#include <cstdio>");
    cpp_lines.push_back("");
    cpp_lines.push_back("int main() {");
    cpp_line_counter = 4; // 0-indexed: line 4 is the first statement

    for (size_t i = 0; i < trust_lines.size(); ++i) {
        std::string trust_line = trim(trust_lines[i]);
        if (trust_line.empty() || trust_line[0] == '#') {
            continue; // Skip comments and blank lines
        }

        LineMapping mapping;
        mapping.trust_line = static_cast<int>(i + 1);
        mapping.cpp_line = cpp_line_counter + 1;

        std::string cpp_code;

        // Parse: create <var> = <expr>
        std::regex create_regex("^create\\s+([a-zA-Z_][a-zA-Z0-9_]*)\\s*=\\s*(.+)$");
        std::smatch match;
        if (std::regex_match(trust_line, match, create_regex)) {
            std::string var_name = trim(match[1].str());
            std::string expr = trim(match[2].str());
            cpp_code = "int " + var_name + " = " + expr + ";";
            mapping.trust_vars.push_back(var_name);
            mapping.cpp_vars.push_back(var_name);
        }
        // Parse: <var> = <expr>  (assignment)
        else if (std::regex_match(trust_line, match, std::regex("^([a-zA-Z_][a-zA-Z0-9_]*)\\s*=\\s*(.+)$"))) {
            std::string var_name = trim(match[1].str());
            std::string expr = trim(match[2].str());
            cpp_code = var_name + " = " + expr + ";";
            mapping.trust_vars.push_back(var_name);
            mapping.cpp_vars.push_back(var_name);
            // Extract additional variables from expression
            auto expr_vars = extract_variables(expr);
            for (const auto& v : expr_vars) {
                if (v != var_name) {
                    mapping.trust_vars.push_back(v);
                    mapping.cpp_vars.push_back(v);
                }
            }
        }
        // Parse: call printf("<format>", <args...>)
        else if (std::regex_match(trust_line, match, std::regex("^call\\s+(.+)$"))) {
            std::string call_expr = trim(match[1].str());
            cpp_code = call_expr + ";";
            // Extract variables from function arguments
            auto call_vars = extract_variables(call_expr);
            for (const auto& v : call_vars) {
                mapping.trust_vars.push_back(v);
                mapping.cpp_vars.push_back(v);
            }
        }
        else {
            std::cerr << "Warning: Unknown Trust syntax on line " << (i + 1) << ": " << trust_line << std::endl;
            continue;
        }

        cpp_lines.push_back(cpp_code);
        cpp_line_counter++;
        mappings.push_back(mapping);
    }

    // Close main function
    cpp_lines.push_back("return 0;");
    cpp_line_counter++;
    cpp_lines.push_back("}");
    cpp_line_counter++;

    // Build the embedded source map JSON string
    std::string map_json = "{\"version\":1,\"sources\":[{\"trust_file\":\"";
    map_json += json_escape(trust_file);
    map_json += "\",\"cpp_file\":\"";
    map_json += json_escape(cpp_file);
    map_json += "\",\"mappings\":[";
    for (size_t i = 0; i < mappings.size(); ++i) {
        if (i > 0) map_json += ",";
        map_json += "{\"trust_line\":" + std::to_string(mappings[i].trust_line);
        map_json += ",\"cpp_line\":" + std::to_string(mappings[i].cpp_line);
        map_json += ",\"trust_vars\":[";
        for (size_t j = 0; j < mappings[i].trust_vars.size(); ++j) {
            if (j > 0) map_json += ",";
            map_json += "\"" + json_escape(mappings[i].trust_vars[j]) + "\"";
        }
        map_json += "]";
        map_json += ",\"cpp_vars\":[";
        for (size_t j = 0; j < mappings[i].cpp_vars.size(); ++j) {
            if (j > 0) map_json += ",";
            map_json += "\"" + json_escape(mappings[i].cpp_vars[j]) + "\"";
        }
        map_json += "]}";
    }
    map_json += "]}]}";

    // Build the embedded source map C++ declaration
    // Generate a C char array with properly escaped content
    // Use a unique raw string delimiter that won't appear in JSON
    std::string embedded_map = "// === TRUST SOURCE MAP (embedded) ===\n";
    embedded_map += "__attribute__((section(\".trust_map\"), used))\n";
    embedded_map += "static const char trust_source_map_data[] =\n";
    embedded_map += "R\"__TRUSTMAP__(";
    embedded_map += map_json;
    embedded_map += ")__TRUSTMAP__\";\n";

    // Write C++ output
    std::ofstream outfile(cpp_file);
    if (!outfile.is_open()) {
        std::cerr << "Error: Cannot open " << cpp_file << " for writing" << std::endl;
        return 1;
    }

    for (const auto& cl : cpp_lines) {
        outfile << cl << "\n";
    }
    outfile << "\n";
    outfile << embedded_map << "\n";
    outfile.close();

    // Write external map.json
    std::ofstream mapfile(map_json_file);
    if (!mapfile.is_open()) {
        std::cerr << "Warning: Cannot open " << map_json_file << " for writing" << std::endl;
    } else {
        mapfile << map_json << "\n";
        mapfile.close();
    }

    std::cout << "Compiled " << trust_file << " -> " << cpp_file << std::endl;
    std::cout << "Source map: " << mappings.size() << " entries" << std::endl;
    std::cout << "JSON file: " << map_json_file << std::endl;

    return 0;
}