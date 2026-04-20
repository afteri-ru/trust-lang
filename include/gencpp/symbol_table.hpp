#pragma once

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
        if (i >= param_types.size())
            return TypeInfo::builtin(TypeKind::Void);
        return param_types[i];
    }
};

class SymbolTable {
  public:
    // Global scope: functions are always global
    void declare_function(const std::string &name, const FuncSignature &sig) { functions_[name] = sig; }

    const FuncSignature *lookup_function(const std::string &name) const {
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

    void declare_var(const std::string &name, TypeInfo type) {
        if (scopes_.empty())
            throw std::runtime_error("No active scope for variable");
        scopes_.back()[name] = std::move(type);
    }

    std::expected<TypeInfo, std::string> lookup_var(const std::string &name, SourceLoc loc) const {
        // Search from innermost to outermost scope
        for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
            auto vit = it->find(name);
            if (vit != it->end())
                return vit->second;
        }
        return std::unexpected("Unknown variable: " + name);
    }

    std::expected<void, std::string> check_assignment(const std::string &target, TypeInfo expr_type, SourceLoc loc) {
        auto var_type_result = lookup_var(target, loc);
        if (!var_type_result.has_value()) {
            return std::unexpected(var_type_result.error());
        }
        TypeInfo var_type = var_type_result.value();
        if (var_type != expr_type && var_type.kind != TypeKind::Void && expr_type.kind != TypeKind::Void) {
            // Allow int <-> bool implicit conversion (common in many languages)
            bool is_numeric = (var_type.kind == TypeKind::Int || var_type.kind == TypeKind::Bool);
            bool is_numeric_expr = (expr_type.kind == TypeKind::Int || expr_type.kind == TypeKind::Bool);
            if (!(is_numeric && is_numeric_expr)) {
                return std::unexpected("Cannot assign " + expr_type.to_string() + " to " + var_type.to_string());
            }
        }
        return {};
    }

  private:
    std::unordered_map<std::string, FuncSignature> functions_;
    std::vector<std::unordered_map<std::string, TypeInfo>> scopes_;
};
} // namespace trust