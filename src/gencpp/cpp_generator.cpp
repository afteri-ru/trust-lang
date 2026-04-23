#include "gencpp/cpp_generator.hpp"
#include "parser/mmproc.hpp"
#include <vector>
#include <algorithm>
#include <unordered_set>

using namespace trust;

// --- TypeUsageCollector — collects all TypeKind values used in the program ---
struct TypeUsageCollector : AstVisitor {
    std::unordered_set<TypeKind> used_types;

    void dispatch_block(const BlockItem &item) {
        if (!item)
            return;
        item->accept(this);
    }

    void visit(const Program *n) override {
        for (const auto &i : n->items)
            i->accept(this);
    }
    void visit(const FuncDecl *n) override {
        used_types.insert(n->return_type.kind);
        for (const auto &p : n->params)
            used_types.insert(p->param_type.kind);
        if (n->body)
            n->body->accept(this);
    }
    void visit(const VarDecl *n) override {
        used_types.insert(n->type_info().kind);
        if (n->init)
            n->init->accept(this);
    }
    void visit(const BlockStmt *n) override {
        for (const auto &it : n->body)
            dispatch_block(it);
    }
    void visit(const IfStmt *n) override {
        if (n->condition)
            n->condition->accept(this);
        for (const auto &it : n->then_body)
            dispatch_block(it);
        if (n->else_if)
            n->else_if->accept(this);
        if (n->else_block)
            n->else_block->accept(this);
    }
    void visit(const WhileStmt *n) override {
        if (n->condition)
            n->condition->accept(this);
        for (const auto &it : n->body)
            dispatch_block(it);
        if (!n->else_body.empty()) {
            for (const auto &it : n->else_body)
                dispatch_block(it);
        }
    }
    void visit(const DoWhileStmt *n) override {
        for (const auto &it : n->body)
            dispatch_block(it);
        if (n->condition)
            n->condition->accept(this);
    }
    void visit(const TryCatchStmt *n) override {
        for (const auto &it : n->try_body)
            dispatch_block(it);
        if (n->catch_block)
            visit(n->catch_block.get());
    }
    void visit(const CatchBlock *n) override {
        for (const auto &it : n->body)
            dispatch_block(it);
    }
    void visit(const MatchingStmt *n) override {
        if (n->expression)
            n->expression->accept(this);
        for (const auto &c : n->cases)
            c->accept(this);
        for (const auto &it : n->else_body)
            dispatch_block(it);
    }
    void visit(const MatchingCase *n) override {
        if (n->pattern)
            n->pattern->accept(this);
        for (const auto &it : n->body)
            dispatch_block(it);
    }
    void visit(const AssignmentStmt *n) override {
        if (n->target)
            n->target->accept(this);
        if (n->value)
            n->value->accept(this);
    }
    void visit(const ReturnStmt *n) override {
        if (n->value)
            n->value->accept(this);
    }
    void visit(const ExprStmt *n) override {
        if (n->expr)
            n->expr->accept(this);
    }
    void visit(const ThrowStmt *n) override {
        if (n->value)
            n->value->accept(this);
    }
    void visit(const BreakStmt *) override {}
    void visit(const ContinueStmt *) override {}
    void visit(const WhileElseBlock *n) override {
        for (const auto &it : n->body)
            dispatch_block(it);
    }
    void visit(const BinaryOp *n) override {
        if (n->left)
            n->left->accept(this);
        if (n->right)
            n->right->accept(this);
    }
    void visit(const CallExpr *n) override {
        for (const auto &a : n->args)
            if (a)
                a->accept(this);
    }
    void visit(const MemberAccess *n) override {
        if (n->object)
            n->object->accept(this);
    }
    void visit(const ArrayAccess *n) override {
        if (n->array)
            n->array->accept(this);
        if (n->index)
            n->index->accept(this);
    }
    void visit(const ArrayInit *n) override {
        used_types.insert(n->element_type);
        for (const auto &e : n->elements)
            if (e)
                e->accept(this);
    }
    void visit(const CastExpr *n) override {
        used_types.insert(n->target_type.kind);
        if (n->expr)
            n->expr->accept(this);
    }
    void visit(const RefMakeExpr *n) override {
        if (n->arg)
            n->arg->accept(this);
    }
    void visit(const RefTakeExpr *n) override {
        if (n->arg)
            n->arg->accept(this);
    }
    void visit(const EnumDecl *) override {}
    void visit(const EnumMember *) override {}
    void visit(const StructDecl *n) override {
        for (const auto &f : n->fields)
            used_types.insert(f->type.kind);
    }
    void visit(const StructField *) override {}
    void visit(const VarRef *) override {}
    void visit(const IntLiteral *) override {}
    void visit(const StringLiteral *) override {}
    void visit(const EnumLiteral *) override {}
    void visit(const EmbedExpr *) override {}
    void visit(const ParamDecl *) override {}
};

