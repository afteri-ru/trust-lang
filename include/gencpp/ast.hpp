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
#include <stdexcept>

namespace trust {

// Forward declarations
class Program;
class Expr;
class Stmt;
class Decl;
class ParamDecl;
class EnumDecl;
class StructDecl;
class EnumLiteral;
class MemberAccess;
class ArrayAccess;
class ArrayInit;
class CastExpr;
class RefMakeExpr;
class RefTakeExpr;
class EnumMember;
class StructField;
class CatchBlock;
class MatchingCase;
class WhileElseBlock;
class BreakStmt;
class ContinueStmt;

// Base interface for all visitable AST nodes
struct AstVisitable {
    virtual ~AstVisitable() = default;
    virtual void accept(AstVisitor *v) const = 0;
    virtual ParserToken::Kind token_kind() const = 0;
};

// --- Type Resolution (separate from AST nodes) ---
struct TypeResolution {
    std::unordered_map<const Expr *, TypeInfo> expr_types;

    void set_type(const Expr *e, TypeInfo ti) { expr_types[e] = std::move(ti); }

    std::optional<TypeInfo> get_type(const Expr *e) const {
        auto it = expr_types.find(e);
        if (it != expr_types.end())
            return it->second;
        return std::nullopt;
    }

    bool has_type(const Expr *e) const { return expr_types.count(e) > 0; }
};

// Base AST node
struct AstNodeBase : public AstVisitable {
    SourceLoc loc;
    std::optional<TokenInfo> source;

    void set_source(const TokenInfo &ti) {
        source = ti;
        loc = ti.range.begin;
    }

    ParserToken::Kind token_kind() const override {
        return source ? source->kind : ParserToken::Kind::END;
    }
};

using AstNodePtr = std::unique_ptr<AstNodeBase>;
using AstNodeSequence = std::vector<AstNodePtr>;

// AstTypeTraits — wraps ParserToken for AST node type operations
struct AstTypeTraits {
    [[nodiscard]] static constexpr bool is_expr_node(ParserToken::Kind t) noexcept {
        return ParserToken::is_expr(t);
    }

    [[nodiscard]] static constexpr bool is_stmt_node(ParserToken::Kind t) noexcept {
        return ParserToken::is_stmt(t);
    }

    [[nodiscard]] static constexpr bool is_decl_node(ParserToken::Kind t) noexcept {
        return ParserToken::is_decl(t);
    }

    [[nodiscard]] static constexpr bool is_root_node(ParserToken::Kind t) noexcept {
        return ParserToken::is_root(t);
    }

    [[nodiscard]] static constexpr TokenCategory node_category(ParserToken::Kind t) noexcept {
        auto flags = ParserToken::flags(t);
        if ((flags & TokenFlag::Expr) != TokenFlag{}) return TokenCategory::Expr;
        if ((flags & TokenFlag::Stmt) != TokenFlag{}) return TokenCategory::Stmt;
        if ((flags & TokenFlag::Decl) != TokenFlag{}) return TokenCategory::Decl;
        if ((flags & TokenFlag::Root) != TokenFlag{}) return TokenCategory::Root;
        return TokenCategory::None;
    }

