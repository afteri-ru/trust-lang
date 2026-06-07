#ifndef INCLUDED_TYPES_H_
#define INCLUDED_TYPES_H_

#include <map>
#include <set>
#include <iosfwd>
#include <memory>
#include <vector>
#include <deque>
#include <iterator>
#include <iomanip>
#include <variant>
#include <algorithm>
#include <stdexcept>
#include <type_traits>
#include <string_view>
#include <cmath>
#include <random>
#include <typeindex>
#include <locale>
#include <codecvt>
#include <functional>
#include <regex>
#include <filesystem>
#include <utility>
#include <cstdlib>
#include <ctime>
#include <complex>
#include <source_location>

#include <sstream>
#include <iostream>
#include <fstream>
#include <fcntl.h>

#include <stdarg.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <linux/limits.h>

#ifdef _MSC_VER
#include <windows.h>
#include <wchar.h>
#else
#include <wait.h>
#include <dlfcn.h>
#include <sys/param.h>
#include <sys/utsname.h>
#include <unistd.h>
#endif

#include "syntax/term_types.h"
#include "utils/error.hpp"

// Keep these for compatibility with existing code
#define TO_STR2(ARG) #ARG
#define TO_STR(ARG) TO_STR2(ARG)

#define STATIC_ASSERT(expr) static_assert((expr), #expr)

#define SCOPE(scope) scope

namespace trust {

static constexpr const char* ws = " \t\n\r\f\v";

inline std::string& rtrim(std::string& s, const char* t = ws) {
    s.erase(s.find_last_not_of(t) + 1);
    return s;
}

inline std::string& ltrim(std::string& s, const char* t = ws) {
    s.erase(0, s.find_first_not_of(t));
    return s;
}

inline std::string& trim(std::string& s, const char* t = ws) {
    return ltrim(rtrim(s, t), t);
}

template <typename T>
T repeat(T str, const std::size_t n) {
    if (n == 0) {
        str.clear();
        str.shrink_to_fit();
        return str;
    } else if (n == 1 || str.empty()) {
        return str;
    }
    const auto period = str.size();
    if (period == 1) {
        str.append(n - 1, str.front());
        return str;
    }
    str.reserve(period * n);
    std::size_t m{2};
    for (; m < n; m *= 2)
        str += str;
    str.append(str.c_str(), (n - (m / 2)) * period);
    return str;
}

template <class T>
T BaseFileName(T const& path, T const& delims = "/\\") {
    return path.substr(path.find_last_of(delims) + 1);
}

template <class T>
T RemoveFileExtension(T const& filename) {
    typename T::size_type const p(filename.find_last_of('.'));
    return p > 0 && p != T::npos ? filename.substr(0, p) : filename;
}

typedef std::vector<std::string> StringArray;

class ParserError : public std::runtime_error {
  public:
    ParserError(const std::string& msg)
    : runtime_error(msg) {}

    ParserError(const char* format, ...)
    : runtime_error(formatMessage(format)) {}