static std::vector<TypeKind> collect_used_types(const Program *program) {
    TypeUsageCollector collector;
    const_cast<Program *>(program)->accept(&collector);
    return std::vector<TypeKind>(collector.used_types.begin(), collector.used_types.end());
}

// --- Feature-based conditional header generation via NodeCollector visitor ---

struct FeatureConfig {
    ParserToken::Kind trigger;
    const char *text;
};

constexpr const char *HEADER_TORCH = "#include <torch/torch.h>\n";

static const FeatureConfig FEATURES[] = {
    {ParserToken::Kind::ArrayInit, HEADER_TORCH},
    {ParserToken::Kind::ArrayAccess, HEADER_TORCH},
    {ParserToken::Kind::RefMakeExpr, "template<typename T> T ref_make(T&& v) { return std::forward<T>(v); }\n"},
    {ParserToken::Kind::RefTakeExpr, "template<typename T> T ref_take(T v) { return v; }\n"},
    {ParserToken::Kind::MatchingStmt, "template<typename T, typename U>\nbool test_matching(const T& val, const U& pattern);\n\n"},
};

// NodeCollector — unified visitor that collects all token_kind occurrences in AST
struct NodeCollector : AstVisitor {
    std::unordered_set<ParserToken::Kind> types;

    void dispatch_block(const BlockItem &item) {
        if (!item)
            return;
        item->accept(this);
    }