    [[nodiscard]] static std::string to_string(ParserToken::Kind t);
    [[nodiscard]] static ParserToken::Kind from_string(const std::string &s);
};

// --- Expressions ---
struct Expr : public AstNodeBase {};

struct IntLiteral : public Expr {
    int value;
    explicit IntLiteral(int v) : value(v) {}
    void accept(AstVisitor *v) const override { v->visit(this); }
    ParserToken::Kind token_kind() const override { return ParserToken::Kind::IntLiteral; }
};

struct StringLiteral : public Expr {
    std::string value;
    explicit StringLiteral(std::string v) : value(std::move(v)) {}
    void accept(AstVisitor *v) const override { v->visit(this); }
    ParserToken::Kind token_kind() const override { return ParserToken::Kind::StringLiteral; }
};

enum class BinOp { Add, Sub, Mul, Div, Eq, Ne, Lt, Le, Gt, Ge, And, Or };

struct BinOpUtils {
    [[nodiscard]] static std::string to_string(BinOp op);
    [[nodiscard]] static BinOp from_string(const std::string &s);
};

struct StringUtils {
    [[nodiscard]] static std::string escape(const std::string &s);
    [[nodiscard]] static std::string unescape(const std::string &s);
};

struct BinaryOp : public Expr {
    BinOp op;
    std::unique_ptr<Expr> left;
    std::unique_ptr<Expr> right;
    BinaryOp(BinOp o, std::unique_ptr<Expr> l, std::unique_ptr<Expr> r) : op(o), left(std::move(l)), right(std::move(r)) {}
    void accept(AstVisitor *v) const override { v->visit(this); }
    ParserToken::Kind token_kind() const override { return ParserToken::Kind::BinaryOp; }
};

struct VarRef : public Expr {
    std::string name;
    explicit VarRef(std::string n) : name(std::move(n)) {}
    void accept(AstVisitor *v) const override { v->visit(this); }
    ParserToken::Kind token_kind() const override { return ParserToken::Kind::VarRef; }
};

struct CallExpr : public Expr {
    std::string name;
    std::vector<std::unique_ptr<Expr>> args;
    explicit CallExpr(std::string n, std::vector<std::unique_ptr<Expr>> a) : name(std::move(n)), args(std::move(a)) {}
    void accept(AstVisitor *v) const override { v->visit(this); }
    ParserToken::Kind token_kind() const override { return ParserToken::Kind::CallExpr; }
};

// --- Statements ---
struct Stmt : public AstNodeBase {};

// --- Declarations ---
struct Decl : public AstNodeBase {};

struct EnumMember : public Decl {
    std::string name;
    std::unique_ptr<int> value;

    EnumMember(std::string n, std::unique_ptr<int> v = nullptr) : name(std::move(n)), value(std::move(v)) {}

    void accept(AstVisitor *v) const override { v->visit(this); }
    ParserToken::Kind token_kind() const override { return ParserToken::Kind::EnumMember; }

    EnumMember(EnumMember &&) = default;
    EnumMember &operator=(EnumMember &&) = default;
};

struct StructField : public Decl {
    std::string name;
    TypeInfo type;
    std::unique_ptr<Expr> init;

    StructField(std::string n, TypeInfo t, std::unique_ptr<Expr> i = nullptr) : name(std::move(n)), type(std::move(t)), init(std::move(i)) {}

    void accept(AstVisitor *v) const override { v->visit(this); }
    ParserToken::Kind token_kind() const override { return ParserToken::Kind::StructField; }

    StructField(StructField &&) = default;
    StructField &operator=(StructField &&) = default;
};

struct EnumDecl : public Decl {
    std::string name;
    std::vector<std::unique_ptr<EnumMember>> members;

    explicit EnumDecl(std::string n, std::vector<std::unique_ptr<EnumMember>> m = {}) : name(std::move(n)), members(std::move(m)) {}

    void accept(AstVisitor *v) const override { v->visit(this); }
    ParserToken::Kind token_kind() const override { return ParserToken::Kind::EnumDecl; }
};

struct StructDecl : public Decl {
    std::string name;
    std::vector<std::unique_ptr<StructField>> fields;
    std::vector<std::unique_ptr<Decl>> methods;

    StructDecl(std::string n, std::vector<std::unique_ptr<StructField>> f, std::vector<std::unique_ptr<Decl>> m = {})
        : name(std::move(n)), fields(std::move(f)), methods(std::move(m)) {}