  private:
    static std::string formatMessage(const char* format, ...) {
        va_list args;
        va_start(args, format);
        int size = std::vsnprintf(nullptr, 0, format, args);
        va_end(args);
        std::string result(size + 1, '\0');
        va_start(args, format);
        std::vsnprintf(&result[0], size + 1, format, args);
        va_end(args);
        result.resize(size);
        return result;
    }
};

// Forward declarations
class Parser;
class Macro;

typedef std::shared_ptr<const Term> TermPtrConst;
typedef std::shared_ptr<Macro> MacroPtr;
typedef std::shared_ptr<Parser> ParserPtr;
typedef std::shared_ptr<std::string> SourceType;
typedef std::vector<std::string> PostLexerType;

void NewLangSignalHandler(int signal);

// Type helpers (kept for parser)
const TermPtr getNoneTerm();
const TermPtr getEllipsysTerm();
const TermPtr getRequiredTerm();

// Name inspection helpers (unchanged)
inline bool isReservedName(const std::string_view name) {
    if (name.empty())
        return false;
    if (name.size() > 3 || !(name[0] == '$' || name[0] == '@' || name[0] == '%')) {
        return name.compare("_") == 0;
    }
    return name.compare("$") == 0 || name.compare("@") == 0 || name.compare("%") == 0 || name.compare("$$") == 0 || name.compare("@$") == 0 ||
           name.compare("$^") == 0 || name.compare("$?") == 0 || name.compare("@::") == 0 || name.compare("$*") == 0 || name.compare("$#") == 0;
}

inline bool isInternalName(const std::string_view name) {
    return !name.empty() && (name.rbegin()[0] == ':' || name.rbegin()[0] == '$' || isReservedName(name));
}

inline bool isMangledName(const std::string_view name) {
    return name.size() > 4 && name[0] == '_' && name[1] == '$';
}

inline bool isModuleName(const std::string_view name) {
    return !name.empty() && name[0] == '\\';
}

inline bool isStaticName(const std::string_view name) {
    return name.find("::") != std::string::npos;
}

inline bool isFieldName(const std::string_view name) {
    return name.find(".") != std::string::npos;
}

inline bool isTrivialName(const std::string_view name) {
    return name.find("$") == std::string::npos && name.find(":") == std::string::npos && name.find("@") == std::string::npos;
}

inline bool isLocalName(const std::string_view name) {
    return !name.empty() && (name[0] == '$' || name[name.size() - 1] == '$');
}

inline bool isGlobalScope(const std::string_view name) {
    ASSERT(!isMangledName(name));
    return name.size() > 1 && ((name[0] == ':' && name[1] == ':') || (name[0] == '$' && name[1] == '$'));
}

inline bool isModuleScope(const std::string_view name) {
    size_t pos = name.find("::");
    return pos && pos != std::string::npos && name[0] != '@';
}

inline bool isTypeName(const std::string_view name) {
    return name.find(":::") != std::string::npos || (name.size() > 1 && name[0] == ':' && name[1] != ':');
}

inline bool isFullName(const std::string_view name) {
    return name.size() > 1 && name[0] == ':' && name[1] == ':';
}

inline bool isMacroName(const std::string_view name) {
    return !name.empty() && name[0] == '@';
}

inline bool isNativeName(const std::string_view name) {
    return !name.empty() && name[0] == '%';
}

inline bool isLocalAnyName(const std::string_view name) {
    return !name.empty() && (name[0] == '$' || name[0] == '@' || name[0] == ':' || name[0] == '%' || name[0] == '\\');
}

inline bool isSystemName(const std::string_view name) {
    if (name.empty())
        return false;
    return name.size() >= 4 && name.find("__") == 0 && name.rfind("__") == name.size() - 2;
}

inline bool isPrivateName(const std::string_view name) {
    if (name.empty())
        return false;
    return name.size() >= 3 && name.find("__") == 0;
}

inline bool isHidenName(const std::string_view name) {
    return !isPrivateName(name) && name.find("_") == 0;
}

inline bool isVariableName(const std::string_view name) {
    if (isModuleName(name)) {
        return name.find("::") != name.npos;
    }
    return !isTypeName(name);
}

inline bool isConstName(const std::string_view name) {
    return !name.empty() && name[name.size() - 1] == '^';
}

inline std::string NormalizeName(const std::string_view name) {
    std::string result(name.begin());
    ASSERT(result.size());
    if (isInternalName(name)) {
        return result;
    } else if (isLocalName(name)) {
        std::rotate(std::begin(result), std::begin(result) + 1, std::end(result));
    } else if (isTrivialName(name)) {
        result += "$";
    } else if (isTypeName(name)) {
        result = result.substr(1);
        result += ":::";
    } else {
        if (!isStaticName(name)) {
            ASSERT(isStaticName(name));
        }
        if (result[0] == '@' && result.find("@::") == 0) {
            result = result.substr(3);
        }
        result += "::";
    }
    return result;
}

inline std::string MakeName(std::string name) {
    if (!name.empty() && (name[0] == '\\' || name[0] == '$' || name[0] == '@' || name[0] == '%')) {
        return name.find("\\\\") == 0 ? name.substr(2) : name.substr(1);
    }
    return name;
}

inline std::string ExtractModuleName(const std::string_view name) {
    if (isMangledName(name)) {
        std::string result(name.begin(), name.begin() + name.find("$_"));
        result[0] = '$';
        std::replace(result.begin(), result.end(), '$', '\\');
        return result;
    } else {
        if (isModuleName(name)) {
            size_t pos = name.find("::");
            if (pos != std::string::npos) {
                return std::string(name.begin(), name.begin() + pos);
            }
            return std::string(name.begin(), name.end());
        }
    }
    return std::string();
}

inline bool CheckCharModuleName(const std::string_view name) {
    size_t found = name.find("\\_");
    if (name.empty() || found == 0 || found == 1 || name[name.size() - 1] == '_') {
        return false;
    }
    for (size_t i = 0; i < name.size(); i++) {
        if (!(name[i] == '\\' || name[i] == '_' || islower(name[i]) || isdigit(name[i]))) {
            return false;
        }
    }
    return true;
}

inline std::string ExtractName(std::string name) {
    size_t pos = name.rfind("::");
    if (pos != std::string::npos) {
        name = name.substr(pos + 2);
    }
    if (isModuleName(name)) {
        return std::string();
    }
    return name;
}

// InternalName class (unchanged)
class InternalName : public std::string {
  public:
    InternalName(const std::string_view str = "") {
        ASSERT(str.empty() || trust::isInternalName(str));
        this->assign(str);
    }
    InternalName(const InternalName& name) {
        ASSERT(name.empty() || trust::isInternalName(name));
        this->assign(name);
    }
    InternalName& operator=(const InternalName& name) {
        ASSERT(name.empty() || trust::isInternalName(name));
        this->assign(name);
        return *this;
    }
    InternalName& operator=(const std::string_view name) {
        this->assign(name);
        return *this;
    }