    void visit(const Program *n) override {
        for (const auto &i : n->items)
            i->accept(this);
    }
    void visit(const FuncDecl *n) override {
        if (n->body)
            n->body->accept(this);
    }
    void visit(const BlockStmt *n) override {
        for (const auto &it : n->body)
            dispatch_block(it);
    }
    void visit(const IfStmt *n) override {
        if (n->condition)
            n->condition->accept(this);
        for (const auto &it : n->then_body)
            dispatch_block(it);
        if (n->else_if)
            n->else_if->accept(this);
        if (n->else_block)
            n->else_block->accept(this);
    }
    void visit(const WhileStmt *n) override {
        if (n->condition)
            n->condition->accept(this);
        for (const auto &it : n->body)
            dispatch_block(it);
        if (!n->else_body.empty()) {
            for (const auto &it : n->else_body)
                dispatch_block(it);
        }
    }
    void visit(const DoWhileStmt *n) override {
        for (const auto &it : n->body)
            dispatch_block(it);
        if (n->condition)
            n->condition->accept(this);
    }
    void visit(const TryCatchStmt *n) override {
        for (const auto &it : n->try_body)
            dispatch_block(it);
        if (n->catch_block)
            visit(n->catch_block.get());
    }
    void visit(const CatchBlock *n) override {
        for (const auto &it : n->body)
            dispatch_block(it);
    }
    void visit(const MatchingStmt *n) override {
        types.insert(n->token_kind());
        if (n->expression)
            n->expression->accept(this);
        for (const auto &c : n->cases) {
            c->accept(this);
        }
        for (const auto &it : n->else_body)
            dispatch_block(it);
    }
    void visit(const MatchingCase *n) override {
        types.insert(n->token_kind());
        if (n->pattern)
            n->pattern->accept(this);
        for (const auto &it : n->body)
            dispatch_block(it);
    }
    void visit(const VarDecl *n) override {
        types.insert(n->token_kind());
        if (n->init)
            n->init->accept(this);
    }
    void visit(const AssignmentStmt *n) override {
        types.insert(n->token_kind());
        if (n->target)
            n->target->accept(this);
        if (n->value)
            n->value->accept(this);
    }
    void visit(const ReturnStmt *n) override {
        types.insert(n->token_kind());
        if (n->value)
            n->value->accept(this);
    }
    void visit(const ExprStmt *n) override {
        types.insert(n->token_kind());
        if (n->expr)
            n->expr->accept(this);
    }
    void visit(const ThrowStmt *n) override {
        types.insert(n->token_kind());
        if (n->value)
            n->value->accept(this);
    }
    void visit(const BreakStmt *n) override {
        types.insert(n->token_kind());
        (void)n;
    }
    void visit(const ContinueStmt *n) override {
        types.insert(n->token_kind());
        (void)n;
    }
    void visit(const WhileElseBlock *n) override {
        for (const auto &it : n->body)
            dispatch_block(it);
    }
    void visit(const BinaryOp *n) override {
        types.insert(n->token_kind());
        if (n->left)
            n->left->accept(this);
        if (n->right)
            n->right->accept(this);
    }
    void visit(const CallExpr *n) override {
        types.insert(n->token_kind());
        for (const auto &a : n->args)
            if (a)
                a->accept(this);
    }
    void visit(const MemberAccess *n) override {
        types.insert(n->token_kind());
        if (n->object)
            n->object->accept(this);
    }
    void visit(const ArrayAccess *n) override {
        types.insert(n->token_kind());
        if (n->array)
            n->array->accept(this);
        if (n->index)
            n->index->accept(this);
    }
    void visit(const ArrayInit *n) override {
        types.insert(n->token_kind());
        for (const auto &e : n->elements)
            if (e)
                e->accept(this);
    }
    void visit(const CastExpr *n) override {
        types.insert(n->token_kind());
        if (n->expr)
            n->expr->accept(this);
    }
    void visit(const RefMakeExpr *n) override {
        types.insert(n->token_kind());
        if (n->arg)
            n->arg->accept(this);
    }
    void visit(const RefTakeExpr *n) override {
        types.insert(n->token_kind());
        if (n->arg)
            n->arg->accept(this);
    }
    void visit(const EnumDecl *n) override { types.insert(n->token_kind()); }
    void visit(const EnumMember *n) override { types.insert(n->token_kind()); }
    void visit(const StructDecl *n) override { types.insert(n->token_kind()); }
    void visit(const StructField *n) override { types.insert(n->token_kind()); }
    void visit(const VarRef *n) override { types.insert(n->token_kind()); }
    void visit(const IntLiteral *n) override { types.insert(n->token_kind()); }
    void visit(const StringLiteral *n) override { types.insert(n->token_kind()); }
    void visit(const EnumLiteral *n) override { types.insert(n->token_kind()); }
    void visit(const EmbedExpr *n) override { types.insert(n->token_kind()); }
    void visit(const ParamDecl *) override {}
};

static std::unordered_set<ParserToken::Kind> collect_node_types(const Program *program) {
    NodeCollector collector;
    const_cast<Program *>(program)->accept(&collector);
    return std::move(collector.types);
}

static std::string torch_dtype_str(TypeKind t) {
    switch (t) {
    case TypeKind::Int:
        return "torch::kInt32";
    case TypeKind::Bool:
        return "torch::kBool";
    default:
        return "torch::kInt32";
    }
}

CppGenerator::CppGenerator(GeneratorOptions opts) : indent_level_(0), opts_(std::move(opts)) {
}

TypeKind CppGenerator::get_expr_type(const Expr *e) const {
    if (e && type_res_) {
        auto ti = type_res_->get_type(e);
        if (ti.has_value()) {
            return ti->kind;
        }
    }
    // Fallback: for literal nodes we know the type statically
    if (e) {
        if (e->is<IntLiteral>())
            return TypeKind::Int;
        if (e->is<StringLiteral>())
            return TypeKind::String;
        if (const auto *cast = e->as<CastExpr>())
            return cast->target_type.kind;
    }
    return TypeKind::Int;
}

