#include "solver/solver_z3.hpp"
#include "solver/smt_printer.hpp"

#include <z3.h>

#include <array>
#include <cstring>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace trust {
namespace solver {

namespace {

/// Рекурсивная inline-подстановка let-биндингов в теле: каждый kNamedVar, чьё имя есть
/// среди bindings, заменяется на соответствующий терм-значение. Семантически эквивалентна
/// `(let (...) body)` (SMT-LIB let - просто сахар); Z3 4.8 не имеет Z3_mk_let.
SmtTerm substLet(const SmtTerm& term, const std::vector<std::pair<std::string, std::shared_ptr<SmtTerm>>>& bindings) {
    const auto find = [&](const std::string& name) -> const SmtTerm* {
        for (const auto& [n, v] : bindings) {
            if (n == name && v) {
                return v.get();
            }
        }
        return nullptr;
    };
    if (term.kind == SmtTermKind::kNamedVar) {
        if (const SmtTerm* rep = find(term.var_name)) {
            return *rep;
        }
    }
    SmtTerm out = term; // копия (args/quant_body/let пересобираются ниже)
    out.args.clear();
    out.args.reserve(term.args.size());
    for (const auto& a : term.args) {
        out.args.push_back(a ? std::make_shared<SmtTerm>(substLet(*a, bindings)) : nullptr);
    }
    if (term.quant_body) {
        out.quant_body = std::make_shared<SmtTerm>(substLet(*term.quant_body, bindings));
    }
    out.let_bindings.clear();
    for (const auto& [n, v] : term.let_bindings) {
        out.let_bindings.emplace_back(n, v ? std::make_shared<SmtTerm>(substLet(*v, bindings)) : nullptr);
    }
    if (term.let_body) {
        out.let_body = std::make_shared<SmtTerm>(substLet(*term.let_body, bindings));
    }
    return out;
}

} // namespace

struct SolverZ3::Impl {
    Z3_config config;
    Z3_context ctx;
    Z3_solver solver;
    bool logic_set = false;
    bool has_error = false;

    Impl() {
        config = Z3_mk_config();
        Z3_set_param_value(config, "model", "true");
        // Non-RC контекст: термы/солвер живут до Z3_del_context (короткоживущий solver).
        // RC-контекст (Z3_mk_context_rc) требует inc_ref каждого терма, иначе GC собирает их
        // сразу - висячие указатели → crash.
        ctx = Z3_mk_context(config);
        solver = Z3_mk_solver(ctx);
    }

    ~Impl() {
        Z3_del_context(ctx);
        Z3_del_config(config);
    }

