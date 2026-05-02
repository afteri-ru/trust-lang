#ifndef TRANSPILER_H
#define TRANSPILER_H

#include <string>
#include <vector>
#include <sstream>
#include <unordered_set>
#include <cctype>

#include "trust_source.h"

// Trims whitespace from both ends
inline std::string trim(const std::string &s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos)
        return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

// Checks if string is a number (possibly negative)
inline bool is_number(const std::string &s) {
    if (s.empty())
        return false;
    size_t i = 0;
    if (s[0] == '-')
        i = 1;
    for (; i < s.size(); ++i)
        if (!std::isdigit(static_cast<unsigned char>(s[i])))
            return false;
    return true;
}

// Splits string into tokens by whitespace
inline std::vector<std::string> tokenize(const std::string &s) {
    std::vector<std::string> tokens;
    std::istringstream iss(s);
    std::string token;
    while (iss >> token)
        tokens.push_back(token);
    return tokens;
}

// Replaces known variable names with cpp_<name> prefix in expression text
inline std::string replace_vars(const std::string &expr, const std::unordered_set<std::string> &vars) {
    std::string result;
    size_t i = 0;
    while (i < expr.size()) {
        if (std::isalpha(static_cast<unsigned char>(expr[i])) || expr[i] == '_') {
            size_t start = i;
            while (i < expr.size() && (std::isalnum(static_cast<unsigned char>(expr[i])) || expr[i] == '_'))
                ++i;
            std::string word = expr.substr(start, i - start);
            if (vars.find(word) != vars.end())
                result += "cpp_" + word;
            else
                result += word;
        } else {
            result += expr[i];
            ++i;
        }
    }
    return result;
}

// Transpile mini-language lines to C++ code
// Supported constructs:
//   create <varname> = <value> [<op> <value>];  ->  int cpp_<varname> = <value>...;
//   print <value> ...;                           ->  std::cout << <value>...;
//   <varname> = <value> [<op> <value>];         ->  cpp_<varname> = <value>...;
// Comments start with '#', constructs end with ';'
inline std::vector<std::string> transpile(const std::vector<std::string> &input_lines, trust::TrustSource &mapping) {
    std::vector<std::string> output;
    std::unordered_set<std::string> declared_vars;

    for (size_t numline = 0; numline < input_lines.size(); ++numline) {
        const auto &raw_line = input_lines[numline];
        std::string line = trim(raw_line);
        if (line.empty() || line[0] == '#')
            continue;

        std::string line_buf;

        // Split line by ';' to support multiple statements per line
        std::vector<std::string> stmts;
        size_t pos = 0;
        while (pos < line.size()) {
            auto sc = line.find(';', pos);
            if (sc == std::string::npos) {
                stmts.push_back(line.substr(pos));
                break;
            }
            stmts.push_back(line.substr(pos, sc - pos));
            pos = sc + 1;
        }

        for (size_t si = 0; si < stmts.size(); ++si) {
            std::string stmt = trim(stmts[si]);
            if (stmt.empty())
                continue;

            std::vector<std::string> tokens = tokenize(stmt);
            if (tokens.empty())
                continue;

            if (tokens[0] == "create") {
                if (tokens.size() < 4 || tokens[2] != "=") {
                    line_buf += "// ERROR: invalid create syntax: " + stmt;
                    continue;
                }
                std::string var_name = tokens[1];
                std::string rhs;
                for (size_t j = 3; j < tokens.size(); ++j) {
                    if (j > 3) rhs += " ";
                    rhs += tokens[j];
                }
                rhs = replace_vars(rhs, declared_vars);
                declared_vars.insert(var_name);
                if (!line_buf.empty()) line_buf += "; ";
                line_buf += "int cpp_" + var_name + " = " + rhs;
                mapping.addVarMapping(numline + 1, output.size() + 1, var_name, "cpp_" + var_name);
            }
            else if (tokens[0] == "print") {
                std::string cout_line = "std::cout";
                for (size_t j = 1; j < tokens.size(); ++j) {
                    std::string arg = tokens[j];
                    if (!is_number(arg) && declared_vars.find(arg) != declared_vars.end())
                        cout_line += " << cpp_" + arg;
                    else
                        cout_line += " << " + arg;
                }
                if (!line_buf.empty()) line_buf += "; ";
                line_buf += cout_line;
            }
            else { // assignment
                if (tokens.size() < 3 || tokens[1] != "=") {
                    line_buf += "// ERROR: invalid syntax: " + stmt;
                    continue;
                }
                std::string var_name = tokens[0];
                std::string rhs;
                for (size_t j = 2; j < tokens.size(); ++j) {
                    if (j > 2) rhs += " ";
                    rhs += tokens[j];
                }

                if (declared_vars.find(var_name) == declared_vars.end()) {
                    line_buf += "// ERROR: undeclared variable '" + var_name + "': " + stmt;
                    continue;
                }

                rhs = replace_vars(rhs, declared_vars);
                if (!line_buf.empty()) line_buf += "; ";
                line_buf += "cpp_" + var_name + " = " + rhs;
            }
        }

        if (!line_buf.empty()) {
            line_buf += ";";
            output.push_back(line_buf);
            mapping.addLineMapping(numline + 1, output.size());
        }
    }

    output.insert(output.begin(), {"#include <iostream>", "", "int main() {"});
    mapping.setCppLineInserted(3);

    output.push_back("return 0;");
    output.push_back("}");

    return output;
}

#endif // TRANSPILER_H