std::string CppGenerator::generate(const Program *program, std::string *binding_header, const char *binding_guard) {
    out_.str("");
    out_.clear();
    indent_level_ = 0;

    // Generate binding header if requested
    if (binding_header && binding_guard) {
        std::ostringstream hdr;
        hdr << "/* Auto-generated binding header */\n";
        hdr << "#ifndef " << binding_guard << "\n";
        hdr << "#define " << binding_guard << "\n";
        hdr << "\n";

        for (const auto &item : program->items) {
            if (auto *func = dynamic_cast<const FuncDecl *>(item.get())) {
                hdr << type_to_cpp(func->return_type) << " " << func->name << "(";
                bool first = true;
                for (const auto &param : func->params) {
                    if (!first)
                        hdr << ", ";
                    hdr << type_to_cpp(param->param_type) << " " << param->name;
                    first = false;
                }
                hdr << ");\n";
            }
        }

        hdr << "\n";
        hdr << "#endif /* " << binding_guard << " */\n";
        *binding_header = hdr.str();
    }

    // For Traditional format: includes first
    if (opts_.format == OutputFormat::Traditional) {
        out_ << "#include <iostream>\n";
        out_ << "#include <string>\n";
        out_ << "#include <type_traits>\n";
        out_ << "\n";
        out_ << "// Enable streaming for enum class types\n";
        out_ << "template<typename T, typename = std::enable_if_t<std::is_enum_v<T>>>\n";
        out_ << "std::ostream& operator<<(std::ostream& os, T e) {\n";
        out_ << "    return os << static_cast<std::underlying_type_t<T>>(e);\n";
        out_ << "}\n\n";

        // Collect headers from type requirements
        auto used_types = collect_used_types(program);
        auto &registry = TypeRequirementsRegistry::instance();
        auto type_headers = registry.collect_headers(used_types);
        for (const auto &hdr : type_headers) {
            out_ << "#include " << hdr << "\n";
        }

        // Conditional headers based on collected node types (AST-level triggers)
        auto node_types = collect_node_types(program);
        std::unordered_set<const char *> inserted;
        for (const auto &f : FEATURES) {
            if (node_types.count(f.trigger)) {
                if (inserted.insert(f.text).second) {
                    out_ << f.text;
                }
            }
        }

        out_ << "\n";
        out_ << "template<typename T> void print(T&& t) { std::cout << std::forward<T>(t) << std::endl; }\n\n";
    }

    // Type definitions (enum/struct) --- emitted in visit(Program) before other items
    // (visit(Program*) skips StructDecl/EnumDecl since they are emitted here)
    if (program) {
        for (const auto &item : program->items) {
            ParserToken::Kind tk = item->token_kind();
            if (tk == ParserToken::Kind::StructDecl || tk == ParserToken::Kind::EnumDecl) {
                item->accept(this);
            }
        }
    }

    // For module formats: write module preamble after type definitions
    if (opts_.format == OutputFormat::Cpp20Module || opts_.format == OutputFormat::Cpp23Module) {
        write_module_preamble();
    }

    // Forward declarations of functions
    if (program) {
        for (const auto &item : program->items) {
            if (item->token_kind() == ParserToken::Kind::FuncDecl) {
                auto *fd = static_cast<const FuncDecl *>(item.get());
                if (opts_.format == OutputFormat::Cpp20Module || opts_.format == OutputFormat::Cpp23Module) {
                    out_ << "export ";
                }
                out_ << type_to_cpp(fd->return_type) << " " << fd->name << "(";
                for (size_t i = 0; i < fd->params.size(); ++i) {
                    out_ << type_to_cpp(fd->params[i]->param_type) << " " << fd->params[i]->name;
                    if (i + 1 < fd->params.size())
                        out_ << ", ";
                }
                out_ << ");\n";
            }
        }
    }
    out_ << "\n";

    // Main content: accept all items (skip types already emitted above)
    if (program) {
        for (const auto &item : program->items) {
            ParserToken::Kind tk = item->token_kind();
            if (tk != ParserToken::Kind::StructDecl && tk != ParserToken::Kind::EnumDecl) {
                item->accept(this);
            }
        }
    }
    return out_.str();
}

