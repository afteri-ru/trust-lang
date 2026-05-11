#pragma once

#include "diag/location.hpp"
#include "gencpp/ast.hpp"
#include <expected>
#include <string>
#include <unordered_map>
#include <vector>

namespace trust {

struct FuncSignature {
    TypeInfo return_type;
    std::vector<TypeInfo> param_types;
    TypeInfo lookup_param(size_t i) const {
        if (i >= param_types.size()) {
            return make_builtin_type(TypeKind::Void);
        }
        return param_types[i];
    }
};

class SymbolTable {
  public:
    // Global scope: functions are always global
    void declare_function(const std::string& name, const FuncSignature& sig) { functions_[name] = sig; }

    const FuncSignature* lookup_function(const std::string& name) const {
        auto it = functions_.find(name);
        if (it == functions_.end())
            return nullptr;
        return &it->second;
    }

    // New local scope (enter function)
    void push_scope() { scopes_.emplace_back(); }

    void pop_scope() {
        if (scopes_.empty())
            throw std::runtime_error("Unbalanced scope");
        scopes_.pop_back();
    }

    void declare_var(const std::string& name, TypeInfo type) {
        if (scopes_.empty())
            throw std::runtime_error("No active scope for variable");
        scopes_.back()[name] = std::move(type);
    }

    TypeInfo lookup_var(const std::string& name, MapperRange loc) const {
        // Search from innermost to outermost scope
        for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
            auto vit = it->find(name);
            if (vit != it->end())
                return vit->second;
        }
        (void)loc;
        return make_builtin_type(TypeKind::Void);
    }

    std::string check_assignment(const std::string& target, TypeInfo expr_type, MapperRange loc) {
        std::string err;
        auto var_type = lookup_var(target, loc);
        if (var_type.id == TypeKind::Void) {
            // variable not found — error already set
            return "Unknown variable: " + target;
        }
        if (var_type.id != expr_type.id && var_type.id != TypeKind::Void && expr_type.id != TypeKind::Void) {
            // Allow int <-> bool implicit conversion (common in many languages)
            bool is_numeric = (var_type.id == TypeKind::Int32 || var_type.id == TypeKind::Bool);
            bool is_numeric_expr = (expr_type.id == TypeKind::Int32 || expr_type.id == TypeKind::Bool);
            if (!(is_numeric && is_numeric_expr)) {
                return "Cannot assign " + expr_type.cpp_name + " to " + var_type.cpp_name;
            }
        }
        return err;
    }

  private:
    std::unordered_map<std::string, FuncSignature> functions_;
    std::vector<std::unordered_map<std::string, TypeInfo>> scopes_;
};
} // namespace trust