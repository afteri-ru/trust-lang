#pragma once

#include "parser/token.hpp"
#include "parser/token_info.hpp"
#include "types/type_info.hpp"
#include "gencpp/ast_visitor.hpp"
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace trust {

// --- Forward declarations ---
struct Expr;
struct Stmt;
struct Decl;

// --- Base interface ---
struct AstVisitable {
    virtual ~AstVisitable() = default;
    virtual void accept(AstVisitor *v) const = 0;
};

// --- Base AST node ---
struct AstNodeBase : AstVisitable {
    SourceLoc loc;
    std::optional<TokenInfo> source;
    virtual ~AstNodeBase() = default;
    virtual ParserToken::Kind token_kind() const = 0;
    void set_source(const TokenInfo &ti);

    template <typename T>
    [[nodiscard]] bool is() const noexcept {
        return token_kind() == T::static_token_kind();
    }
    template <typename T>
    [[nodiscard]] T *as() noexcept {
        return is<T>() ? static_cast<T *>(this) : nullptr;
    }
    template <typename T>
    [[nodiscard]] const T *as() const noexcept {
        return is<T>() ? static_cast<const T *>(this) : nullptr;
    }
};

using AstNodePtr = std::unique_ptr<AstNodeBase>;
using AstNodeSequence = std::vector<AstNodePtr>;

// --- CRTP base: inherits from category tag (Expr/Stmt/Decl) so it can override token_kind() ---
template <typename Derived, ParserToken::Kind Kind, typename CategoryBase = AstNodeBase>
struct AstNodeImpl : CategoryBase {
    void accept(AstVisitor *v) const override { v->visit(static_cast<const Derived *>(this)); }
    static constexpr ParserToken::Kind static_token_kind() noexcept { return Kind; }
    ParserToken::Kind token_kind() const override { return Kind; }
};

// --- Type Resolution ---
struct TypeResolution {
    std::unordered_map<const Expr *, TypeInfo> expr_types;
    void set_type(const Expr *e, TypeInfo ti);
    std::optional<TypeInfo> get_type(const Expr *e) const;
    bool has_type(const Expr *e) const;
};

// --- Type Traits ---
struct AstTypeTraits {
    [[nodiscard]] static constexpr bool is_expr_node(ParserToken::Kind t) noexcept { return ParserToken::is_expr(t); }
    [[nodiscard]] static constexpr bool is_stmt_node(ParserToken::Kind t) noexcept { return ParserToken::is_stmt(t); }
    [[nodiscard]] static constexpr bool is_decl_node(ParserToken::Kind t) noexcept { return ParserToken::is_decl(t); }
    [[nodiscard]] static constexpr bool is_root_node(ParserToken::Kind t) noexcept { return ParserToken::is_root(t); }
    [[nodiscard]] static constexpr TokenCategory node_category(ParserToken::Kind t) noexcept {
        auto flags = ParserToken::flags(t);
        if ((flags & TokenFlag::Expr) != TokenFlag{})
            return TokenCategory::Expr;
        if ((flags & TokenFlag::Stmt) != TokenFlag{})
            return TokenCategory::Stmt;
        if ((flags & TokenFlag::Decl) != TokenFlag{})
            return TokenCategory::Decl;
        if ((flags & TokenFlag::Root) != TokenFlag{})
            return TokenCategory::Root;
        return TokenCategory::None;
    }
    [[nodiscard]] static std::string to_string(ParserToken::Kind t);
    [[nodiscard]] static ParserToken::Kind from_string(const std::string &s);
};

// --- Enums ---
enum class BinOp { Add, Sub, Mul, Div, Eq, Ne, Lt, Le, Gt, Ge, And, Or };

// --- Type aliases ---
using BlockItem = std::unique_ptr<AstNodeBase>;
using BlockBody = std::vector<BlockItem>;

// ============================================================================
// EXPRESSION NODES
// ============================================================================

struct Expr : public AstNodeBase {};

struct IntLiteral : AstNodeImpl<IntLiteral, ParserToken::Kind::IntLiteral, Expr> {
    int value;
    explicit IntLiteral(int v);
};

struct StringLiteral : AstNodeImpl<StringLiteral, ParserToken::Kind::StringLiteral, Expr> {
    explicit StringLiteral(std::string v);
    [[nodiscard]] const std::string &value() const;
};

struct BinaryOp : AstNodeImpl<BinaryOp, ParserToken::Kind::BinaryOp, Expr> {
    BinOp op;
    std::unique_ptr<Expr> left;
    std::unique_ptr<Expr> right;
    BinaryOp(BinOp o, std::unique_ptr<Expr> l, std::unique_ptr<Expr> r);
};

struct VarRef : AstNodeImpl<VarRef, ParserToken::Kind::VarRef, Expr> {
    std::string name;
    explicit VarRef(std::string n);
};

struct CallExpr : AstNodeImpl<CallExpr, ParserToken::Kind::CallExpr, Expr> {
    std::string name;
    std::vector<std::unique_ptr<Expr>> args;
    CallExpr(std::string n, std::vector<std::unique_ptr<Expr>> a);
};

struct EmbedExpr : AstNodeImpl<EmbedExpr, ParserToken::Kind::EmbedExpr, Expr> {
    explicit EmbedExpr(std::string v);
    [[nodiscard]] const std::string &value() const;
};

struct EnumLiteral : AstNodeImpl<EnumLiteral, ParserToken::Kind::EnumLiteral, Expr> {
    std::string enum_name;
    std::string member_name;
    EnumLiteral(std::string en, std::string mn);
};

struct MemberAccess : AstNodeImpl<MemberAccess, ParserToken::Kind::MemberAccess, Expr> {
    std::unique_ptr<Expr> object;
    std::string field;
    MemberAccess(std::unique_ptr<Expr> obj, std::string f);
};

struct ArrayAccess : AstNodeImpl<ArrayAccess, ParserToken::Kind::ArrayAccess, Expr> {
    std::unique_ptr<Expr> array;
    std::unique_ptr<Expr> index;
    ArrayAccess(std::unique_ptr<Expr> arr, std::unique_ptr<Expr> idx);
};

struct ArrayInit : AstNodeImpl<ArrayInit, ParserToken::Kind::ArrayInit, Expr> {
    std::vector<std::unique_ptr<Expr>> elements;
    TypeKind element_type;
    ArrayInit(std::vector<std::unique_ptr<Expr>> elems, TypeKind et);
};

struct CastExpr : AstNodeImpl<CastExpr, ParserToken::Kind::CastExpr, Expr> {
    std::unique_ptr<Expr> expr;
    TypeInfo target_type;
    CastExpr(std::unique_ptr<Expr> e, TypeInfo tt);
};

struct RefMakeExpr : AstNodeImpl<RefMakeExpr, ParserToken::Kind::RefMakeExpr, Expr> {
    std::unique_ptr<Expr> arg;
    explicit RefMakeExpr(std::unique_ptr<Expr> a);
};

struct RefTakeExpr : AstNodeImpl<RefTakeExpr, ParserToken::Kind::RefTakeExpr, Expr> {
    std::unique_ptr<Expr> arg;
    explicit RefTakeExpr(std::unique_ptr<Expr> a);
};

// ============================================================================
// STATEMENT NODES
// ============================================================================

struct Stmt : public AstNodeBase {};

struct AssignmentStmt : AstNodeImpl<AssignmentStmt, ParserToken::Kind::AssignmentStmt, Stmt> {
    std::unique_ptr<Expr> target;
    std::unique_ptr<Expr> value;
    AssignmentStmt(std::unique_ptr<Expr> t, std::unique_ptr<Expr> v);
};

struct ReturnStmt : AstNodeImpl<ReturnStmt, ParserToken::Kind::ReturnStmt, Stmt> {
    std::unique_ptr<Expr> value;
    explicit ReturnStmt(std::unique_ptr<Expr> v);
};

struct ExprStmt : AstNodeImpl<ExprStmt, ParserToken::Kind::ExprStmt, Stmt> {
    std::unique_ptr<Expr> expr;
    explicit ExprStmt(std::unique_ptr<Expr> e);
};

struct BlockStmt : AstNodeImpl<BlockStmt, ParserToken::Kind::BlockStmt, Stmt> {
    BlockBody body;
    explicit BlockStmt(BlockBody b);
};

struct IfStmt : AstNodeImpl<IfStmt, ParserToken::Kind::IfStmt, Stmt> {
    std::unique_ptr<Expr> condition;
    BlockBody then_body;
    std::unique_ptr<IfStmt> else_if;
    std::unique_ptr<BlockStmt> else_block;
    IfStmt(std::unique_ptr<Expr> cond, BlockBody tb, std::unique_ptr<IfStmt> elif, std::unique_ptr<BlockStmt> eb);
};

struct WhileElseBlock : AstNodeImpl<WhileElseBlock, ParserToken::Kind::WhileElseBlock, Stmt> {
    BlockBody body;
    explicit WhileElseBlock(BlockBody b);
};

struct WhileStmt : AstNodeImpl<WhileStmt, ParserToken::Kind::WhileStmt, Stmt> {
    std::unique_ptr<Expr> condition;
    BlockBody body;
    BlockBody else_body;
    WhileStmt(std::unique_ptr<Expr> cond, BlockBody b, BlockBody eb = BlockBody{});
};

struct DoWhileStmt : AstNodeImpl<DoWhileStmt, ParserToken::Kind::DoWhileStmt, Stmt> {
    BlockBody body;
    std::unique_ptr<Expr> condition;
    DoWhileStmt(BlockBody b, std::unique_ptr<Expr> cond);
};

struct BreakStmt : AstNodeImpl<BreakStmt, ParserToken::Kind::BreakStmt, Stmt> {
    BreakStmt();
};

struct ContinueStmt : AstNodeImpl<ContinueStmt, ParserToken::Kind::ContinueStmt, Stmt> {
    ContinueStmt();
};

struct CatchBlock : AstNodeImpl<CatchBlock, ParserToken::Kind::CatchBlock, Stmt> {
    TypeInfo exception_type;
    std::string var_name;
    BlockBody body;
    CatchBlock(TypeInfo t, std::string n, BlockBody b);
};

struct TryCatchStmt : AstNodeImpl<TryCatchStmt, ParserToken::Kind::TryCatchStmt, Stmt> {
    BlockBody try_body;
    std::unique_ptr<CatchBlock> catch_block;
    TryCatchStmt(BlockBody tb, std::unique_ptr<CatchBlock> cb);
};

struct ThrowStmt : AstNodeImpl<ThrowStmt, ParserToken::Kind::ThrowStmt, Stmt> {
    std::unique_ptr<Expr> value;
    explicit ThrowStmt(std::unique_ptr<Expr> v);
};

struct MatchingCase : AstNodeImpl<MatchingCase, ParserToken::Kind::MatchingCase, Stmt> {
    std::unique_ptr<Expr> pattern;
    BlockBody body;
    MatchingCase(std::unique_ptr<Expr> p, BlockBody b);
};

struct MatchingStmt : AstNodeImpl<MatchingStmt, ParserToken::Kind::MatchingStmt, Stmt> {
    std::unique_ptr<Expr> expression;
    std::vector<std::unique_ptr<MatchingCase>> cases;
    BlockBody else_body;
    MatchingStmt(std::unique_ptr<Expr> expr, std::vector<std::unique_ptr<MatchingCase>> c, BlockBody eb);
};

// ============================================================================
// DECLARATION NODES
// ============================================================================

struct Decl : public AstNodeBase {};

struct EnumMember : AstNodeImpl<EnumMember, ParserToken::Kind::EnumMember, Decl> {
    std::string name;
    std::unique_ptr<int> value;
    EnumMember(std::string n, std::unique_ptr<int> v);
};

struct StructField : AstNodeImpl<StructField, ParserToken::Kind::StructField, Decl> {
    std::string name;
    TypeInfo type;
    std::unique_ptr<Expr> init;
    StructField(std::string n, TypeInfo t, std::unique_ptr<Expr> i);
};

struct EnumDecl : AstNodeImpl<EnumDecl, ParserToken::Kind::EnumDecl, Decl> {
    std::string name;
    std::vector<std::unique_ptr<EnumMember>> members;
    EnumDecl(std::string n, std::vector<std::unique_ptr<EnumMember>> m);
};

struct StructDecl : AstNodeImpl<StructDecl, ParserToken::Kind::StructDecl, Decl> {
    std::string name;
    std::vector<std::unique_ptr<StructField>> fields;
    std::vector<std::unique_ptr<Decl>> methods;
    StructDecl(std::string n, std::vector<std::unique_ptr<StructField>> f, std::vector<std::unique_ptr<Decl>> m);
};

struct VarDecl : AstNodeImpl<VarDecl, ParserToken::Kind::VarDecl, Decl> {
    std::string name;
    TypeInfo var_type;
    std::unique_ptr<Expr> init;
    bool type_explicitly_set;
    VarDecl(std::string n, std::unique_ptr<Expr> i);
    VarDecl(std::string n, TypeInfo t, std::unique_ptr<Expr> i);
    TypeInfo type_info() const;
    bool needs_type_inference() const;
};

struct ParamDecl : AstNodeImpl<ParamDecl, ParserToken::Kind::ParamDecl, Decl> {
    std::string name;
    TypeInfo param_type;
    ParamDecl(std::string n, TypeInfo t);
};

struct FuncDecl : AstNodeImpl<FuncDecl, ParserToken::Kind::FuncDecl, Decl> {
    std::string name;
    TypeInfo return_type;
    std::vector<std::unique_ptr<ParamDecl>> params;
    std::unique_ptr<BlockStmt> body;
    FuncDecl(std::string n, TypeKind rt, std::vector<std::unique_ptr<ParamDecl>> p, std::unique_ptr<BlockStmt> b);
    FuncDecl(std::string n, TypeInfo rt, std::vector<std::unique_ptr<ParamDecl>> p, std::unique_ptr<BlockStmt> b);
};

// ============================================================================
// PROGRAM
// ============================================================================

struct Program : AstNodeImpl<Program, ParserToken::Kind::Program> {
    std::vector<std::unique_ptr<Decl>> items;
    explicit Program(std::vector<std::unique_ptr<Decl>> i);
};

} // namespace trust