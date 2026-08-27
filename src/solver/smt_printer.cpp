#include "solver/smt_printer.hpp"

#include "diag/context.hpp"
#include "diag/mapper.hpp"

#include <cstdint>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace trust {
namespace solver {

namespace {

/// SMT-символ может содержать [a-zA-Z0-9_+*/%?!.$~&^<>=:#-] (см. escapeSymbol).
bool is_symbol_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '+' || c == '*' || c == '/' || c == '%' ||
           c == '?' || c == '!' || c == '.' || c == '$' || c == '~' || c == '&' || c == '^' || c == '<' || c == '>' || c == '=' || c == ':' || c == '#';
}

/// Является ли [pos, pos+len) отдельным SMT-токеном (не частью большего имени).
bool is_bounded_token(std::string_view text, std::size_t pos, std::size_t len) {
    if (pos > 0 && is_symbol_char(text[pos - 1])) {
        return false;
    }
    if (pos + len < text.size() && is_symbol_char(text[pos + len])) {
        return false;
    }
    return true;
}

/// Первое вхождение name как отдельного токена; npos - нет.
std::size_t find_token(std::string_view text, std::string_view name) {
    std::size_t pos = 0;
    while ((pos = text.find(name, pos)) != std::string_view::npos) {
        if (is_bounded_token(text, pos, name.size())) {
            return pos;
        }
        pos += name.size();
    }
    return std::string_view::npos;
}

/// (line, column) с 1-based отсчётом для байтового смещения pos в text.
std::pair<std::size_t, std::size_t> line_col_of(std::string_view text, std::size_t pos) {
    std::size_t line = 1, col = 1;
    const std::size_t limit = pos < text.size() ? pos : text.size();
    for (std::size_t i = 0; i < limit; ++i) {
        if (text[i] == '\n') {
            ++line;
            col = 1;
        } else {
            ++col;
        }
    }
    return {line, col};
}

} // namespace

std::string SmtPrinter::buildSmt2Map(const Context& ctx, const SmtScript& script, std::string_view smt2_text) {
    // Диапазон trust-источника → "файл:строка:кол-строка:кол" (1-based).
    const auto format_trust = [&](MapperRange r) -> std::string {
        if (r.isInvalid()) {
            return "<unknown>";
        }
        const auto b = ctx.source().line_column(r.begin);
        const auto e = ctx.source().line_column(r.end);
        std::string file(ctx.source().filename(r.begin.fileIdx()));
        return file + ":" + std::to_string(b.line) + ":" + std::to_string(b.column) + "-" + std::to_string(e.line) + ":" + std::to_string(e.column);
    };

    std::string result;
    result += "# SMT-LIB2 -> trust source map (--solver-mode=export)\n";

    // Символы: SMT-имя → {trust-имя, диапазон}. Диапазон в .smt2 - первое standalone-вхождение.
    // Символы, отсутствующие в .smt2 (напр. функция с закодированным телом - заменена термом),
    // не выводятся (нет фрагмента .smt2 для сопоставления).
    for (const auto& [smt_name, ref] : script.symbolMap) {
        const std::size_t pos = find_token(smt2_text, smt_name);
        if (pos == std::string_view::npos) {
            continue;
        }
        const auto [sline, scol] = line_col_of(smt2_text, pos);
        const std::size_t end_col = scol + smt_name.size() - 1;
        result += "symbol=" + smt_name + " smt2=" + std::to_string(sline) + ":" + std::to_string(scol) + "-" + std::to_string(end_col);
        result += " file=" + format_trust(ref.srcRange);
        result += " name=" + ref.trustName + "\n";
    }

    // Asserts: каждая команда печатается на отдельной строке .smt2.
    // Строка команды: строка 1 = set-logic (если есть), затем команды по порядку, команда i на
    // строке (logic?2:1) + i.
    const std::size_t first_cmd_line = (script.logic.empty() ? 0u : 1u) + 1u;
    std::size_t assert_idx = 0;
    for (std::size_t i = 0; i < script.commands.size(); ++i) {
        const auto& cmd = script.commands[i];
        if (cmd.kind != SmtCommandKind::kAssert) {
            continue;
        }
        const std::size_t line = first_cmd_line + i; // 1-based строка (printScript - команда на строку)
        const std::string_view text = smt2_text;
        // Переходим к началу нужной строки.
        std::size_t line_start = 0;
        for (std::size_t l = 1; l < line && line_start != std::string_view::npos; ++l) {
            std::size_t nl = text.find('\n', line_start);
            line_start = (nl == std::string_view::npos) ? text.size() : nl + 1;
        }
        std::size_t col_end = text.size();
        if (line_start < text.size()) {
            const std::size_t nl = text.find('\n', line_start);
            col_end = (nl == std::string_view::npos) ? text.size() : nl;
        }
        result += "assert=" + std::to_string(assert_idx) + " smt2=" + std::to_string(line) + ":1-" + std::to_string(col_end);
        result += " file=" + format_trust(cmd.srcRange);
        // trust-имя: по совпадению диапазона - имя функции, чей контракт породил VC.
        std::string trust_name;
        for (const auto& [_, ref] : script.symbolMap) {
            if (ref.srcRange == cmd.srcRange) {
                trust_name = ref.trustName;
                break;
            }
        }
        result += " name=" + trust_name + "\n";
        ++assert_idx;
    }
    return result;
}

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
        // BitVec-литерал: const_value - числовое значение, сорт задаёт разрядность.
        if (term.sort.kind == SmtSortKind::kBitVec) {
            return "(_ bv" + term.const_value + " " + std::to_string(term.sort.bv_width) + ")";
        }
        // Константный массив: ((as const (Array D R)) val) - val в сорте range.
        if (term.sort.kind == SmtSortKind::kArray) {
            std::string val = term.const_value;
            if (term.sort.range && term.sort.range->kind == SmtSortKind::kBitVec) {
                val = "(_ bv" + term.const_value + " " + std::to_string(term.sort.range->bv_width) + ")";
            }
            return "((as const " + printSort(term.sort) + ") " + val + ")";
        }
        return term.const_value;
    case SmtTermKind::kVar:
        return "?v" + std::to_string(term.var_index);
    case SmtTermKind::kNamedVar:
        return escapeSymbol(term.var_name);
    case SmtTermKind::kApp: {
        // Sign/zero-расширение - indexed-оператор SMT-LIB: `((_ sign_extend N) x)`.
        if (term.op == SmtOp::SignExt || term.op == SmtOp::ZeroExt) {
            const char* op = (term.op == SmtOp::SignExt) ? "sign_extend" : "zero_extend";
            if (term.args.size() != 1) {
                return "..err..";
            }
            return "((_ " + std::string(op) + " " + std::to_string(term.ext_amount) + ") " + (term.args[0] ? printTerm(*term.args[0]) : "..err..") + ")";
        }
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
            // Сорт bound-переменной (инвариант: quant_var_sorts параллелен quant_vars).
            SmtSort vs;
            vs.kind = SmtSortKind::kBool;
            if (i < term.quant_var_sorts.size()) {
                vs = term.quant_var_sorts[i];
            }
            result += "(" + escapeSymbol(term.quant_vars[i]) + " " + printSort(vs) + ")";
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
            SmtSort vs;
            vs.kind = SmtSortKind::kBool;
            if (i < term.quant_var_sorts.size()) {
                vs = term.quant_var_sorts[i];
            }
            result += "(" + escapeSymbol(term.quant_vars[i]) + " " + printSort(vs) + ")";
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