    void accept(AstVisitor *v) const override { v->visit(this); }
    ParserToken::Kind token_kind() const override { return ParserToken::Kind::StructDecl; }
};

struct EnumLiteral : public Expr {
    std::string enum_name;
    std::string member_name;
    EnumLiteral(std::string en, std::string mn) : enum_name(std::move(en)), member_name(std::move(mn)) {}
    void accept(AstVisitor *v) const override { v->visit(this); }
    ParserToken::Kind token_kind() const override { return ParserToken::Kind::EnumLiteral; }
};

struct MemberAccess : public Expr {
    std::unique_ptr<Expr> object;
    std::string field;
    MemberAccess(std::unique_ptr<Expr> obj, std::string f) : object(std::move(obj)), field(std::move(f)) {}
    void accept(AstVisitor *v) const override { v->visit(this); }
    ParserToken::Kind token_kind() const override { return ParserToken::Kind::MemberAccess; }
};

struct ArrayAccess : public Expr {
    std::unique_ptr<Expr> array;
    std::unique_ptr<Expr> index;
    ArrayAccess(std::unique_ptr<Expr> arr, std::unique_ptr<Expr> idx) : array(std::move(arr)), index(std::move(idx)) {}
    void accept(AstVisitor *v) const override { v->visit(this); }
    ParserToken::Kind token_kind() const override { return ParserToken::Kind::ArrayAccess; }
};

struct ArrayInit : public Expr {
    std::vector<std::unique_ptr<Expr>> elements;
    TypeKind element_type;
    explicit ArrayInit(std::vector<std::unique_ptr<Expr>> elems, TypeKind et = TypeKind::Int) : elements(std::move(elems)), element_type(et) {}
    void accept(AstVisitor *v) const override { v->visit(this); }
    ParserToken::Kind token_kind() const override { return ParserToken::Kind::ArrayInit; }
};

struct CastExpr : public Expr {
    std::unique_ptr<Expr> expr;
    TypeInfo target_type;
    CastExpr(std::unique_ptr<Expr> e, TypeInfo tt) : expr(std::move(e)), target_type(std::move(tt)) {}
    void accept(AstVisitor *v) const override { v->visit(this); }
    ParserToken::Kind token_kind() const override { return ParserToken::Kind::CastExpr; }
};

struct RefMakeExpr : public Expr {
    std::unique_ptr<Expr> arg;
    explicit RefMakeExpr(std::unique_ptr<Expr> a) : arg(std::move(a)) {}
    void accept(AstVisitor *v) const override { v->visit(this); }
    ParserToken::Kind token_kind() const override { return ParserToken::Kind::RefMakeExpr; }
};

struct RefTakeExpr : public Expr {
    std::unique_ptr<Expr> arg;
    explicit RefTakeExpr(std::unique_ptr<Expr> a) : arg(std::move(a)) {}
    void accept(AstVisitor *v) const override { v->visit(this); }
    ParserToken::Kind token_kind() const override { return ParserToken::Kind::RefTakeExpr; }
};

struct AssignmentStmt : public Stmt {
    std::unique_ptr<Expr> target;
    std::unique_ptr<Expr> value;
    AssignmentStmt(std::unique_ptr<Expr> t, std::unique_ptr<Expr> v) : target(std::move(t)), value(std::move(v)) {}
    void accept(AstVisitor *v) const override { v->visit(this); }
    ParserToken::Kind token_kind() const override { return ParserToken::Kind::AssignmentStmt; }
};

struct ReturnStmt : public Stmt {
    std::unique_ptr<Expr> value;
    explicit ReturnStmt(std::unique_ptr<Expr> v) : value(std::move(v)) {}
    void accept(AstVisitor *v) const override { v->visit(this); }
    ParserToken::Kind token_kind() const override { return ParserToken::Kind::ReturnStmt; }
};

struct ExprStmt : public Stmt {
    std::unique_ptr<Expr> expr;
    explicit ExprStmt(std::unique_ptr<Expr> e) : expr(std::move(e)) {}
    void accept(AstVisitor *v) const override { v->visit(this); }
    ParserToken::Kind token_kind() const override { return ParserToken::Kind::ExprStmt; }
};

using BlockItem = std::unique_ptr<AstNodeBase>;
using BlockBody = std::vector<BlockItem>;

struct BlockStmt : public Stmt {
    BlockBody body;
    explicit BlockStmt(BlockBody b) : body(std::move(b)) {}
    void accept(AstVisitor *v) const override { v->visit(this); }
    ParserToken::Kind token_kind() const override { return ParserToken::Kind::BlockStmt; }
};

struct IfStmt : public Stmt {
    std::unique_ptr<Expr> condition;
    BlockBody then_body;
    std::unique_ptr<IfStmt> else_if;
    std::unique_ptr<BlockStmt> else_block;

