#include "solver/smt_printer.hpp"

#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

namespace trust {
namespace solver {

std::string SmtPrinter::escapeSymbol(const std::string& name) {
    // SMT-LIB 2 symbols: if it contains non-alphanumeric chars, use |...|
    for (char c : name) {
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '+' || c == '*' || c == '/' ||
              c == '%' || c == '?' || c == '!' || c == '.' || c == '$' || c == '~' || c == '&' || c == '^' || c == '<' || c == '>' || c == '=' || c == ':' ||
              c == '#')) {
            return "|" + name + "|";
        }
    }
    return name;
}

std::string SmtPrinter::printSort(const SmtSort& sort) {
    switch (sort.kind) {
    case SmtSortKind::kBool:
        return "Bool";
    case SmtSortKind::kInt:
        return "Int";
    case SmtSortKind::kReal:
        return "Real";
    case SmtSortKind::kBitVec:
        return "(_ BitVec " + std::to_string(sort.bv_width) + ")";
    case SmtSortKind::kArray: {
        std::string d = sort.domain ? printSort(*sort.domain) : "Array_unknown";
        std::string r = sort.range ? printSort(*sort.range) : "Array_unknown";
        return "(Array " + d + " " + r + ")";
    }
    case SmtSortKind::kUninterpreted:
        return escapeSymbol(sort.name);
    }
    return "_err_";
}

std::string SmtPrinter::printTerm(const SmtTerm& term) {
    switch (term.kind) {
    case SmtTermKind::kConst:
        return term.const_value;
    case SmtTermKind::kVar:
        return "?v" + std::to_string(term.var_index);
    case SmtTermKind::kNamedVar:
        return escapeSymbol(term.var_name);
    case SmtTermKind::kApp: {
        if (term.args.empty()) {
            return escapeSymbol(term.fun_name);
        }
        std::string result = "(" + escapeSymbol(term.fun_name);
        for (const auto& arg : term.args) {
            result += " " + (arg ? printTerm(*arg) : "..err..");
        }
        result += ")";
        return result;
    }
    case SmtTermKind::kForall: {
        std::string result = "(forall (";
        for (size_t i = 0; i < term.quant_vars.size(); ++i) {
            if (i > 0) {
                result += " ";
            }
            result += "(" + escapeSymbol(term.quant_vars[i]) +
                      " "
                      "Bool)";
        }
        result += ") ";
        result += term.quant_body ? printTerm(*term.quant_body) : "true";
        result += ")";
        return result;
    }
    case SmtTermKind::kExists: {
        std::string result = "(exists (";
        for (size_t i = 0; i < term.quant_vars.size(); ++i) {
            if (i > 0) {
                result += " ";
            }
            result += "(" + escapeSymbol(term.quant_vars[i]) +
                      " "
                      "Bool)";
        }
        result += ") ";
        result += term.quant_body ? printTerm(*term.quant_body) : "true";
        result += ")";
        return result;
    }
    case SmtTermKind::kLet: {
        std::string result = "(let (";
        for (size_t i = 0; i < term.let_bindings.size(); ++i) {
            if (i > 0) {
                result += " ";
            }
            result += "(" + escapeSymbol(term.let_bindings[i].first) + " " +
                      (term.let_bindings[i].second ? printTerm(*term.let_bindings[i].second) : "..err..") + ")";
        }
        result += ") ";
        result += term.let_body ? printTerm(*term.let_body) : "..err..";
        result += ")";
        return result;
    }
    }
    return "..err..";
}

std::string SmtPrinter::printCommand(const SmtCommand& cmd) {
    switch (cmd.kind) {
    case SmtCommandKind::kSetLogic:
        return "(set-logic " + escapeSymbol(cmd.logic_name) + ")";
    case SmtCommandKind::kDeclareSort:
        return "(declare-sort " + escapeSymbol(cmd.sort_name) + " " + std::to_string(cmd.sort_arity) + ")";
    case SmtCommandKind::kDeclareFun: {
        std::string result = "(declare-fun " + escapeSymbol(cmd.fun_name) + " (";
        for (size_t i = 0; i < cmd.fun_arg_sorts.size(); ++i) {
            if (i > 0) {
                result += " ";
            }
            result += printSort(cmd.fun_arg_sorts[i]);
        }
        SmtSort default_bool;
        default_bool.kind = SmtSortKind::kBool;
        result += ") " + printSort(cmd.fun_result_sort ? *cmd.fun_result_sort : default_bool) + ")";
        return result;
    }
    case SmtCommandKind::kDefineFun: {
        std::string result = "(define-fun " + escapeSymbol(cmd.fun_name) + " (";
        for (size_t i = 0; i < cmd.fun_arg_sorts.size(); ++i) {
            if (i > 0) {
                result += " ";
            }
            result += "(" + escapeSymbol("x" + std::to_string(i)) + " " + printSort(cmd.fun_arg_sorts[i]) + ")";
        }
        SmtSort default_bool;
        default_bool.kind = SmtSortKind::kBool;
        result += ") " + printSort(cmd.fun_result_sort ? *cmd.fun_result_sort : default_bool) + " ";
        result += cmd.fun_body ? printTerm(*cmd.fun_body) : "..err..";
        result += ")";
        return result;
    }
    case SmtCommandKind::kAssert:
        return "(assert " + (cmd.assert_term ? printTerm(*cmd.assert_term) : "true") + ")";
    case SmtCommandKind::kCheckSat:
        return "(check-sat)";
    case SmtCommandKind::kGetModel:
        return "(get-model)";
    case SmtCommandKind::kGetValue: {
        std::string result = "(get-value (";
        for (size_t i = 0; i < cmd.get_value_terms.size(); ++i) {
            if (i > 0) {
                result += " ";
            }
            result += cmd.get_value_terms[i] ? printTerm(*cmd.get_value_terms[i]) : "..err..";
        }
        result += "))";
        return result;
    }
    case SmtCommandKind::kPush:
        return "(push " + std::to_string(cmd.stack_depth) + ")";
    case SmtCommandKind::kPop:
        return "(pop " + std::to_string(cmd.stack_depth) + ")";
    case SmtCommandKind::kExit:
        return "(exit)";
    }
    return "..err..";
}

std::string SmtPrinter::printScript(const SmtScript& script) {
    std::string result;
    if (!script.logic.empty()) {
        result += "(set-logic " + escapeSymbol(script.logic) + ")\n";
    }
    for (const auto& cmd : script.commands) {
        result += printCommand(cmd) + "\n";
    }
    return result;
}

} // namespace solver
} // namespace trust