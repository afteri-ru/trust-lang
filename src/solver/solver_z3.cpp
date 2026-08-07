#include "solver/solver_z3.hpp"
#include "solver/smt_printer.hpp"

#include <z3.h>

#include <cstring>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace trust {
namespace solver {

struct SolverZ3::Impl {
    Z3_config config;
    Z3_context ctx;
    Z3_solver solver;
    bool logic_set = false;

    Impl() {
        config = Z3_mk_config();
        Z3_set_param_value(config, "model", "true");
        ctx = Z3_mk_context_rc(config);
        solver = Z3_mk_solver(ctx);
        Z3_solver_inc_ref(ctx, solver);
    }

    ~Impl() {
        Z3_solver_dec_ref(ctx, solver);
        Z3_del_context(ctx);
        Z3_del_config(config);
    }

    void setLogic(const std::string& logic) {
        Z3_symbol logic_sym = Z3_mk_string_symbol(ctx, logic.c_str());
        solver = Z3_mk_solver_for_logic(ctx, logic_sym);
        Z3_solver_inc_ref(ctx, solver);
        logic_set = true;
    }

    Z3_sort toZ3Sort(const SmtSort& sort) {
        switch (sort.kind) {
        case SmtSortKind::kBool:
            return Z3_mk_bool_sort(ctx);
        case SmtSortKind::kInt:
            return Z3_mk_int_sort(ctx);
        case SmtSortKind::kReal:
            return Z3_mk_real_sort(ctx);
        case SmtSortKind::kBitVec:
            return Z3_mk_bv_sort(ctx, sort.bv_width);
        case SmtSortKind::kArray: {
            Z3_sort d = sort.domain ? toZ3Sort(*sort.domain) : Z3_mk_bool_sort(ctx);
            Z3_sort r = sort.range ? toZ3Sort(*sort.range) : Z3_mk_bool_sort(ctx);
            return Z3_mk_array_sort(ctx, d, r);
        }
        case SmtSortKind::kUninterpreted: {
            Z3_symbol sym = Z3_mk_string_symbol(ctx, sort.name.c_str());
            return Z3_mk_uninterpreted_sort(ctx, sym);
        }
        }
        return Z3_mk_bool_sort(ctx);
    }

