#pragma once

#include "gencpp/ast.hpp"
#include <memory>
#include <tuple>
#include <vector>
#include <utility>

namespace trust {
// AstBuilder — unified, overloaded factory for AST nodes.
// All methods follow a consistent naming convention: snake_case, full words.
// Overloads use TypeInfo vs TypeKind vs inferred where appropriate.
class AstBuilder {
  public:
    AstBuilder() = default;

    // ============================
    // Expression factories
    // ============================

    std::unique_ptr<IntLiteral> int_literal(int v) { return std::make_unique<IntLiteral>(v); }

    std::unique_ptr<StringLiteral> string_literal(std::string v) { return std::make_unique<StringLiteral>(std::move(v)); }

    std::unique_ptr<StringLiteral> string_literal(TokenInfo ti) { return std::make_unique<StringLiteral>(std::move(ti.text)); }

    std::unique_ptr<VarRef> var_ref(std::string name) { return std::make_unique<VarRef>(std::move(name)); }

    std::unique_ptr<BinaryOp> binary_op(BinOp op, std::unique_ptr<Expr> l, std::unique_ptr<Expr> r) {
        return std::make_unique<BinaryOp>(op, std::move(l), std::move(r));
    }

    std::unique_ptr<CallExpr> call(std::string name, std::vector<std::unique_ptr<Expr>> args) {
        return std::make_unique<CallExpr>(std::move(name), std::move(args));
    }

    // Enum/Struct expressions
    std::unique_ptr<EnumLiteral> enum_literal(std::string enum_name, std::string member) {
        return std::make_unique<EnumLiteral>(std::move(enum_name), std::move(member));
    }

    std::unique_ptr<MemberAccess> member_access(std::unique_ptr<Expr> obj, std::string field) {
        return std::make_unique<MemberAccess>(std::move(obj), std::move(field));
    }

    // Array expressions
    std::unique_ptr<ArrayAccess> array_access(std::unique_ptr<Expr> arr, std::unique_ptr<Expr> idx) {
        return std::make_unique<ArrayAccess>(std::move(arr), std::move(idx));
    }

    std::unique_ptr<ArrayInit> array_init(std::vector<std::unique_ptr<Expr>> elems, TypeKind elem_type = TypeKind::Int) {
        return std::make_unique<ArrayInit>(std::move(elems), elem_type);
    }

    // Cast expression — overloaded for TypeInfo and TypeKind
    std::unique_ptr<CastExpr> cast_expr(std::unique_ptr<Expr> e, TypeInfo target_type) {
        return std::make_unique<CastExpr>(std::move(e), std::move(target_type));
    }

    std::unique_ptr<CastExpr> cast_expr(std::unique_ptr<Expr> e, TypeKind target_type) {
        return std::make_unique<CastExpr>(std::move(e), TypeInfo::builtin(target_type));
    }

    // Ref expressions
    std::unique_ptr<RefMakeExpr> ref_make(std::unique_ptr<Expr> arg) { return std::make_unique<RefMakeExpr>(std::move(arg)); }

    std::unique_ptr<RefTakeExpr> ref_take(std::unique_ptr<Expr> arg) { return std::make_unique<RefTakeExpr>(std::move(arg)); }

    // Embed expression — raw C++ code embedded as string
    std::unique_ptr<EmbedExpr> embed_expr(std::string v) { return std::make_unique<EmbedExpr>(std::move(v)); }

    // ============================
    // Statement factories
    // ============================

    // VarDecl — overloaded: inferred type, explicit TypeInfo, explicit TypeKind
    // Builder without type — type_explicitly_set = false, type will be inferred from init
    std::unique_ptr<VarDecl> var_decl(std::string name, std::unique_ptr<Expr> init) { return std::make_unique<VarDecl>(std::move(name), std::move(init)); }