    IfStmt(std::unique_ptr<Expr> cond, BlockBody tb, std::unique_ptr<IfStmt> elif = nullptr, std::unique_ptr<BlockStmt> eb = nullptr)
        : condition(std::move(cond)), then_body(std::move(tb)), else_if(std::move(elif)), else_block(std::move(eb)) {}

    void accept(AstVisitor *v) const override { v->visit(this); }
    ParserToken::Kind token_kind() const override { return ParserToken::Kind::IfStmt; }
};

struct WhileElseBlock : public Stmt {
    BlockBody body;
    explicit WhileElseBlock(BlockBody b) : body(std::move(b)) {}
    void accept(AstVisitor *v) const override { v->visit(this); }
    ParserToken::Kind token_kind() const override { return ParserToken::Kind::WhileElseBlock; }
};

struct WhileStmt : public Stmt {
    std::unique_ptr<Expr> condition;
    BlockBody body;
    BlockBody else_body;

    WhileStmt(std::unique_ptr<Expr> cond, BlockBody b, BlockBody eb = BlockBody{})
        : condition(std::move(cond)), body(std::move(b)), else_body(std::move(eb)) {}

    void accept(AstVisitor *v) const override { v->visit(this); }
    ParserToken::Kind token_kind() const override { return ParserToken::Kind::WhileStmt; }
};

struct DoWhileStmt : public Stmt {
    BlockBody body;
    std::unique_ptr<Expr> condition;

    DoWhileStmt(BlockBody b, std::unique_ptr<Expr> cond)
        : body(std::move(b)), condition(std::move(cond)) {}

    void accept(AstVisitor *v) const override { v->visit(this); }
    ParserToken::Kind token_kind() const override { return ParserToken::Kind::DoWhileStmt; }
};

struct BreakStmt : public Stmt {
    explicit BreakStmt() = default;
    void accept(AstVisitor *v) const override { v->visit(this); }
    ParserToken::Kind token_kind() const override { return ParserToken::Kind::BreakStmt; }
};

struct ContinueStmt : public Stmt {
    explicit ContinueStmt() = default;
    void accept(AstVisitor *v) const override { v->visit(this); }
    ParserToken::Kind token_kind() const override { return ParserToken::Kind::ContinueStmt; }
};

struct CatchBlock : public Stmt {
    TypeInfo exception_type;
    std::string var_name;
    BlockBody body;

    CatchBlock(TypeInfo t, std::string n, BlockBody b) : exception_type(std::move(t)), var_name(std::move(n)), body(std::move(b)) {}

    void accept(AstVisitor *v) const override { v->visit(this); }
    ParserToken::Kind token_kind() const override { return ParserToken::Kind::CatchBlock; }
};

struct TryCatchStmt : public Stmt {
    BlockBody try_body;
    std::unique_ptr<CatchBlock> catch_block;

    TryCatchStmt(BlockBody tb, std::unique_ptr<CatchBlock> cb) : try_body(std::move(tb)), catch_block(std::move(cb)) {}