    inline bool isInternalName() { return trust::isInternalName(*this); }

    inline std::string getMangledName(const std::string_view module) {
        std::string result(*this);
        if (!isInternalName()) {
            FAULT("The name '{}' is not internal!", result);
        }
        std::replace(result.begin(), result.end(), ':', '$');
        result.insert(0, "$_");
        if (module.size() > 2) {
            result.insert(result.begin(), module.begin(), module.end());
            std::replace(result.begin(), result.begin() + module.size(), '\\', '$');
        }
        result.insert(0, "_$");
        return result;
    }

    static std::string ExtractModuleName(const std::string_view name) { return trust::ExtractModuleName(name); }

    inline bool isModule() { return trust::isModuleName(this->c_str()); }
    inline bool isStatic() { return trust::isStaticName(this->c_str()); }
    inline bool isLocal() { return trust::isLocalName(this->c_str()); }
    inline bool isGlobalScope() { return trust::isGlobalScope(*this); }
    inline bool isModuleScope() { return trust::isModuleScope(*this); }
    inline bool isTypeName() { return trust::isTypeName(this->c_str()); }
    inline bool isFullName() { return trust::isFullName(this->c_str()); }
    inline bool isMacroName() { return trust::isMacroName(this->c_str()); }
    inline bool isNativeName() { return trust::isNativeName(this->c_str()); }
    inline bool isLocalAnyName() { return trust::isLocalAnyName(this->c_str()); }
    inline bool isSystemName() { return trust::isSystemName(this->c_str()); }
    inline bool isPrivateName() { return trust::isPrivateName(this->c_str()); }
    inline bool isHidenName() { return trust::isHidenName(this->c_str()); }
    inline bool isVariableName() { return trust::isVariableName(this->c_str()); }
    inline bool isConstName() { return trust::isConstName(this->c_str()); }

    inline std::string SetFromLocalName(std::string name) {
        this->assign(name);
        return *this;
    }
    inline std::string SetFromGlobalName(std::string name) {
        this->assign(name);
        return *this;
    }
    inline std::string GetLocalName() { return *this; }
    inline std::string ExtractModuleName() { return trust::ExtractModuleName(this->c_str()); }
    inline std::string ExtractName() { return trust::ExtractName(this->c_str()); }
};

} // namespace trust

#endif // INCLUDED_TYPES_H_