void CppGenerator::write_module_preamble() {
    if (opts_.format == OutputFormat::Cpp23Module) {
        // C++23: use import for standard library headers (P2069R7)
        // Module declaration first
        out_ << "module;\n";
        out_ << "#include <iostream>\n";
        out_ << "#include <string>\n";
        out_ << "#include <forward>\n";
        out_ << "#include <format>\n";
        // Also demonstrate C++23 import capability for named modules
        out_ << "\n";
        out_ << "export module " << opts_.module_name << ";\n";
        out_ << "\n";
        // C++23: can use `import std;` instead of individual headers
        // (when std module is available)
        out_ << "// C++23: import std;  // replaces all standard headers above\n\n";
        // Extra imports
        for (const auto &imp : opts_.extra_imports) {
            out_ << "import " << imp << ";\n";
        }
        if (!opts_.extra_imports.empty()) {
            out_ << "\n";
        }
        out_ << "export template<typename T> void print(T&& t) { std::cout << std::forward<T>(t) << std::endl; }\n\n";
    } else {
        // C++20: Global module fragment for includes
        out_ << "module;\n";
        out_ << "#include <iostream>\n";
        out_ << "#include <string>\n";
        out_ << "#include <forward>\n";
        out_ << "#include <format>\n";
        out_ << "\n";
        // Module declaration
        out_ << "export module " << opts_.module_name << ";\n";
        out_ << "\n";
        // Include extra imports
        for (const auto &imp : opts_.extra_imports) {
            out_ << "import " << imp << ";\n";
        }
        if (!opts_.extra_imports.empty()) {
            out_ << "\n";
        }
        // Print template function --- exported
        out_ << "export template<typename T> void print(T&& t) { std::cout << std::forward<T>(t) << std::endl; }\n\n";
    }
}

void CppGenerator::dispatch_block_item(const BlockItem &item) {
    if (!item)
        return;
    item->accept(this);
}

void CppGenerator::visit(const Program *node) {
    // Types are already emitted in generate(), skip them here
    for (const auto &item : node->items) {
        ParserToken::Kind tk = item->token_kind();
        if (tk != ParserToken::Kind::StructDecl && tk != ParserToken::Kind::EnumDecl) {
            item->accept(this);
        }
    }
}

void CppGenerator::visit(const FuncDecl *node) {
    if (opts_.format == OutputFormat::Cpp20Module || opts_.format == OutputFormat::Cpp23Module) {
        out_ << "export ";
    }
    out_ << type_to_cpp(node->return_type) << " " << node->name << "(";
    for (size_t i = 0; i < node->params.size(); ++i) {
        visit(node->params[i].get());
        if (i + 1 < node->params.size())
            out_ << ", ";
    }
    out_ << ") {\n";
    indent_level_++;
    if (node->body) {
        for (const auto &bitem : node->body->body)
            dispatch_block_item(bitem);
    }
    indent_level_--;
    out_ << "}\n\n";
}

