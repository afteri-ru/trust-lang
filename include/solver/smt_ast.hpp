#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace trust {
namespace solver {

/// Sort (type) in SMT-LIB 2
enum class SmtSortKind {
    kBool,
    kInt,
    kReal,
    kBitVec,
    kArray,
    kUninterpreted,
};

struct SmtSort {
    SmtSortKind kind = SmtSortKind::kBool;
    std::string name;                ///< for kUninterpreted
    uint32_t bv_width = 0;           ///< for kBitVec
    std::shared_ptr<SmtSort> domain; ///< for kArray
    std::shared_ptr<SmtSort> range;  ///< for kArray
};

/// Command kind in SMT-LIB 2
enum class SmtCommandKind {
    kDeclareSort,
    kDeclareFun,
    kDefineFun,
    kAssert,
    kCheckSat,
    kGetModel,
    kGetValue,
    kExit,
    kSetLogic,
    kPush,
    kPop,
};

/// Term (expression) nodes
enum class SmtTermKind {
    kConst,    ///< constant value
    kVar,      ///< variable by index/de Bruijn
    kNamedVar, ///< variable by name
    kApp,      ///< function application
    kForall,   ///< forall quantifier
    kExists,   ///< exists quantifier
    kLet,      ///< let expression
};

struct SmtTerm {
    SmtTermKind kind = SmtTermKind::kConst;

    // Constant
    std::string const_value;

    // Variable (named)
    std::string var_name;
    int var_index = -1;
    std::shared_ptr<SmtTerm> var_sort; ///< optional sort annotation

    // Function application
    std::string fun_name;
    std::vector<std::shared_ptr<SmtTerm>> args;

    // Quantifier
    std::vector<std::string> quant_vars;
    std::shared_ptr<SmtTerm> quant_body;

    // Let
    std::vector<std::pair<std::string, std::shared_ptr<SmtTerm>>> let_bindings;
    std::shared_ptr<SmtTerm> let_body;
};

/// SMT-LIB 2 command
struct SmtCommand {
    SmtCommandKind kind = SmtCommandKind::kCheckSat;

    // Declare-sort
    std::string sort_name;
    uint32_t sort_arity = 0;

    // Declare-fun / define-fun
    std::string fun_name;
    std::vector<SmtSort> fun_arg_sorts;
    std::shared_ptr<SmtSort> fun_result_sort; ///< for kUninterpreted sorts we need heap
    std::shared_ptr<SmtTerm> fun_body;        ///< for define-fun

    // Assert
    std::shared_ptr<SmtTerm> assert_term;

    // Get-value
    std::vector<std::shared_ptr<SmtTerm>> get_value_terms;

    // Push/Pop
    uint32_t stack_depth = 1;

    // Set-logic
    std::string logic_name;
};

/// A complete SMT-LIB 2 script
struct SmtScript {
    std::string logic;
    std::vector<SmtCommand> commands;
};

} // namespace solver
} // namespace trust