    Z3_ast toZ3Term(const SmtTerm& term) {
        switch (term.kind) {
        case SmtTermKind::kConst: {
            // Try to parse as integer, then boolean
            if (term.const_value == "true") {
                return Z3_mk_true(ctx);
            }
            if (term.const_value == "false") {
                return Z3_mk_false(ctx);
            }
            // Default: create symbol
            Z3_symbol sym = Z3_mk_string_symbol(ctx, term.const_value.c_str());
            return Z3_mk_const(ctx, sym, Z3_mk_bool_sort(ctx));
        }
        case SmtTermKind::kNamedVar: {
            Z3_symbol sym = Z3_mk_string_symbol(ctx, term.var_name.c_str());
            return Z3_mk_const(ctx, sym, Z3_mk_bool_sort(ctx));
        }
        case SmtTermKind::kApp: {
            Z3_symbol sym = Z3_mk_string_symbol(ctx, term.fun_name.c_str());
            // Built-in logic operators
            if (term.fun_name == "and" || term.fun_name == "or" || term.fun_name == "xor" || term.fun_name == "=>") {
                if (term.args.size() < 2) {
                    return Z3_mk_true(ctx);
                }
                Z3_ast a = term.args[0] ? toZ3Term(*term.args[0]) : Z3_mk_true(ctx);
                Z3_ast b = term.args[1] ? toZ3Term(*term.args[1]) : Z3_mk_true(ctx);
                if (term.fun_name == "and") {
                    return Z3_mk_and(ctx, 2, std::array<Z3_ast, 2>{a, b}.data());
                }
                if (term.fun_name == "or") {
                    return Z3_mk_or(ctx, 2, std::array<Z3_ast, 2>{a, b}.data());
                }
                if (term.fun_name == "xor") {
                    return Z3_mk_xor(ctx, a, b);
                }
                return Z3_mk_implies(ctx, a, b);
            }
            if (term.fun_name == "not") {
                if (term.args.empty()) {
                    return Z3_mk_true(ctx);
                }
                return Z3_mk_not(ctx, term.args[0] ? toZ3Term(*term.args[0]) : Z3_mk_true(ctx));
            }
            if (term.fun_name == "=") {
                if (term.args.size() < 2) {
                    return Z3_mk_true(ctx);
                }
                return Z3_mk_eq(ctx, term.args[0] ? toZ3Term(*term.args[0]) : Z3_mk_true(ctx), term.args[1] ? toZ3Term(*term.args[1]) : Z3_mk_true(ctx));
            }
            // User-defined function
            std::vector<Z3_ast> z3_args;
            for (const auto& arg : term.args) {
                z3_args.push_back(arg ? toZ3Term(*arg) : Z3_mk_true(ctx));
            }
            Z3_sort range_sort = Z3_mk_bool_sort(ctx);
            Z3_func_decl fdecl = Z3_mk_func_decl(ctx, sym, 0, nullptr, range_sort);
            return Z3_mk_app(ctx, fdecl, z3_args.size(), z3_args.data());
        }
        default:
            return Z3_mk_true(ctx);
        }
    }
};

SolverZ3::SolverZ3()
: m_impl(std::make_unique<Impl>()) {
}

SolverZ3::~SolverZ3() = default;

void SolverZ3::setLogic(const std::string& logic) {
    m_impl->setLogic(logic);
}

void SolverZ3::declareSort(const std::string& name, uint32_t /*arity*/) {
    Z3_symbol sym = Z3_mk_string_symbol(m_impl->ctx, name.c_str());
    Z3_mk_uninterpreted_sort(m_impl->ctx, sym);
}

void SolverZ3::declareFun(const std::string& name, const std::vector<SmtSort>& arg_sorts, const SmtSort& result_sort) {
    Z3_symbol sym = Z3_mk_string_symbol(m_impl->ctx, name.c_str());
    std::vector<Z3_sort> z3_domain;
    for (const auto& s : arg_sorts) {
        z3_domain.push_back(m_impl->toZ3Sort(s));
    }
    Z3_sort z3_range = m_impl->toZ3Sort(result_sort);
    Z3_func_decl fdecl = Z3_mk_func_decl(m_impl->ctx, sym, z3_domain.size(), z3_domain.data(), z3_range);
    (void)fdecl;
}

void SolverZ3::defineFun(const std::string& name, const std::vector<SmtSort>& arg_sorts, const SmtSort& result_sort, const SmtTerm& body) {
    declareFun(name, arg_sorts, result_sort);
    (void)body;
}

void SolverZ3::assertFormula(const SmtTerm& formula) {
    Z3_ast z3_term = m_impl->toZ3Term(formula);
    Z3_solver_assert(m_impl->ctx, m_impl->solver, z3_term);
}

SolverResult SolverZ3::checkSat() {
    Z3_lbool result = Z3_solver_check(m_impl->ctx, m_impl->solver);
    switch (result) {
    case Z3_L_TRUE:
        return SolverResult::kSat;
    case Z3_L_FALSE:
        return SolverResult::kUnsat;
    case Z3_L_UNDEF:
    default:
        return SolverResult::kUnknown;
    }
}

void SolverZ3::push(uint32_t depth) {
    for (uint32_t i = 0; i < depth; ++i) {
        Z3_solver_push(m_impl->ctx, m_impl->solver);
    }
}

void SolverZ3::pop(uint32_t depth) {
    Z3_solver_pop(m_impl->ctx, m_impl->solver, depth);
}

std::string SolverZ3::getSmtLibText() const {
    Z3_string smt2 = Z3_solver_to_string(m_impl->ctx, m_impl->solver);
    return smt2 ? std::string(smt2) : "";
}

void SolverZ3::reset() {
    Z3_solver_reset(m_impl->ctx, m_impl->solver);
}

} // namespace solver
} // namespace trust