void CppGenerator::visit(const WhileStmt *node) {
    std::string loop_id = unique_label();
    std::string cond_label = loop_id + "_cond";
    std::string body_label = loop_id + "_body";
    std::string else_label = loop_id + "_else";
    std::string end_label = loop_id + "_end";

    if (!node->else_body.empty()) {
        // While with else: check condition first, goto body if true, goto else if false
        // Continue should go back to condition check
        current_loop_labels_.push_back({body_label, cond_label, end_label});

        indent();
        out_ << cond_label << ":\n";
        indent();
        out_ << "if (!(";
        if (node->condition)
            node->condition->accept(this);
        out_ << ")) goto " << else_label << ";\n";
        indent();
        out_ << "goto " << body_label << ";\n";
        indent();
        out_ << else_label << ":;\n";
        indent_level_++;
        for (const auto &bitem : node->else_body)
            dispatch_block_item(bitem);
        indent_level_--;
        indent();
        out_ << "goto " << end_label << ";\n";
        indent();
        out_ << body_label << ":\n";
        indent_level_++;
        indent();
        out_ << "do {\n";
        for (const auto &bitem : node->body)
            dispatch_block_item(bitem);
        indent_level_--;
        indent();
        out_ << "} while (";
        if (node->condition)
            node->condition->accept(this);
        out_ << ");\n";
        indent();
        out_ << end_label << ":;\n";
    } else {
        // Simple while without else
        current_loop_labels_.push_back({body_label, cond_label, end_label});

        indent();
        out_ << cond_label << ":\n";
        indent();
        out_ << "if (!(";
        if (node->condition)
            node->condition->accept(this);
        out_ << ")) goto " << end_label << ";\n";
        indent();
        out_ << body_label << ":\n";
        indent_level_++;
        for (const auto &bitem : node->body)
            dispatch_block_item(bitem);
        indent_level_--;
        indent();
        out_ << "goto " << cond_label << ";\n";
        indent();
        out_ << end_label << ":;\n";
    }

    current_loop_labels_.pop_back();
}

void CppGenerator::visit(const DoWhileStmt *node) {
    std::string loop_id = unique_label();
    std::string body_label = loop_id + "_body";
    std::string cont_label = loop_id + "_cont";
    std::string end_label = loop_id + "_end";

    current_loop_labels_.push_back({body_label, cont_label, end_label});

    indent();
    out_ << body_label << ":\n";
    indent_level_++;
    indent();
    out_ << "do {\n";
    for (const auto &bitem : node->body)
        dispatch_block_item(bitem);
    indent();
    out_ << cont_label << ":;\n";
    indent_level_--;
    indent();
    out_ << "} while (";
    if (node->condition)
        node->condition->accept(this);
    out_ << ");\n";
    indent();
    out_ << end_label << ":;\n";

    current_loop_labels_.pop_back();
}

void CppGenerator::visit(const TryCatchStmt *node) {
    indent();
    out_ << "try {\n";
    indent_level_++;
    for (const auto &bitem : node->try_body)
        dispatch_block_item(bitem);
    indent_level_--;
    indent();
    out_ << "} ";
    if (node->catch_block) {
        this->visit(node->catch_block.get());
    }
}

void CppGenerator::visit(const CatchBlock *node) {
    out_ << "catch (" << type_to_cpp(node->exception_type) << " " << node->var_name << ") {\n";
    indent_level_++;
    for (const auto &bitem : node->body)
        dispatch_block_item(bitem);
    indent_level_--;
    indent();
    out_ << "}\n";
}

void CppGenerator::visit(const ThrowStmt *node) {
    indent();
    out_ << "throw ";
    if (node->value)
        node->value->accept(this);
    out_ << ";\n";
}

void CppGenerator::visit(const MatchingStmt *node) {
    indent();
    build_matching_chain(node->expression.get(), node->cases, node->else_body, 0, true);
}

void CppGenerator::build_matching_chain(Expr *expr, const std::vector<std::unique_ptr<MatchingCase>> &cases, const BlockBody &else_body, size_t idx,
                                        bool is_first) {
    if (idx >= cases.size()) {
        if (!else_body.empty()) {
            if (!is_first) {
                out_ << " else {\n";
            } else {
                out_ << "}\n";
                indent();
                out_ << "{\n";
            }
            indent_level_++;
            for (const auto &bitem : else_body)
                dispatch_block_item(bitem);
            indent_level_--;
            indent();
            out_ << "}\n";
        } else if (!is_first) {
            out_ << "}\n";
        }
        return;
    }

    auto &current_case = cases[idx];
    if (is_first)
        indent();
    else
        out_ << " else ";
    out_ << "if (test_matching(";
    expr->accept(this);
    out_ << ", ";
    current_case->pattern->accept(this);
    out_ << ")) {\n";
    indent_level_++;
    for (const auto &bitem : current_case->body)
        dispatch_block_item(bitem);
    indent_level_--;
    indent();
    out_ << "}";

    if (idx + 1 < cases.size() || !else_body.empty()) {
        // More cases or else body --> continue chain
    } else {
        out_ << "\n";
    }
    build_matching_chain(expr, cases, else_body, idx + 1, false);
}