    // With explicit TypeInfo — type_explicitly_set = true
    std::unique_ptr<VarDecl> var_decl(std::string name, TypeInfo type, std::unique_ptr<Expr> init) {
        return std::make_unique<VarDecl>(std::move(name), std::move(type), std::move(init));
    }

    // With explicit TypeKind — type_explicitly_set = true
    std::unique_ptr<VarDecl> var_decl(std::string name, TypeKind type, std::unique_ptr<Expr> init) {
        return std::make_unique<VarDecl>(std::move(name), TypeInfo::builtin(type), std::move(init));
    }

    std::unique_ptr<AssignmentStmt> assign(std::unique_ptr<Expr> target, std::unique_ptr<Expr> value) {
        return std::make_unique<AssignmentStmt>(std::move(target), std::move(value));
    }

    // Convenience overload for simple var name
    std::unique_ptr<AssignmentStmt> assign(std::string target_name, std::unique_ptr<Expr> value) {
        return std::make_unique<AssignmentStmt>(std::make_unique<VarRef>(std::move(target_name)), std::move(value));
    }

    std::unique_ptr<ReturnStmt> ret(std::unique_ptr<Expr> value) { return std::make_unique<ReturnStmt>(std::move(value)); }

    std::unique_ptr<ReturnStmt> ret() { return std::make_unique<ReturnStmt>(nullptr); }

    std::unique_ptr<ExprStmt> expr_stmt(std::unique_ptr<Expr> e) { return std::make_unique<ExprStmt>(std::move(e)); }

    std::unique_ptr<IfStmt> if_stmt(std::unique_ptr<Expr> cond, BlockBody then_body, std::unique_ptr<IfStmt> elif = nullptr,
                                    std::unique_ptr<BlockStmt> else_body = nullptr) {
        return std::make_unique<IfStmt>(std::move(cond), std::move(then_body), std::move(elif), std::move(else_body));
    }

    // WhileStmt — overloaded: without else, with else
    std::unique_ptr<WhileStmt> while_stmt(std::unique_ptr<Expr> cond, BlockBody body) { return std::make_unique<WhileStmt>(std::move(cond), std::move(body)); }

    std::unique_ptr<WhileStmt> while_stmt(std::unique_ptr<Expr> cond, BlockBody body, BlockBody else_body) {
        return std::make_unique<WhileStmt>(std::move(cond), std::move(body), std::move(else_body));
    }

    // DoWhileStmt
    std::unique_ptr<DoWhileStmt> do_while_stmt(BlockBody body, std::unique_ptr<Expr> cond) {
        return std::make_unique<DoWhileStmt>(std::move(body), std::move(cond));
    }

    std::unique_ptr<CatchBlock> catch_block(TypeInfo type, std::string name, BlockBody body) {
        return std::make_unique<CatchBlock>(std::move(type), std::move(name), std::move(body));
    }

    std::unique_ptr<CatchBlock> catch_block(TypeKind type, std::string name, BlockBody body) {
        return std::make_unique<CatchBlock>(TypeInfo::builtin(type), std::move(name), std::move(body));
    }

    std::unique_ptr<EnumMember> enum_member(std::string name, std::unique_ptr<int> value = nullptr) {
        return std::make_unique<EnumMember>(std::move(name), std::move(value));
    }

    std::unique_ptr<StructField> struct_field(std::string name, TypeInfo type, std::unique_ptr<Expr> init = nullptr) {
        return std::make_unique<StructField>(std::move(name), std::move(type), std::move(init));
    }

    std::unique_ptr<TryCatchStmt> try_catch(BlockBody try_body, std::unique_ptr<CatchBlock> cb) {
        return std::make_unique<TryCatchStmt>(std::move(try_body), std::move(cb));
    }

    std::unique_ptr<ThrowStmt> throw_stmt(std::unique_ptr<Expr> value) { return std::make_unique<ThrowStmt>(std::move(value)); }

    std::unique_ptr<MatchingCase> matching_case(std::unique_ptr<Expr> pattern, BlockBody body) {
        return std::make_unique<MatchingCase>(std::move(pattern), std::move(body));
    }