    void setLogic(const std::string& logic) {
        Z3_symbol logic_sym = Z3_mk_string_symbol(ctx, logic.c_str());
        solver = Z3_mk_solver_for_logic(ctx, logic_sym);
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
            if (term.sort.kind == SmtSortKind::kBool) {
                return (term.const_value == "true" || term.const_value == "1") ? Z3_mk_true(ctx) : Z3_mk_false(ctx);
            }
            if (term.sort.kind == SmtSortKind::kArray && term.sort.domain && term.sort.range) {
                // Константный массив ((as const (Array D R)) val): все элементы = const_value.
                Z3_ast dv = Z3_mk_numeral(ctx, term.const_value.c_str(), toZ3Sort(*term.sort.range));
                return Z3_mk_const_array(ctx, toZ3Sort(*term.sort.domain), dv);
            }
            // BitVec/Real и пр.: числовой литерал в сорт терма.
            return Z3_mk_numeral(ctx, term.const_value.c_str(), toZ3Sort(term.sort));
        }
        case SmtTermKind::kNamedVar: {
            Z3_symbol sym = Z3_mk_string_symbol(ctx, term.var_name.c_str());
            return Z3_mk_const(ctx, sym, toZ3Sort(term.sort));
        }
        case SmtTermKind::kApp: {
            // Встроенный оператор (enum SmtOp) - компайлтайм-проверяемый switch; nullopt -
            // пользовательская функция (declare-fun) обрабатывается ниже. Единый источник
            // имён операторов - parseSmtOp (X-макрос SOLVER_OPERATOR_LIST), без строковых сравнений.
            if (term.op) {
                const auto binary = [&](auto&& mk) -> Z3_ast {
                    if (term.args.size() != 2) {
                        return nullptr;
                    }
                    return mk(toZ3Term(*term.args[0]), toZ3Term(*term.args[1]));
                };
                const auto unary = [&](auto&& mk) -> Z3_ast {
                    if (term.args.size() != 1) {
                        return nullptr;
                    }
                    return mk(toZ3Term(*term.args[0]));
                };
                switch (*term.op) {
                case SmtOp::BvAdd:
                    return binary([&](Z3_ast a, Z3_ast b) { return Z3_mk_bvadd(ctx, a, b); });
                case SmtOp::BvSub:
                    return binary([&](Z3_ast a, Z3_ast b) { return Z3_mk_bvsub(ctx, a, b); });
                case SmtOp::BvMul:
                    return binary([&](Z3_ast a, Z3_ast b) { return Z3_mk_bvmul(ctx, a, b); });
                case SmtOp::BvSdiv:
                    return binary([&](Z3_ast a, Z3_ast b) { return Z3_mk_bvsdiv(ctx, a, b); });
                case SmtOp::BvSrem:
                    return binary([&](Z3_ast a, Z3_ast b) { return Z3_mk_bvsrem(ctx, a, b); });
                case SmtOp::BvSgt:
                    return binary([&](Z3_ast a, Z3_ast b) { return Z3_mk_bvsgt(ctx, a, b); });
                case SmtOp::BvSge:
                    return binary([&](Z3_ast a, Z3_ast b) { return Z3_mk_bvsge(ctx, a, b); });
                case SmtOp::BvSlt:
                    return binary([&](Z3_ast a, Z3_ast b) { return Z3_mk_bvslt(ctx, a, b); });
                case SmtOp::BvSle:
                    return binary([&](Z3_ast a, Z3_ast b) { return Z3_mk_bvsle(ctx, a, b); });
                case SmtOp::BvUdiv:
                    return binary([&](Z3_ast a, Z3_ast b) { return Z3_mk_bvudiv(ctx, a, b); });
                case SmtOp::BvUrem:
                    return binary([&](Z3_ast a, Z3_ast b) { return Z3_mk_bvurem(ctx, a, b); });
                case SmtOp::BvUgt:
                    return binary([&](Z3_ast a, Z3_ast b) { return Z3_mk_bvugt(ctx, a, b); });
                case SmtOp::BvUge:
                    return binary([&](Z3_ast a, Z3_ast b) { return Z3_mk_bvuge(ctx, a, b); });
                case SmtOp::BvUlt:
                    return binary([&](Z3_ast a, Z3_ast b) { return Z3_mk_bvult(ctx, a, b); });
                case SmtOp::BvUle:
                    return binary([&](Z3_ast a, Z3_ast b) { return Z3_mk_bvule(ctx, a, b); });
                case SmtOp::BvNeg:
                    return unary([&](Z3_ast a) { return Z3_mk_bvneg(ctx, a); });
                case SmtOp::BvAnd:
                    return binary([&](Z3_ast a, Z3_ast b) { return Z3_mk_bvand(ctx, a, b); });
                case SmtOp::BvOr:
                    return binary([&](Z3_ast a, Z3_ast b) { return Z3_mk_bvor(ctx, a, b); });
                case SmtOp::BvXor:
                    return binary([&](Z3_ast a, Z3_ast b) { return Z3_mk_bvxor(ctx, a, b); });
                case SmtOp::BvNot:
                    return unary([&](Z3_ast a) { return Z3_mk_bvnot(ctx, a); });
                case SmtOp::BvShl:
                    return binary([&](Z3_ast a, Z3_ast b) { return Z3_mk_bvshl(ctx, a, b); });
                case SmtOp::BvLshr:
                    return binary([&](Z3_ast a, Z3_ast b) { return Z3_mk_bvlshr(ctx, a, b); });
                case SmtOp::BvAshr:
                    return binary([&](Z3_ast a, Z3_ast b) { return Z3_mk_bvashr(ctx, a, b); });
                case SmtOp::And: {
                    // mkAnd может дать N аргументов (>=2): Z3_mk_and принимает массив.
                    std::vector<Z3_ast> ab;
                    ab.reserve(term.args.size());
                    for (const auto& a : term.args) {
                        if (!a) {
                            return nullptr;
                        }
                        ab.push_back(toZ3Term(*a));
                    }
                    return Z3_mk_and(ctx, static_cast<unsigned>(ab.size()), ab.data());
                }
                case SmtOp::Or: {
                    std::vector<Z3_ast> ab;
                    ab.reserve(term.args.size());
                    for (const auto& a : term.args) {
                        if (!a) {
                            return nullptr;
                        }
                        ab.push_back(toZ3Term(*a));
                    }
                    return Z3_mk_or(ctx, static_cast<unsigned>(ab.size()), ab.data());
                }
                case SmtOp::Xor:
                    return binary([&](Z3_ast a, Z3_ast b) { return Z3_mk_xor(ctx, a, b); });
                case SmtOp::Implies:
                    return binary([&](Z3_ast a, Z3_ast b) { return Z3_mk_implies(ctx, a, b); });
                case SmtOp::Not:
                    return unary([&](Z3_ast a) { return Z3_mk_not(ctx, a); });
                case SmtOp::Eq:
                    return binary([&](Z3_ast a, Z3_ast b) { return Z3_mk_eq(ctx, a, b); });
                case SmtOp::Ite: {
                    if (term.args.size() != 3) {
                        return nullptr;
                    }
                    return Z3_mk_ite(ctx, toZ3Term(*term.args[0]), toZ3Term(*term.args[1]), toZ3Term(*term.args[2]));
                }
                case SmtOp::SignExt: {
                    if (term.args.size() != 1) {
                        return nullptr;
                    }
                    return Z3_mk_sign_ext(ctx, term.ext_amount, toZ3Term(*term.args[0]));
                }
                case SmtOp::ZeroExt: {
                    if (term.args.size() != 1) {
                        return nullptr;
                    }
                    return Z3_mk_zero_ext(ctx, term.ext_amount, toZ3Term(*term.args[0]));
                }
                case SmtOp::Select: {
                    if (term.args.size() != 2) {
                        return nullptr;
                    }
                    return Z3_mk_select(ctx, toZ3Term(*term.args[0]), toZ3Term(*term.args[1]));
                }
                case SmtOp::Store: {
                    if (term.args.size() != 3) {
                        return nullptr;
                    }
                    return Z3_mk_store(ctx, toZ3Term(*term.args[0]), toZ3Term(*term.args[1]), toZ3Term(*term.args[2]));
                }
                default:
                    return nullptr; // все enumerators покрыты выше; защитная ветка
                }
            }
            // Пользовательская функция (declare-fun): сигнатура из сортов аргументов и результата.
            const auto& fn = term.fun_name;
            std::vector<Z3_ast> z3_args;
            std::vector<Z3_sort> domain;
            z3_args.reserve(term.args.size());
            domain.reserve(term.args.size());
            for (const auto& arg : term.args) {
                z3_args.push_back(toZ3Term(*arg));
                domain.push_back(toZ3Sort(arg->sort));
            }
            Z3_symbol sym = Z3_mk_string_symbol(ctx, fn.c_str());
            Z3_func_decl fdecl = Z3_mk_func_decl(ctx, sym, static_cast<unsigned>(domain.size()), domain.data(), toZ3Sort(term.sort));
            return Z3_mk_app(ctx, fdecl, static_cast<unsigned>(z3_args.size()), z3_args.data());
        }
        case SmtTermKind::kForall:
        case SmtTermKind::kExists: {
            if (!term.quant_body || term.quant_vars.size() != term.quant_var_sorts.size()) {
                return nullptr;
            }
            const unsigned n = static_cast<unsigned>(term.quant_vars.size());
            // Bound-переменные как константы; Z3_mk_forall_const абстрагирует их в квантор.
            std::vector<Z3_app> bound;
            bound.reserve(n);
            for (unsigned i = 0; i < n; ++i) {
                const Z3_sort s = toZ3Sort(term.quant_var_sorts[i]);
                const Z3_symbol sym = Z3_mk_string_symbol(ctx, term.quant_vars[i].c_str());
                bound.push_back(reinterpret_cast<Z3_app>(Z3_mk_const(ctx, sym, s)));
            }
            const Z3_ast body = toZ3Term(*term.quant_body);
            if (!body) {
                return nullptr;
            }
            if (term.kind == SmtTermKind::kForall) {
                return Z3_mk_forall_const(ctx, 0, n, bound.data(), 0, nullptr, body);
            }
            return Z3_mk_exists_const(ctx, 0, n, bound.data(), 0, nullptr, body);
        }
        case SmtTermKind::kLet: {
            if (!term.let_body) {
                return nullptr;
            }
            // Z3 4.8 не имеет Z3_mk_let: эквивалентная inline-подстановка биндингов в тело.
            const SmtTerm body = substLet(*term.let_body, term.let_bindings);
            return toZ3Term(body);
        }
        default:
            return nullptr;
        }
    }

    /// Значение 0-арной константы в модели последнего SAT check-sat (для отчёта о контрпримере).
    /// nullopt - модель недоступна / имя не константа. Вызывается СРАЗУ после SAT check-sat
    /// (модель сохраняется до следующего check-sat/pop). srt - сорт константы (для Z3_model_eval).
    std::optional<std::string> modelValue(const std::string& name, const SmtSort& srt) {
        Z3_model model = Z3_solver_get_model(ctx, solver);
        if (!model) {
            return std::nullopt;
        }
        Z3_symbol sym = Z3_mk_string_symbol(ctx, name.c_str());
        Z3_ast c = Z3_mk_const(ctx, sym, toZ3Sort(srt));
        Z3_ast val = nullptr;
        if (!Z3_model_eval(ctx, model, c, true, &val) || !val) {
            return std::nullopt;
        }
        Z3_string s = Z3_ast_to_string(ctx, val);
        return s ? std::optional<std::string>(s) : std::nullopt;
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
    if (!z3_term) {
        m_impl->has_error = true; // неподдерживаемый терм (кванторы/let) - ошибка, не тихий пропуск
        return;
    }
    Z3_solver_assert(m_impl->ctx, m_impl->solver, z3_term);
}

SolverResult SolverZ3::checkSat() {
    if (m_impl->has_error) {
        return SolverResult::kError;
    }
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

std::optional<std::string> SolverZ3::getModelValue(const std::string& name, const SmtSort& srt) {
    return m_impl->modelValue(name, srt);
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