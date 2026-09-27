// src/pm/semver.cpp
#include "pm/semver.hpp"

#include "pm/protocol.hpp"

#include <algorithm>
#include <cctype>
#include <vector>

namespace trust::pm {
namespace {

PmError invalidVersion(std::string_view text, const char* why) {
    return PmError{"pm.version.invalid", std::string("invalid version '") + std::string(text) + "': " + why, nlohmann::json{{"version", std::string(text)}}};
}

std::string_view trim(std::string_view text) {
    std::size_t b = 0;
    std::size_t e = text.size();
    while (b < e && std::isspace(static_cast<unsigned char>(text[b])) != 0) {
        ++b;
    }
    while (e > b && std::isspace(static_cast<unsigned char>(text[e - 1])) != 0) {
        --e;
    }
    return text.substr(b, e - b);
}

// Parses up to three dot-separated numeric components. `components` receives
// how many were present. Throws on malformed input.
Semver parseParts(std::string_view text, int& components) {
    Semver v;
    int values[3] = {0, 0, 0};
    components = 0;
    std::size_t pos = 0;
    while (pos <= text.size()) {
        const std::size_t dot = text.find('.', pos);
        const std::string_view part = (dot == std::string_view::npos) ? text.substr(pos) : text.substr(pos, dot - pos);
        if (part.empty()) {
            throw PmException(invalidVersion(text, "empty component"));
        }
        if (components >= 3) {
            throw PmException(invalidVersion(text, "too many components"));
        }
        int value = 0;
        for (const char c : part) {
            if (std::isdigit(static_cast<unsigned char>(c)) == 0) {
                throw PmException(invalidVersion(text, "non-numeric component"));
            }
            value = value * 10 + (c - '0');
        }
        values[components] = value;
        ++components;
        if (dot == std::string_view::npos) {
            break;
        }
        pos = dot + 1;
    }
    if (components == 0) {
        throw PmException(invalidVersion(text, "empty version"));
    }
    v.major = values[0];
    v.minor = values[1];
    v.patch = values[2];
    return v;
}

} // namespace

Semver parseSemver(std::string_view text) {
    int components = 0;
    const Semver v = parseParts(trim(text), components);
    if (components != 3) {
        throw PmException(invalidVersion(text, "expected MAJOR.MINOR.PATCH"));
    }
    return v;
}

std::string formatSemver(const Semver& version) {
    return std::to_string(version.major) + "." + std::to_string(version.minor) + "." + std::to_string(version.patch);
}

namespace {

int compareSemver(const Semver& lhs, const Semver& rhs) {
    if (lhs.major != rhs.major) {
        return lhs.major < rhs.major ? -1 : 1;
    }
    if (lhs.minor != rhs.minor) {
        return lhs.minor < rhs.minor ? -1 : 1;
    }
    if (lhs.patch != rhs.patch) {
        return lhs.patch < rhs.patch ? -1 : 1;
    }
    return 0;
}

} // namespace

namespace {

bool satisfiesClause(const Semver& version, std::string_view clause) {
    clause = trim(clause);
    if (clause.empty() || clause == "*") {
        return true;
    }
    // Longest operators first.
    std::string_view op;
    for (const std::string_view candidate : {std::string_view(">="), std::string_view("<="), std::string_view(">"), std::string_view("<"),
                                             std::string_view("="), std::string_view("^"), std::string_view("~")}) {
        if (clause.substr(0, candidate.size()) == candidate) {
            op = candidate;
            break;
        }
    }
    const std::string_view body = trim(clause.substr(op.size()));
    int components = 0;
    const Semver v = parseParts(body, components);
    const int cmp = compareSemver(version, v);
    if (op == ">=") {
        return cmp >= 0;
    }
    if (op == "<=") {
        return cmp <= 0;
    }
    if (op == ">") {
        return cmp > 0;
    }
    if (op == "<") {
        return cmp < 0;
    }
    if (op == "=" || op.empty()) {
        return cmp == 0;
    }
    if (op == "^") {
        // ^X.Y.Z: >=X.Y.Z and next major change (with the 0.x exceptions).
        Semver upper;
        if (v.major > 0) {
            upper = Semver{v.major + 1, 0, 0};
        } else if (v.minor > 0) {
            upper = Semver{0, v.minor + 1, 0};
        } else {
            upper = Semver{0, 0, v.patch + 1};
        }
        return cmp >= 0 && compareSemver(version, upper) < 0;
    }
    if (op == "~") {
        // ~X.Y.Z: >=X.Y.Z and next minor change (~X -> next major).
        Semver upper = (components >= 2) ? Semver{v.major, v.minor + 1, 0} : Semver{v.major + 1, 0, 0};
        return cmp >= 0 && compareSemver(version, upper) < 0;
    }
    return false;
}

} // namespace

bool satisfiesRange(const Semver& version, std::string_view range) {
    const std::string_view whole = trim(range);
    if (whole.empty() || whole == "*") {
        return true;
    }
    std::size_t pos = 0;
    while (pos <= whole.size()) {
        const std::size_t comma = whole.find(',', pos);
        const std::string_view clause = (comma == std::string_view::npos) ? whole.substr(pos) : whole.substr(pos, comma - pos);
        if (!satisfiesClause(version, clause)) {
            return false;
        }
        if (comma == std::string_view::npos) {
            break;
        }
        pos = comma + 1;
    }
    return true;
}

} // namespace trust::pm