    std::unique_ptr<MatchingStmt> matching(std::unique_ptr<Expr> expr, std::vector<std::unique_ptr<MatchingCase>> cases, BlockBody else_body = BlockBody{}) {
        return std::make_unique<MatchingStmt>(std::move(expr), std::move(cases), std::move(else_body));
    }

    std::unique_ptr<BlockStmt> block(BlockBody body) { return std::make_unique<BlockStmt>(std::move(body)); }

    std::unique_ptr<WhileElseBlock> while_else_block(BlockBody body) { return std::make_unique<WhileElseBlock>(std::move(body)); }

    std::unique_ptr<BreakStmt> break_stmt() { return std::make_unique<BreakStmt>(); }

    std::unique_ptr<ContinueStmt> continue_stmt() { return std::make_unique<ContinueStmt>(); }

    // ============================
    // Declaration factories
    // ============================

    std::unique_ptr<ParamDecl> param(std::string name, TypeInfo type) { return std::make_unique<ParamDecl>(std::move(name), std::move(type)); }

    std::unique_ptr<ParamDecl> param(std::string name, TypeKind type) { return std::make_unique<ParamDecl>(std::move(name), TypeInfo::builtin(type)); }

    std::unique_ptr<FuncDecl> function(std::string name, TypeInfo return_type, std::vector<std::unique_ptr<ParamDecl>> params, BlockBody body) {
        auto blk = std::make_unique<BlockStmt>(std::move(body));
        return std::make_unique<FuncDecl>(std::move(name), std::move(return_type), std::move(params), std::move(blk));
    }

    std::unique_ptr<FuncDecl> function(std::string name, TypeKind return_type, std::vector<std::unique_ptr<ParamDecl>> params, BlockBody body) {
        auto blk = std::make_unique<BlockStmt>(std::move(body));
        return std::make_unique<FuncDecl>(std::move(name), return_type, std::move(params), std::move(blk));
    }

    // Enum/Struct declarations
    std::unique_ptr<EnumDecl> enum_decl(std::string name, std::vector<std::pair<std::string, std::string>> members) {
        std::vector<std::unique_ptr<EnumMember>> enum_members;
        for (auto &[mname, mval] : members) {
            if (mval.empty()) {
                enum_members.push_back(std::make_unique<EnumMember>(std::move(mname), nullptr));
            } else {
                enum_members.push_back(std::make_unique<EnumMember>(std::move(mname), std::make_unique<int>(std::stoi(mval))));
            }
        }
        return std::make_unique<EnumDecl>(std::move(name), std::move(enum_members));
    }

    std::unique_ptr<StructDecl> struct_decl(std::string name, std::vector<std::tuple<std::string, std::string, std::string>> fields,
                                            std::vector<std::unique_ptr<Decl>> methods = {}) {
        std::vector<std::unique_ptr<StructField>> struct_fields;
        for (auto &[fname, ftype, fdefault] : fields) {
            std::unique_ptr<Expr> init = nullptr;
            if (!fdefault.empty()) {
                if (!fdefault.empty() && fdefault[0] == '"') {
                    {
                        auto s = fdefault.substr(1, fdefault.size() - 2);
                        auto n = std::make_unique<StringLiteral>(std::move(s));
                        init = std::move(n);
                    }
                } else {
                    try {
                        init = std::make_unique<IntLiteral>(std::stoi(fdefault));
                    } catch (...) { /* not a simple literal */
                    }
                }
            }
            struct_fields.push_back(std::make_unique<StructField>(std::move(fname), TypeInfo::parse(ftype), std::move(init)));
        }
        return std::make_unique<StructDecl>(std::move(name), std::move(struct_fields), std::move(methods));
    }

    std::unique_ptr<Program> program(std::vector<std::unique_ptr<Decl>> items) { return std::make_unique<Program>(std::move(items)); }
};
} // namespace trust