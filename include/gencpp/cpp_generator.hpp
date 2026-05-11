#pragma once

#include "gencpp/ast.hpp"
#include "gencpp/ast_visitor.hpp"
#include "diag/context.hpp"
#include <memory>
#include <sstream>
#include <string>
#include <vector>
#include <unordered_map>

namespace trust {

struct GeneratorOptions {
    LanguageVersion format = LanguageVersion::C;
    std::string module_name = "transpiled";
    std::vector<std::string> extra_imports; // modules to import
};

class CppGenerator : public AstVisitor {
  public:
    explicit CppGenerator(GeneratorOptions opts = {});

    // Set the TypeResolution from semantic analysis (optional, for type-aware generation)
    void set_type_resolution(std::shared_ptr<TypeResolution> tr) { type_res_ = std::move(tr); }

    // Set Context for source mapping (optional)
    void set_context(Context* c) { ctx_ = c; }

    // Set output FileIdx for source mapping (optional)
    void set_output_file_idx(MapperFile fid) { output_file_idx_ = fid; }

    std::string generate(const Program* program, std::string* binding_header = nullptr, const char* binding_guard = nullptr);

    void visit(const Program* node) override;
    void visit(const FuncDecl* node) override;
    void visit(const VarDecl* node) override;
    void visit(const AssignmentStmt* node) override;
    void visit(const ReturnStmt* node) override;
    void visit(const CallExpr* node) override;
    void visit(const ExprStmt* node) override;
    void visit(const IfStmt* node) override;
    void visit(const WhileStmt* node) override;
    void visit(const DoWhileStmt* node) override;
    void visit(const TryCatchStmt* node) override;
    void visit(const CatchBlock* node) override;
    void visit(const ThrowStmt* node) override;
    void visit(const MatchingStmt* node) override;
    void visit(const MatchingCase* node) override;
    void visit(const VarRef* node) override;
    void visit(const IntLiteral* node) override;
    void visit(const BinaryOp* node) override;
    void visit(const StringLiteral* node) override;
    void visit(const BlockStmt* node) override;
    void visit(const ParamDecl* node) override;

    // New: enum and struct
    void visit(const EnumDecl* node) override;
    void visit(const EnumMember* node) override;
    void visit(const StructDecl* node) override;
    void visit(const StructField* node) override;
    void visit(const EnumLiteral* node) override;
    void visit(const MemberAccess* node) override;

    // New: array support
    void visit(const ArrayAccess* node) override;
    void visit(const ArrayInit* node) override;

    // Cast expression
    void visit(const CastExpr* node) override;

    // Ref expressions
    void visit(const RefMakeExpr* node) override;
    void visit(const RefTakeExpr* node) override;

    // Embed expression
    void visit(const EmbedExpr* node) override;

    // New: loop control and while-else
    void visit(const WhileElseBlock* node) override;
    void visit(const BreakStmt* node) override;
    void visit(const ContinueStmt* node) override;

    // After calling generate(), call this to flush the remaining output
    // to Context and finalize the output.
    void finalize_output(Context& ctx, MapperFile outputIdx);

  private:
    std::ostringstream out_;
    int indent_level_;
    GeneratorOptions opts_;
    std::shared_ptr<TypeResolution> type_res_;

    // --- SourceMapping helpers (using mapStart/mapStop) ---

    // Flush accumulated out_ to Context, then start a new mapping.
    // If ctx_ is null or node->source is null, no-op.
    void map_node_begin(const AstNode* node);

    // Flush accumulated out_ to Context, then stop the current mapping.
    void map_node_end();

    // Map a name using addNameMapping on Context.
    // trustName/cppName provide the overrides; if empty, uses node->source->text.
    void add_node_name(const AstNode* node, const std::string& trustName, const std::string& cppName);

    // Flush out_ to Context via output_append (if ctx_ is set).
    void flush_out();

    void indent();
    void dispatch_block_item(const BlockItem& item);
    void visit_if_stmt(const IfStmt* node, bool is_primary);

    [[nodiscard]] std::string type_to_cpp(TypeInfo t) const { return t.cpp_name; }

    // Get resolved type for an expression node (falls back to node->type() if not set)
    [[nodiscard]] TypeKind get_expr_type(const Expr* e) const;

    void build_matching_chain(Expr* expr, const std::vector<std::unique_ptr<MatchingCase>>& cases, const BlockBody& else_body, size_t idx, bool is_primary);

    // Write module preamble (includes + module declaration)
    void write_module_preamble();

    // Label management for goto-based loop generation
    std::string unique_label();

    struct LoopLabels {
        std::string body_label;
        std::string cont_label; // continue target
        std::string end_label;
    };

    Context* ctx_ = nullptr;
    MapperFile output_file_idx_{};

    int label_counter_ = 0;
    std::vector<LoopLabels> current_loop_labels_; // stack of unnamed loops

    // Состояние текущего map_node для вложенной вложенности
    std::optional<MapperRange> current_map_range_;
};
} // namespace trust