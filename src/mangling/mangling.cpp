// src/mangling/mangling.cpp
// Реализация единых правил именования (см. include/mangling/mangling.hpp).
#include "mangling/mangling.hpp"

#include "types/operator_registry.hpp"
#include "types/type_names.hpp"
#include "utils/error.hpp"
#include "utils/strings.hpp"

#include <cctype>

namespace trust::mangling {

std::string cppFuncIdent(std::string_view trustName, bool isOperator, bool isEntry) {
    if (isOperator) {
        return op::operatorCppName(trustName);
    }
    if (isEntry) {
        return std::string(utils::strip_native_prefix(trustName));
    }
    return utils::name_to_cpp(trustName);
}

std::string overloadSuffix(const TypeRegistry& reg, TypeId sig) {
    const auto* fd = reg.getTypeDataAs<FunctionTypeData>(structuralType(sig));
    if (fd == nullptr) {
        return {};
    }
    std::string s = "_";
    const auto sanitize = [](std::string_view n) {
        std::string out;
        out.reserve(n.size());
        for (const char c : n) {
            out += (std::isalnum(static_cast<unsigned char>(c)) != 0) ? c : '_';
        }
        return out;
    };
    for (const TypeId p : fd->paramTypes) {
        s += sanitize(reg.getFullTypeName(p));
        s += "_";
    }
    if (fd->variadicType != INVALID_TYPE_ID) {
        s += "vararg_";
    }
    return s;
}

std::string sanitizeSymbolPart(std::string_view s) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(s.size() * 2);
    for (const char c : s) {
        const auto u = static_cast<unsigned char>(c);
        if ((u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') || (u >= '0' && u <= '9')) {
            out += static_cast<char>(u);
        } else {
            out += '_';
            out += kHex[(u >> 4) & 0xf];
            out += kHex[u & 0xf];
        }
    }
    return out;
}

std::string exportSymbol(std::string_view qualifiedCppName, ExportKind kind) {
    std::string_view tag;
    switch (kind) {
    case ExportKind::Function:
        tag = "fn_";
        break;
    case ExportKind::Variable:
        tag = "var_";
        break;
    case ExportKind::Operator:
        tag = "op_";
        break;
    }
    std::string out = "__trust_";
    out += tag;
    out += sanitizeSymbolPart(qualifiedCppName);
    return out;
}

std::string interfaceDecl(std::string_view fwdDecl, std::string_view symbolRef) {
    static constexpr std::string_view kTail = " := ...;";
    EXPECT(!symbolRef.empty() && "interface declaration invariant (non-empty symbol reference)");
    EXPECT(fwdDecl.ends_with(kTail) && "interface declaration invariant (' := ...;' tail)");
    return std::string(fwdDecl.substr(0, fwdDecl.size() - kTail.size())) + " := " + std::string(symbolRef) + ";";
}

std::string tmpName(TempKind kind, int id) {
    switch (kind) {
    case TempKind::Match:
        return "_match" + std::to_string(id);
    case TempKind::Result:
        return "__trust_res_" + std::to_string(id);
    case TempKind::Discard:
        return "__trust_discard_" + std::to_string(id);
    }
    return {};
}

} // namespace trust::mangling