    void accept(AstVisitor *v) const override { v->visit(this); }
    ParserToken::Kind token_kind() const override { return ParserToken::Kind::TryCatchStmt; }
};

struct ThrowStmt : public Stmt {
    std::unique_ptr<Expr> value;
    explicit ThrowStmt(std::unique_ptr<Expr> v) : value(std::move(v)) {}
    void accept(AstVisitor *v) const override { v->visit(this); }
    ParserToken::Kind token_kind() const override { return ParserToken::Kind::ThrowStmt; }
};

struct MatchingCase : public Stmt {
    std::unique_ptr<Expr> pattern;
    BlockBody body;

    MatchingCase(std::unique_ptr<Expr> p, BlockBody b) : pattern(std::move(p)), body(std::move(b)) {}

    void accept(AstVisitor *v) const override { v->visit(this); }
    ParserToken::Kind token_kind() const override { return ParserToken::Kind::MatchingCase; }
};

struct MatchingStmt : public Stmt {
    std::unique_ptr<Expr> expression;
    std::vector<std::unique_ptr<MatchingCase>> cases;
    BlockBody else_body;

    MatchingStmt(std::unique_ptr<Expr> expr, std::vector<std::unique_ptr<MatchingCase>> c, BlockBody eb)
        : expression(std::move(expr)), cases(std::move(c)), else_body(std::move(eb)) {}

    void accept(AstVisitor *v) const override { v->visit(this); }
    ParserToken::Kind token_kind() const override { return ParserToken::Kind::MatchingStmt; }
};

struct VarDecl : public Decl {
    std::string name;
    TypeInfo var_type;
    std::unique_ptr<Expr> init;
    bool type_explicitly_set = false;

    VarDecl(std::string n, std::unique_ptr<Expr> i) : name(std::move(n)), var_type(), init(std::move(i)), type_explicitly_set(false) {}

    VarDecl(std::string n, TypeInfo t, std::unique_ptr<Expr> i) : name(std::move(n)), var_type(std::move(t)), init(std::move(i)), type_explicitly_set(true) {}

    void accept(AstVisitor *v) const override { v->visit(this); }
    TypeInfo type_info() const { return var_type; }
    ParserToken::Kind token_kind() const override { return ParserToken::Kind::VarDecl; }
    bool needs_type_inference() const { return !type_explicitly_set; }
};

struct ParamDecl : public Decl {
    std::string name;
    TypeInfo param_type;
    ParamDecl(std::string n, TypeInfo t) : name(std::move(n)), param_type(std::move(t)) {}
    void accept(AstVisitor *v) const override { v->visit(this); }
    ParserToken::Kind token_kind() const override { return ParserToken::Kind::ParamDecl; }
};

struct FuncDecl : public Decl {
    std::string name;
    TypeInfo return_type;
    std::vector<std::unique_ptr<ParamDecl>> params;
    std::unique_ptr<BlockStmt> body;

    FuncDecl(std::string n, TypeKind rt, std::vector<std::unique_ptr<ParamDecl>> p, std::unique_ptr<BlockStmt> b)
        : name(std::move(n)), return_type(TypeInfo::builtin(rt)), params(std::move(p)), body(std::move(b)) {}

    FuncDecl(std::string n, TypeInfo rt, std::vector<std::unique_ptr<ParamDecl>> p, std::unique_ptr<BlockStmt> b)
        : name(std::move(n)), return_type(std::move(rt)), params(std::move(p)), body(std::move(b)) {}

    void accept(AstVisitor *v) const override { v->visit(this); }
    ParserToken::Kind token_kind() const override { return ParserToken::Kind::FuncDecl; }
};

struct Program : public AstVisitable {
    std::vector<std::unique_ptr<Decl>> items;
    explicit Program(std::vector<std::unique_ptr<Decl>> i) : items(std::move(i)) {}
    void accept(AstVisitor *v) const override { v->visit(this); }
    ParserToken::Kind token_kind() const override { return ParserToken::Kind::Program; }
};

} // namespace trust
