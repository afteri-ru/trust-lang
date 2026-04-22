#include "gencpp/ast.hpp"
#include "types/type_info.hpp"
#include <stdexcept>
#include <format>

namespace trust {

// ============================================================================
// TypeResolution
// ============================================================================

void TypeResolution::set_type(const Expr *e, TypeInfo ti) {
    expr_types[e] = std::move(ti);
}
std::optional<TypeInfo> TypeResolution::get_type(const Expr *e) const {
    auto it = expr_types.find(e);
    if (it != expr_types.end())
        return it->second;
    return std::nullopt;
}
bool TypeResolution::has_type(const Expr *e) const {
    return expr_types.count(e) > 0;
}

// ============================================================================
// AstNodeBase
// ============================================================================

void AstNodeBase::set_source(const TokenInfo &ti) {
    source = ti;
    loc = ti.range.begin;
}

// ============================================================================
// AstTypeTraits
// ============================================================================

std::string AstTypeTraits::to_string(ParserToken::Kind t) {
    return std::string(ParserToken::name(t));
}
ParserToken::Kind AstTypeTraits::from_string(const std::string &s) {
    if (auto *pk = ParserToken::from_name(s))
        return *pk;
    throw std::invalid_argument("Unknown node type: '" + s + "'");
}

// ============================================================================
// EXPRESSION NODES — constructors and custom methods only
// ============================================================================

IntLiteral::IntLiteral(int v) : value(v) {
}
StringLiteral::StringLiteral(std::string v) {
    source = TokenInfo(ParserToken::Kind::StringLiteral, {}, std::move(v));
}
const std::string &StringLiteral::value() const {
    static const std::string empty{};
    return source ? source->text : empty;
}
BinaryOp::BinaryOp(BinOp o, std::unique_ptr<Expr> l, std::unique_ptr<Expr> r) : op(o), left(std::move(l)), right(std::move(r)) {
}
VarRef::VarRef(std::string n) : name(std::move(n)) {
}
CallExpr::CallExpr(std::string n, std::vector<std::unique_ptr<Expr>> a) : name(std::move(n)), args(std::move(a)) {
}
EmbedExpr::EmbedExpr(std::string v) {
    source = TokenInfo(ParserToken::Kind::EmbedExpr, {}, std::move(v));
}
const std::string &EmbedExpr::value() const {
    static const std::string empty{};
    return source ? source->text : empty;
}
EnumLiteral::EnumLiteral(std::string en, std::string mn) : enum_name(std::move(en)), member_name(std::move(mn)) {
}
MemberAccess::MemberAccess(std::unique_ptr<Expr> obj, std::string f) : object(std::move(obj)), field(std::move(f)) {
}
ArrayAccess::ArrayAccess(std::unique_ptr<Expr> arr, std::unique_ptr<Expr> idx) : array(std::move(arr)), index(std::move(idx)) {
}
ArrayInit::ArrayInit(std::vector<std::unique_ptr<Expr>> elems, TypeKind et) : elements(std::move(elems)), element_type(et) {
}
CastExpr::CastExpr(std::unique_ptr<Expr> e, TypeInfo tt) : expr(std::move(e)), target_type(std::move(tt)) {
}
RefMakeExpr::RefMakeExpr(std::unique_ptr<Expr> a) : arg(std::move(a)) {
}
RefTakeExpr::RefTakeExpr(std::unique_ptr<Expr> a) : arg(std::move(a)) {
}

// ============================================================================
// STATEMENT NODES — constructors only
// ============================================================================

AssignmentStmt::AssignmentStmt(std::unique_ptr<Expr> t, std::unique_ptr<Expr> v) : target(std::move(t)), value(std::move(v)) {
}
ReturnStmt::ReturnStmt(std::unique_ptr<Expr> v) : value(std::move(v)) {
}
ExprStmt::ExprStmt(std::unique_ptr<Expr> e) : expr(std::move(e)) {
}
BlockStmt::BlockStmt(BlockBody b) : body(std::move(b)) {
}
IfStmt::IfStmt(std::unique_ptr<Expr> cond, BlockBody tb, std::unique_ptr<IfStmt> elif, std::unique_ptr<BlockStmt> eb)
    : condition(std::move(cond)), then_body(std::move(tb)), else_if(std::move(elif)), else_block(std::move(eb)) {
}
WhileElseBlock::WhileElseBlock(BlockBody b) : body(std::move(b)) {
}
WhileStmt::WhileStmt(std::unique_ptr<Expr> cond, BlockBody b, BlockBody eb) : condition(std::move(cond)), body(std::move(b)), else_body(std::move(eb)) {
}
DoWhileStmt::DoWhileStmt(BlockBody b, std::unique_ptr<Expr> cond) : body(std::move(b)), condition(std::move(cond)) {
}
BreakStmt::BreakStmt() = default;
ContinueStmt::ContinueStmt() = default;
CatchBlock::CatchBlock(TypeInfo t, std::string n, BlockBody b) : exception_type(std::move(t)), var_name(std::move(n)), body(std::move(b)) {
}
TryCatchStmt::TryCatchStmt(BlockBody tb, std::unique_ptr<CatchBlock> cb) : try_body(std::move(tb)), catch_block(std::move(cb)) {
}
ThrowStmt::ThrowStmt(std::unique_ptr<Expr> v) : value(std::move(v)) {
}
MatchingCase::MatchingCase(std::unique_ptr<Expr> p, BlockBody b) : pattern(std::move(p)), body(std::move(b)) {
}
MatchingStmt::MatchingStmt(std::unique_ptr<Expr> expr, std::vector<std::unique_ptr<MatchingCase>> c, BlockBody eb)
    : expression(std::move(expr)), cases(std::move(c)), else_body(std::move(eb)) {
}

// ============================================================================
// DECLARATION NODES — constructors and custom methods only
// ============================================================================

EnumMember::EnumMember(std::string n, std::unique_ptr<int> v) : name(std::move(n)), value(std::move(v)) {
}
StructField::StructField(std::string n, TypeInfo t, std::unique_ptr<Expr> i) : name(std::move(n)), type(std::move(t)), init(std::move(i)) {
}
EnumDecl::EnumDecl(std::string n, std::vector<std::unique_ptr<EnumMember>> m) : name(std::move(n)), members(std::move(m)) {
}
StructDecl::StructDecl(std::string n, std::vector<std::unique_ptr<StructField>> f, std::vector<std::unique_ptr<Decl>> m)
    : name(std::move(n)), fields(std::move(f)), methods(std::move(m)) {
}
VarDecl::VarDecl(std::string n, std::unique_ptr<Expr> i) : name(std::move(n)), var_type(), init(std::move(i)), type_explicitly_set(false) {
}
VarDecl::VarDecl(std::string n, TypeInfo t, std::unique_ptr<Expr> i)
    : name(std::move(n)), var_type(std::move(t)), init(std::move(i)), type_explicitly_set(true) {
}
TypeInfo VarDecl::type_info() const {
    return var_type;
}
bool VarDecl::needs_type_inference() const {
    return !type_explicitly_set;
}
ParamDecl::ParamDecl(std::string n, TypeInfo t) : name(std::move(n)), param_type(std::move(t)) {
}
FuncDecl::FuncDecl(std::string n, TypeKind rt, std::vector<std::unique_ptr<ParamDecl>> p, std::unique_ptr<BlockStmt> b)
    : name(std::move(n)), return_type(TypeInfo::builtin(rt)), params(std::move(p)), body(std::move(b)) {
}
FuncDecl::FuncDecl(std::string n, TypeInfo rt, std::vector<std::unique_ptr<ParamDecl>> p, std::unique_ptr<BlockStmt> b)
    : name(std::move(n)), return_type(std::move(rt)), params(std::move(p)), body(std::move(b)) {
}

// ============================================================================
// PROGRAM — constructors only
// ============================================================================

Program::Program(std::vector<std::unique_ptr<Decl>> i) : items(std::move(i)) {
}

} // namespace trust