void CppGenerator::visit(const MatchingCase *node) {
    (void)node;
}

void CppGenerator::visit(const ParamDecl *node) {
    out_ << type_to_cpp(node->param_type) << " " << node->name;
}

void CppGenerator::visit(const BlockStmt *node) {
    for (const auto &bitem : node->body)
        dispatch_block_item(bitem);
}

void CppGenerator::visit(const VarDecl *node) {
    indent();
    bool is_array = node->init && node->init->token_kind() == ParserToken::Kind::ArrayInit;
    if (is_array) {
        out_ << "torch::Tensor " << node->name << " = torch::tensor(";
        node->init->accept(this);
        out_ << ", " << torch_dtype_str(node->type_info().kind) << ");\n";
    } else if (node->init) {
        out_ << type_to_cpp(node->type_info()) << " " << node->name << " = ";
        node->init->accept(this);
        out_ << ";\n";
    } else {
        out_ << type_to_cpp(node->type_info()) << " " << node->name << ";\n";
    }
}

void CppGenerator::visit(const AssignmentStmt *node) {
    indent();
    if (node->target)
        node->target->accept(this);
    out_ << " = ";
    node->value->accept(this);
    out_ << ";\n";
}

void CppGenerator::visit(const ReturnStmt *node) {
    indent();
    out_ << "return";
    if (node->value) {
        out_ << " ";
        node->value->accept(this);
    }
    out_ << ";\n";
}

void CppGenerator::visit(const ExprStmt *node) {
    indent();
    node->expr->accept(this);
    out_ << ";\n";
}

void CppGenerator::visit(const IfStmt *node) {
    visit_if_stmt(node, true);
}

void CppGenerator::visit_if_stmt(const IfStmt *node, bool is_primary) {
    if (is_primary)
        indent();
    out_ << "if (";
    if (node->condition)
        node->condition->accept(this);
    out_ << ") {\n";
    indent_level_++;
    for (const auto &bitem : node->then_body)
        dispatch_block_item(bitem);
    indent_level_--;
    if (node->else_if) {
        indent();
        out_ << "} else ";
        visit_if_stmt(node->else_if.get(), false);
    } else if (node->else_block) {
        indent();
        out_ << "} else {\n";
        indent_level_++;
        for (const auto &bitem : node->else_block->body)
            dispatch_block_item(bitem);
        indent_level_--;
        indent();
        out_ << "}\n";
    } else {
        indent();
        out_ << "}\n";
    }
}

void CppGenerator::visit(const CallExpr *node) {
    out_ << node->name << "(";
    for (size_t i = 0; i < node->args.size(); ++i) {
        node->args[i]->accept(this);
        if (i + 1 < node->args.size())
            out_ << ", ";
    }
    out_ << ")";
}

void CppGenerator::visit(const BinaryOp *node) {
    out_ << "(";
    node->left->accept(this);
    out_ << " " << bin_op_to_str(node->op) << " ";
    node->right->accept(this);
    out_ << ")";
}

void CppGenerator::visit(const VarRef *node) {
    out_ << node->name;
}
void CppGenerator::visit(const IntLiteral *node) {
    out_ << node->value;
}
void CppGenerator::visit(const StringLiteral *node) {
    out_ << "\"" << MMProcessor::escape(node->value()) << "\"";
}

void CppGenerator::visit(const EnumMember *node) {
    (void)node;
}

