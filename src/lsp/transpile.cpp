#include "lsp/transpile.h"

#include <filesystem>
#include <cctype>
#include <sstream>
#include <unordered_set>

namespace trust {
namespace lsp {

namespace {

std::string trim(const std::string &s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

bool isNumber(const std::string &s) {
    if (s.empty()) return false;
    size_t i = 0;
    if (s[0] == '-') i = 1;
    for (; i < s.size(); ++i)
        if (!std::isdigit(static_cast<unsigned char>(s[i])))
            return false;
    return true;
}

std::vector<std::string> tokenize(const std::string &s) {
    std::vector<std::string> tokens;
    std::istringstream iss(s);
    std::string token;
    while (iss >> token) tokens.push_back(token);
    return tokens;
}

std::string replaceVars(const std::string &expr, const std::unordered_set<std::string> &vars) {
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

} // anonymous namespace

TranspileResult transpileTrustSource(std::string_view trustCode,
                                     std::string_view trustFileName,
                                     std::string_view basePath) {
    TranspileResult result;
    auto mapping = std::make_unique<TrustSource>(
        basePath.empty() ? "." : std::string(basePath));

    // Если есть имя файла — задаём пару trust/cpp (cppFile — только имя, без пути,
    // т.к. cpp-код хранится in-memory, и normalizePath в TrustSource требует,
    // чтобы cppFile был относительным или лежал под cpp_directory_)
    if (!trustFileName.empty()) {
        std::filesystem::path srcPath(trustFileName);
        std::string cppFile = srcPath.filename().string() + ".cpp";
        mapping->setFilePair(std::string(trustFileName), cppFile);
    }

    // Разбиваем на строки
    std::vector<std::string> inputLines;
    {
        std::string trustCodeStr(trustCode);
        std::istringstream stream(std::move(trustCodeStr));
        std::string line;
        while (std::getline(stream, line)) {
            inputLines.push_back(line);
        }
    }

    std::unordered_set<std::string> declaredVars;

    for (size_t numline = 0; numline < inputLines.size(); ++numline) {
        const auto &rawLine = inputLines[numline];
        std::string line = trim(rawLine);
        if (line.empty() || line[0] == '#') continue;

        std::string lineBuf;

        // Split by ';'
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
            if (stmt.empty()) continue;

            std::vector<std::string> tokens = tokenize(stmt);
            if (tokens.empty()) continue;

            if (tokens[0] == "create") {
                if (tokens.size() < 4 || tokens[2] != "=") {
                    result.errors.push_back("invalid create syntax: " + stmt);
                    continue;
                }
                std::string varName = tokens[1];
                std::string rhs;
                for (size_t j = 3; j < tokens.size(); ++j) {
                    if (j > 3) rhs += " ";
                    rhs += tokens[j];
                }
                rhs = replaceVars(rhs, declaredVars);
                declaredVars.insert(varName);
                if (!lineBuf.empty()) lineBuf += "; ";
                lineBuf += "int cpp_" + varName + " = " + rhs;
                mapping->addVarMapping(numline + 1, result.cppLines.size() + 1,
                                       varName, "cpp_" + varName);
            } else if (tokens[0] == "print") {
                std::string coutLine = "std::cout";
                for (size_t j = 1; j < tokens.size(); ++j) {
                    std::string arg = tokens[j];
                    if (!isNumber(arg) && declaredVars.find(arg) != declaredVars.end())
                        coutLine += " << cpp_" + arg;
                    else
                        coutLine += " << " + arg;
                }
                if (!lineBuf.empty()) lineBuf += "; ";
                lineBuf += coutLine;
            } else { // assignment
                if (tokens.size() < 3 || tokens[1] != "=") {
                    result.errors.push_back("invalid syntax: " + stmt);
                    continue;
                }
                std::string varName = tokens[0];
                std::string rhs;
                for (size_t j = 2; j < tokens.size(); ++j) {
                    if (j > 2) rhs += " ";
                    rhs += tokens[j];
                }
                if (declaredVars.find(varName) == declaredVars.end()) {
                    result.errors.push_back("undeclared variable '" + varName + "': " + stmt);
                    continue;
                }
                rhs = replaceVars(rhs, declaredVars);
                if (!lineBuf.empty()) lineBuf += "; ";
                lineBuf += "cpp_" + varName + " = " + rhs;
            }
        }

        if (!lineBuf.empty()) {
            lineBuf += ";";
            result.cppLines.push_back(lineBuf);
            mapping->addLineMapping(numline + 1, result.cppLines.size());
        }
    }

    // Оборачиваем в main()
    result.cppLines.insert(result.cppLines.begin(),
                           {"#include <iostream>", "", "int main() {"});
    mapping->setCppLineInserted(3);

    result.cppLines.push_back("return 0;");
    result.cppLines.push_back("}");

    result.sourceMap = std::move(mapping);
    return result;
}

} // namespace lsp
} // namespace trust