void CppGenerator::visit(const EnumDecl *node) {
    if (opts_.format == OutputFormat::Cpp20Module || opts_.format == OutputFormat::Cpp23Module) {
        out_ << "export ";
    }
    out_ << "enum class " << node->name << " { ";
    for (size_t i = 0; i < node->members.size(); ++i) {
        out_ << node->members[i]->name;
        if (node->members[i]->value) {
            out_ << " = " << *node->members[i]->value;
        }
        if (i + 1 < node->members.size())
            out_ << ", ";
    }
    out_ << " };\n\n";
}

void CppGenerator::visit(const StructField *node) {
    (void)node;
}

void CppGenerator::visit(const StructDecl *node) {
    if (opts_.format == OutputFormat::Cpp20Module || opts_.format == OutputFormat::Cpp23Module) {
        out_ << "export ";
    }
    out_ << "struct " << node->name << " {\n";
    indent_level_++;
    for (const auto &field : node->fields) {
        indent();
        out_ << type_to_cpp(field->type) << " " << field->name;
        if (field->init) {
            out_ << " = ";
            field->init->accept(this);
        }
        out_ << ";\n";
    }
    out_ << "\n";
    for (const auto &method : node->methods) {
        if (auto *fd = static_cast<const FuncDecl *>(method.get())) {
            out_ << type_to_cpp(fd->return_type) << " " << fd->name << "(";
            for (size_t i = 0; i < fd->params.size(); ++i) {
                visit(fd->params[i].get());
                if (i + 1 < fd->params.size())
                    out_ << ", ";
            }
            out_ << ") {\n";
            indent_level_++;
            if (fd->body) {
                for (const auto &bitem : fd->body->body)
                    dispatch_block_item(bitem);
            }
            indent_level_--;
            indent();
            out_ << "}\n\n";
        }
    }
    indent_level_--;
    out_ << "};\n\n";
}

void CppGenerator::visit(const EnumLiteral *node) {
    out_ << node->enum_name << "::" << node->member_name;
}

void CppGenerator::visit(const MemberAccess *node) {
    node->object->accept(this);
    out_ << "." << node->field;
}

void CppGenerator::visit(const ArrayAccess *node) {
    node->array->accept(this);
    out_ << "[";
    node->index->accept(this);
    out_ << "]";
}

void CppGenerator::visit(const ArrayInit *node) {
    out_ << "{";
    for (size_t i = 0; i < node->elements.size(); ++i) {
        node->elements[i]->accept(this);
        if (i + 1 < node->elements.size())
            out_ << ", ";
    }
    out_ << "}";
}

void CppGenerator::visit(const CastExpr *node) {
    out_ << "static_cast<" << type_to_cpp(node->target_type) << ">(";
    node->expr->accept(this);
    out_ << ")";
}

void CppGenerator::visit(const RefMakeExpr *node) {
    out_ << "ref_make(";
    node->arg->accept(this);
    out_ << ")";
}

void CppGenerator::visit(const RefTakeExpr *node) {
    out_ << "ref_take(";
    node->arg->accept(this);
    out_ << ")";
}

void CppGenerator::visit(const EmbedExpr *node) {
    out_ << node->value();
}

void CppGenerator::visit(const BreakStmt *node) {
    indent();
    if (!current_loop_labels_.empty()) {
        out_ << "goto " << current_loop_labels_.back().end_label << ";\n";
    } else {
        out_ << "break;\n";
    }
}

void CppGenerator::visit(const ContinueStmt *node) {
    indent();
    if (!current_loop_labels_.empty()) {
        out_ << "goto " << current_loop_labels_.back().cont_label << ";\n";
    } else {
        out_ << "continue;\n";
    }
}

void CppGenerator::visit(const WhileElseBlock *node) {
    for (const auto &bitem : node->body)
        dispatch_block_item(bitem);
}

std::string CppGenerator::unique_label() {
    return "__L" + std::to_string(label_counter_++);
}

void CppGenerator::indent() {
    for (int i = 0; i < indent_level_; ++i)
        out_ << "    ";
}

std::string CppGenerator::bin_op_to_str(BinOp op) const {
    return MMProcessor::bin_op_to_string(op);
}

std::string CppGenerator::type_to_cpp(TypeInfo t) const {
    return t.to_cpp();
}