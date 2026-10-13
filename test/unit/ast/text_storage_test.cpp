// test/unit/ast/text_storage_test.cpp
// Модель текста: текст хранится ТОЛЬКО у узлов-носителей (WithText/HasText) как интернированный
// view в пул строк StringInterner; исходный буфер в AST не удерживается. Синтаксические узлы
// и операторы (Sequence/Binary/TryCatch) текста не имеют.

#include "ast/ast_nodes.hpp"
#include "ast/clone.hpp"
#include "ast/term_to_ast.hpp"
#include "pipeline/pipeline.hpp"
#include "semantic/diag.hpp"
#include "session/context.hpp"
#include "syntax/parser.h"
#include "syntax/term/term.h"
#include "types/type_system.hpp"
#include "utils/string_interner.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace trust {
namespace {

/// true - view указывает внутрь буфера источника (проверка "не интернирован").
bool pointsIntoBuffer(std::string_view text, std::string_view buffer) {
    const auto textBegin = reinterpret_cast<std::uintptr_t>(text.data());
    const auto textEnd = textBegin + text.size();
    const auto bufferBegin = reinterpret_cast<std::uintptr_t>(buffer.data());
    const auto bufferEnd = bufferBegin + buffer.size();
    return textBegin >= bufferBegin && textEnd <= bufferEnd;
}

/// Один Own-текст обязан быть интернирован в пул и не алиасить исходный буфер.
void expectOwnText(std::string_view text, const StringInterner& pool, std::string_view source) {
    if (text.empty()) {
        return;
    }
    EXPECT_TRUE(pool.owns(text)) << "AST text must be interned: '" << text << "'";
    EXPECT_FALSE(pointsIntoBuffer(text, source)) << "AST text must not alias the source buffer";
}

void expectAstTextInterned(const AstNodeBase& node, const StringInterner& pool, std::string_view source);

/// Список узлов вне общего обхода children() (доки/шаблоны/захваты).
void expectAstTextList(const std::optional<std::vector<AstNodePtr>>& list, const StringInterner& pool, std::string_view source) {
    if (!list) {
        return;
    }
    for (const auto& child : *list) {
        if (child) {
            expectAstTextInterned(*child, pool, source);
        }
    }
}

/// Обход ВСЕГО дерева: Own-текст в любом владеемом слоте (включая не-children слоты)
/// обязан быть интернирован в пул и не алиасить исходный буфер.
void expectAstTextInterned(const AstNodeBase& node, const StringInterner& pool, std::string_view source) {
    if (const HasText* text = node.as_text()) {
        expectOwnText(text->text(), pool, source);
    }
    expectOwnText(node.documentation, pool, source);
    for (const auto& contract : node.m_trust) {
        if (contract) {
            expectAstTextInterned(*contract, pool, source);
        }
    }

    const auto kind = node.kind();
    if (kind == ParserToken::Kind::IntLiteral || kind == ParserToken::Kind::FloatLiteral || kind == ParserToken::Kind::StrChar ||
        kind == ParserToken::Kind::StrWide || kind == ParserToken::Kind::RationalLiteral) {
        if (const auto* literal = node.as<Literal>(); literal && literal->typeAnnotation) {
            expectAstTextInterned(*literal->typeAnnotation, pool, source);
        }
    }
    if (kind == ParserToken::Kind::FuncDecl) {
        const auto& func = static_cast<const FuncDecl&>(node);
        expectAstTextList(func.m_templateParams, pool, source);
        expectAstTextList(func.m_captures, pool, source);
    }
    if (kind == ParserToken::Kind::TypeName) {
        expectAstTextList(static_cast<const IdentType&>(node).templateArgs(), pool, source);
    }
    if (kind == ParserToken::Kind::DestructureDecl) {
        const auto& destructure = static_cast<const DestructureDecl&>(node);
        if (destructure.m_sourceTemp) {
            expectAstTextInterned(*destructure.m_sourceTemp, pool, source);
        }
    }
    if (kind == ParserToken::Kind::ClassDecl) {
        expectAstTextList(static_cast<const ClassDecl&>(node).m_templateParams, pool, source);
    }
    if (kind == ParserToken::Kind::StructDecl) {
        expectAstTextList(static_cast<const RecordDecl&>(node).m_templateParams, pool, source);
    }
    if (kind == ParserToken::Kind::TrustContract) {
        const auto& contract = static_cast<const TrustContract&>(node);
        if (contract.m_expr) {
            expectAstTextInterned(*contract.m_expr, pool, source);
        }
    }

    for (const auto& child : node.children()) {
        if (child) {
            expectAstTextInterned(*child, pool, source);
        }
    }
}

/// Первый узел указанного kind в поддереве (или nullptr).
const AstNodeBase* findKind(const AstNodeBase& node, ParserToken::Kind kind) {
    if (node.kind() == kind) {
        return &node;
    }
    for (const auto& child : node.children()) {
        if (child) {
            if (const AstNodeBase* found = findKind(*child, kind)) {
                return found;
            }
        }
    }
    return nullptr;
}

/// Клон обязан делить те же интернированные view (никаких копий текста).
void expectCloneSharesText(const AstNodeBase& original, const AstNodeBase& clone) {
    const HasText* originalText = original.as_text();
    const HasText* cloneText = clone.as_text();
    EXPECT_EQ(originalText != nullptr, cloneText != nullptr) << "text carrier must match between original and clone";
    if (originalText != nullptr && cloneText != nullptr) {
        EXPECT_EQ(cloneText->text().data(), originalText->text().data());
        EXPECT_EQ(cloneText->text(), originalText->text());
    }
    // Документация — отдельный интернированный слот: клон обязан делить тот же view.
    EXPECT_EQ(clone.documentation.data(), original.documentation.data()) << "clone must share interned documentation";
    EXPECT_EQ(clone.documentation, original.documentation);
    const auto originalChildren = original.children();
    const auto cloneChildren = clone.children();
    ASSERT_EQ(originalChildren.size(), cloneChildren.size());
    for (std::size_t i = 0; i < originalChildren.size(); ++i) {
        if (originalChildren[i] && cloneChildren[i]) {
            expectCloneSharesText(*originalChildren[i], *cloneChildren[i]);
        }
    }
}

// Текст узла — НЕ view в SourceMap, а интернированный текст пула Context.
TEST(TextStorageTest, TextIsInternedIntoCompilationPool) {
    Context ctx;
    Parser parser(ctx);
    const TermPtr term = parser.ParseText("x := 1;");
    const std::vector<AstNodePtr> roots = TermToAstConverter::termToAst(term, ctx);
    ASSERT_FALSE(roots.empty());
    const AstNodePtr& node = roots.front();
    ASSERT_NE(node, nullptr);

    const std::string_view text = node->as_text()->text();
    ASSERT_FALSE(text.empty());
    EXPECT_EQ(ctx.strings().intern(text).data(), text.data());
    EXPECT_TRUE(ctx.strings().owns(text));

    const std::string_view source = ctx.source().source(node->range().begin.fileIdx());
    ASSERT_FALSE(source.empty());
    EXPECT_FALSE(pointsIntoBuffer(text, source)) << "AST text must not alias the source buffer";
}

TEST(TextStorageTest, SemanticTextIsInternedAndDeduplicated) {
    Context ctx;
    Parser parser(ctx);
    const TermPtr term = parser.ParseText("x := 1;\ny := x;");
    (void)TermToAstConverter::termToAst(term, ctx);

    EXPECT_TRUE(ctx.strings().owns("x"));
    EXPECT_TRUE(ctx.strings().owns("y"));
    EXPECT_TRUE(ctx.strings().owns("1"));
    // Объявление не интернирует текст операторного терма (":=") - в пуле только настоящие spelling.
    EXPECT_FALSE(ctx.strings().owns(":="));
    EXPECT_FALSE(ctx.strings().owns(""));

    const std::size_t uniqueCount = ctx.strings().size();
    Parser parser2(ctx);
    const TermPtr term2 = parser2.ParseText("x := 1;");
    (void)TermToAstConverter::termToAst(term2, ctx);
    EXPECT_EQ(ctx.strings().size(), uniqueCount) << "repeated spellings must not grow the pool";
}

TEST(TextStorageTest, TypeNameNormalizationInternsFinalSpelling) {
    Context ctx;
    Parser parser(ctx);
    const TermPtr term = parser.ParseText("x : Int32 := 1;");
    (void)TermToAstConverter::termToAst(term, ctx);

    EXPECT_TRUE(ctx.strings().owns("Int32"));
    EXPECT_FALSE(ctx.strings().owns(":Int32")) << "normalized spelling must be interned once";
}

// Текст узла интернирован: он переживает разрушение дерева Term (исходный буфер/owned-строки
// Term не удерживаются AST).
TEST(TextStorageTest, SourceBackedNodeTextSurvivesTermRelease) {
    Context ctx;
    TermPtr term;
    {
        Parser parser(ctx);
        term = parser.ParseText("x := 1;");
    }
    const std::vector<AstNodePtr> roots = TermToAstConverter::termToAst(term, ctx);
    ASSERT_FALSE(roots.empty());
    ASSERT_NE(roots.front(), nullptr);
    const std::string_view text = roots.front()->as_text()->text();
    ASSERT_EQ(text, "x");
    term.reset();
    EXPECT_EQ(roots.front()->as_text()->text(), text);
    EXPECT_EQ(roots.front()->as_text()->text().data(), text.data());
}

TEST(TextStorageTest, CloneSharesTextViewWithoutCopy) {
    Context ctx;
    Parser parser(ctx);
    const TermPtr term = parser.ParseText("x := 1;");
    const std::vector<AstNodePtr> roots = TermToAstConverter::termToAst(term, ctx);
    const std::vector<AstNodePtr> clones = cloneTrees(roots);
    ASSERT_EQ(clones.size(), roots.size());
    ASSERT_NE(clones.front(), nullptr);
    EXPECT_EQ(clones.front()->as_text()->text().data(), roots.front()->as_text()->text().data());
    EXPECT_EQ(clones.front()->range(), roots.front()->range());
}

// Документация интернируется: owned-док Term не висит после уничтожения дерева Term.
TEST(TextStorageTest, OwnedDocumentationSurvivesTermRelease) {
    Context ctx;
    // CREATE_NAME := с owned-доком в m_docs (аналог дока, собранного macro.cpp:850).
    auto name = Term::Create(TermID::NAME, "x", {}, parser::token_type::NAME);
    auto op = Term::Create(TermID::CREATE_NAME, ":=");
    op->m_left = name;
    op->m_right = Term::Create(TermID::INTEGER, "1", {}, parser::token_type::INTEGER);
    auto doc = Term::Create(TermID::DOCUMENT, std::string("/// owned doc"));
    op->m_docs.push_back(doc);

    const std::vector<AstNodePtr> roots = TermToAstConverter::termToAst(op, ctx);
    ASSERT_FALSE(roots.empty());
    const AstNodePtr& decl = roots.front();
    ASSERT_NE(decl, nullptr);
    ASSERT_EQ(decl->kind(), ParserToken::Kind::VarDecl);
    ASSERT_FALSE(decl->documentation.empty());
    EXPECT_EQ(decl->documentation, "/// owned doc");

    const std::string_view docView = decl->documentation;
    op.reset(); // дерево Term уничтожено, owned-текст дока освобождён
    EXPECT_EQ(decl->documentation, "/// owned doc");
    EXPECT_EQ(decl->documentation, "/// owned doc");
}

// Пул строк принадлежит компиляции: освобождается вместе с последним владельцем.
TEST(StringInternerTest, CompilationPoolIsReleasedWithLastOwner) {
    std::weak_ptr<StringInterner> pool;
    {
        auto ctx = std::make_unique<Context>();
        Parser parser(*ctx);
        const TermPtr term = parser.ParseText("abc := 1;");
        const std::vector<AstNodePtr> roots = TermToAstConverter::termToAst(term, *ctx);
        ASSERT_FALSE(roots.empty());
        pool = ctx->stringsPool();
        EXPECT_FALSE(pool.expired());
    }
    EXPECT_TRUE(pool.expired()) << "StringInterner pool must die with the compilation context";
}

// PipelineResult разделяет владение пулом: тексты AST/SemanticModel валидны после Context.
TEST(StringInternerTest, PipelineResultKeepsPoolAlive) {
    std::weak_ptr<StringInterner> pool;
    std::string_view firstName;
    {
        auto ctx = std::make_unique<Context>();
        TypeSystem reg(ctx->diag(), ctx->opts());
        ctx->setTypes(&reg);

        PipelineOpts opts;
        std::optional<PipelineResult> result;
        {
            Pipeline pipeline(*ctx, opts);
            const MapperFile src = ctx->source().add_source("test.src", "x := 1;");
            result = pipeline.run(RunRequest::analysis(), src);
        }
        ASSERT_TRUE(result.has_value());
        ASSERT_TRUE(result->astNodes.has_value() && !result->astNodes->empty());
        ASSERT_TRUE(result->strings);
        ASSERT_TRUE(result->semantics.has_value());
        pool = result->strings;
        for (const auto& symbol : result->semantics->symbols()) {
            if (symbol.name == "x") {
                firstName = symbol.name;
                break;
            }
        }
        ASSERT_EQ(firstName, "x") << "symbol 'x' must be collected" << std::endl;
        ctx.reset(); // Context уничтожен, но результат держит пул
        EXPECT_FALSE(pool.expired());
        EXPECT_EQ(firstName, "x");
    }
    EXPECT_TRUE(pool.expired());
}

TEST(TextStorageTest, IdentifierRawSpellingIsNotInterned) {
    Context ctx;
    Parser parser(ctx);
    const TermPtr term = parser.ParseText("x^ := 1;");
    const std::vector<AstNodePtr> roots = TermToAstConverter::termToAst(term, ctx);
    ASSERT_FALSE(roots.empty());

    EXPECT_TRUE(ctx.strings().owns("x"));
    EXPECT_FALSE(ctx.strings().owns("x^")) << "raw identifier spelling must not enter the pool";
}

TEST(TextStorageTest, SourceBackedTermTextViewSurvivesSourceMapGrowth) {
    Context ctx;
    const MapperFile file = ctx.source().add_source("lifetime.src", "shared_name");
    const std::string_view source = ctx.source().source(file);
    const TermPtr term = Term::Create(TermID::NAME, parser::token_type::NAME, source.data(), source.size());

    for (int i = 0; i < 32; ++i) {
        ctx.source().add_source("extra_" + std::to_string(i) + ".src", std::string(128, 'x'));
    }

    EXPECT_EQ(term->getText(), "shared_name");
    EXPECT_EQ(term->getText().data(), source.data()) << "SourceMap storage must keep Term views stable";
}

TEST(TextStorageTest, MultipleDocumentationCommentsAreInterned) {
    Context ctx;
    auto name = Term::Create(TermID::NAME, "x", {}, parser::token_type::NAME);
    auto op = Term::Create(TermID::CREATE_NAME, ":=");
    op->m_left = name;
    op->m_right = Term::Create(TermID::INTEGER, "1", {}, parser::token_type::INTEGER);
    op->m_docs.push_back(Term::Create(TermID::DOCUMENT, std::string("/// a")));
    op->m_docs.push_back(Term::Create(TermID::DOCUMENT, std::string("/// b")));

    const std::vector<AstNodePtr> roots = TermToAstConverter::termToAst(op, ctx);
    ASSERT_FALSE(roots.empty());
    const AstNodePtr& decl = roots.front();
    ASSERT_NE(decl, nullptr);
    ASSERT_EQ(decl->kind(), ParserToken::Kind::VarDecl);
    EXPECT_EQ(decl->documentation, "/// a\n/// b");
    EXPECT_TRUE(ctx.strings().owns(decl->documentation));
}

TEST(TextStorageTest, AllAstTextIsInternedAndDoesNotAliasSource) {
    Context ctx;
    Parser parser(ctx);
    const TermPtr term = parser.ParseText("x := 1; y := x + 2; z := \"abc^\";");
    const std::vector<AstNodePtr> roots = TermToAstConverter::termToAst(term, ctx);
    ASSERT_FALSE(roots.empty());

    const MapperRange range = roots.front()->range();
    ASSERT_FALSE(range.isInvalid());
    const std::string_view source = ctx.source().source(range.begin.fileIdx());
    ASSERT_FALSE(source.empty());

    EXPECT_EQ(ctx.diag().errorCount(), 0) << "caret inside a literal must not be diagnosed as a qualifier";
    for (const auto& root : roots) {
        ASSERT_NE(root, nullptr);
        expectAstTextInterned(*root, ctx.strings(), source);
    }
}

// (S5) Полный обход Own-текста: children + не-children слоты (доки, типовые параметры,
// захваты лямбды, типовые аргументы нативного шаблона, контракты).
TEST(TextStorageTest, AllOwnTextSlotsAreInterned) {
    Context ctx;
    Parser parser(ctx);
    const TermPtr term = parser.ParseText("/// doc\n"
                                          "<T> identity(x: T): T := { y := x; };\n"
                                          "@main() := {\n"
                                          "    z := 1;\n"
                                          "    f := [z](m : Int32) : Int32 { w := m; };\n"
                                          "    a, b := ... z;\n"
                                          "    v : vector<:Int32> := [1, 2, 3,];\n"
                                          "};\n");
    const std::vector<AstNodePtr> roots = TermToAstConverter::termToAst(term, ctx);
    ASSERT_FALSE(roots.empty());

    const MapperRange range = roots.front()->range();
    ASSERT_FALSE(range.isInvalid());
    const std::string_view source = ctx.source().source(range.begin.fileIdx());
    ASSERT_FALSE(source.empty());

    EXPECT_EQ(ctx.diag().errorCount(), 0) << "test source must parse/convert without errors";

    bool hasDocumentation = false;
    bool hasCaptures = false;
    bool hasTemplateArgs = false;
    bool hasDestructure = false;
    const auto scan = [&](const auto& self, const AstNodeBase& node) -> void {
        hasDocumentation = hasDocumentation || !node.documentation.empty();
        hasDestructure = hasDestructure || node.kind() == ParserToken::Kind::DestructureDecl;
        if (node.kind() == ParserToken::Kind::FuncDecl) {
            const auto& func = static_cast<const FuncDecl&>(node);
            hasCaptures = hasCaptures || (func.m_captures && !func.m_captures->empty());
        }
        if (node.kind() == ParserToken::Kind::TypeName) {
            hasTemplateArgs = hasTemplateArgs || static_cast<const IdentType&>(node).templateArgs().has_value();
        }
        for (const auto& child : node.children()) {
            if (child) {
                self(self, *child);
            }
        }
    };
    for (const auto& root : roots) {
        ASSERT_NE(root, nullptr);
        scan(scan, *root);
    }
    EXPECT_TRUE(hasDocumentation) << "test must exercise attached documentation";
    EXPECT_TRUE(hasCaptures) << "test must exercise lambda capture slots";
    EXPECT_TRUE(hasTemplateArgs) << "test must exercise native-template type arguments";
    EXPECT_TRUE(hasDestructure) << "test must exercise DestructureDecl";

    for (const auto& root : roots) {
        ASSERT_NE(root, nullptr);
        expectAstTextInterned(*root, ctx.strings(), source);
    }
}

// (S5) Текст синтаксических (None) kind не попадает в пул строк.
TEST(TextStorageTest, SyntaxOnlyKindTextIsNotInterned) {
    Context ctx;
    Parser parser(ctx);
    // Сырая грамматика (без DSL-макроса `if`): `[cond] --> body` = IfStmt (синтаксический узел без текста),
    // текст терма "if" интернироваться не должен.
    const TermPtr term = parser.ParseText("@main() := { f(a); [true] --> { b := a; }; };");
    (void)TermToAstConverter::termToAst(term, ctx);

    EXPECT_EQ(ctx.diag().errorCount(), 0) << "test source must parse without errors";

    EXPECT_TRUE(ctx.strings().owns("a")) << "identifier spellings are Own-text";
    EXPECT_FALSE(ctx.strings().owns("(")) << "CallExpr text must not be interned";
    EXPECT_FALSE(ctx.strings().owns(")")) << "CallExpr text must not be interned";
    EXPECT_FALSE(ctx.strings().owns("if")) << "IfStmt text must not be interned";
}

// (S5) range узла с Term - provenance самого Term (range и текст из одного источника).
TEST(TextStorageTest, TermBackedNodeRangeIsTermProvenance) {
    Context ctx;
    Parser parser(ctx);
    const TermPtr parsed = parser.ParseText("x := 1;");
    ASSERT_NE(parsed, nullptr);
    const MapperRange range = parsed->m_mapperRange;
    ASSERT_FALSE(range.isInvalid());
    const std::string_view source = ctx.source().source(range.begin.fileIdx());
    ASSERT_FALSE(source.empty());

    const TermPtr rawTerm = Term::Create(TermID::DOCUMENT, parser::token_type::END, source.data(), source.size(), range);
    RawText raw(ParserToken::Kind::Document, rawTerm);
    EXPECT_EQ(raw.range(), range) << "range must be taken from the source Term";
    EXPECT_EQ(raw.text(), source);

    const TermPtr litTerm = Term::Create(TermID::STRCHAR, parser::token_type::END, source.data(), source.size(), range);
    Literal literal(ParserToken::Kind::StrChar, litTerm);
    EXPECT_EQ(literal.range(), range);
    EXPECT_EQ(literal.text(), source);
}

// (S5) Одинаковый spelling хранится один раз: узлы делят интернированную строку.
TEST(TextStorageTest, EqualSpellingSharesInternedStorage) {
    Context ctx;
    Parser parser(ctx);
    const TermPtr term = parser.ParseText("x := 1; y := x;");
    const std::vector<AstNodePtr> roots = TermToAstConverter::termToAst(term, ctx);
    ASSERT_FALSE(roots.empty());

    std::vector<const HasText*> sameName;
    const auto collect = [&](const auto& self, const AstNodeBase& node) -> void {
        if (const HasText* text = node.as_text(); text != nullptr && text->text() == "x") {
            sameName.push_back(text);
        }
        for (const auto& child : node.children()) {
            if (child) {
                self(self, *child);
            }
        }
    };
    for (const auto& root : roots) {
        ASSERT_NE(root, nullptr);
        collect(collect, *root);
    }
    ASSERT_GE(sameName.size(), 2u) << "declaration and use of 'x' must both carry the spelling";
    EXPECT_EQ(sameName[0]->text().data(), sameName[1]->text().data()) << "equal spellings must share one interned string";
}

// (S5) Док, привязанный к объявлению, не материализуется вторым (sibling) узлом.
TEST(TextStorageTest, DocumentationIsNotDuplicatedAsSiblingNode) {
    Context ctx;
    Parser parser(ctx);
    const TermPtr term = parser.ParseText("/// only doc\nx := 1;");
    const std::vector<AstNodePtr> roots = TermToAstConverter::termToAst(term, ctx);
    ASSERT_FALSE(roots.empty());
    for (const auto& root : roots) {
        ASSERT_NE(root, nullptr);
        EXPECT_EQ(findKind(*root, ParserToken::Kind::Document), nullptr) << "attached doc must not be materialized twice";
        EXPECT_NE(root->documentation.find("only doc"), std::string_view::npos);
        EXPECT_TRUE(ctx.strings().owns(root->documentation));
    }
}

// (S5) Trust-контракт: m_expr вне children() - обходчик обязан проверить его текст.
TEST(TextStorageTest, TrustContractExpressionIsWalkedForText) {
    StringInterner pool;
    StringInternerScope scope(pool);
    auto expr = std::make_shared<IdentName>("contract_target");
    TrustContract contract(ParserToken::Kind::TrustContract, expr);

    ASSERT_TRUE(pool.owns("contract_target"));
    expectAstTextInterned(contract, pool, std::string_view{});
}

// (S5) Списки типовых параметров FuncDecl/ClassDecl/RecordDecl вне children() обходятся.
TEST(TextStorageTest, TemplateParameterListsAreWalkedForText) {
    StringInterner pool;
    StringInternerScope scope(pool);

    FuncDecl func("func_template");
    func.m_templateParams = std::vector<AstNodePtr>{std::make_shared<ArgNode>("T_func")};
    ClassDecl cls("class_template");
    cls.m_templateParams = std::vector<AstNodePtr>{std::make_shared<ArgNode>("T_class")};
    RecordDecl record("record_template");
    record.m_templateParams = std::vector<AstNodePtr>{std::make_shared<ArgNode>("T_record")};

    expectAstTextInterned(func, pool, std::string_view{});
    expectAstTextInterned(cls, pool, std::string_view{});
    expectAstTextInterned(record, pool, std::string_view{});
    EXPECT_TRUE(pool.owns("T_func"));
    EXPECT_TRUE(pool.owns("T_class"));
    EXPECT_TRUE(pool.owns("T_record"));
}

TEST(TextStorageTest, CaretInOperatorsAndLiteralsIsNotDiagnosed) {
    Context ctx;
    Parser parser(ctx);
    const TermPtr term = parser.ParseText("x := 1 ^^ 0; y := \"abc^\";");
    const std::vector<AstNodePtr> roots = TermToAstConverter::termToAst(term, ctx);
    ASSERT_FALSE(roots.empty());
    EXPECT_EQ(ctx.diag().errorCount(), 0);

    const AstNodeBase* logicalXor = nullptr;
    const AstNodeBase* literal = nullptr;
    for (const auto& root : roots) {
        ASSERT_NE(root, nullptr);
        if (!logicalXor) {
            logicalXor = findKind(*root, ParserToken::Kind::LogicalOp);
        }
        if (!literal) {
            literal = findKind(*root, ParserToken::Kind::StrChar);
        }
    }
    ASSERT_NE(logicalXor, nullptr);
    // Оператор не хранит текст: глиф `^^` (с '^' как частью оператора) - в m_op, не квалификатор.
    const auto* xorBinary = logicalXor->as<Binary>();
    ASSERT_NE(xorBinary, nullptr);
    EXPECT_EQ(xorBinary->m_op, BinaryOp::LogicalXor);
    EXPECT_EQ(xorBinary->as_text(), nullptr);
    ASSERT_NE(literal, nullptr);
    EXPECT_EQ(literal->as_text()->text(), "abc^");
}

TEST(TextStorageTest, CloneSharesAllInternedTextView) {
    Context ctx;
    Parser parser(ctx);
    const TermPtr term = parser.ParseText("x := 1 + 2;");
    const std::vector<AstNodePtr> roots = TermToAstConverter::termToAst(term, ctx);
    const std::vector<AstNodePtr> clones = cloneTrees(roots);
    ASSERT_EQ(roots.size(), clones.size());

    for (std::size_t i = 0; i < roots.size(); ++i) {
        ASSERT_NE(roots[i], nullptr);
        ASSERT_NE(clones[i], nullptr);
        expectCloneSharesText(*roots[i], *clones[i]);
    }
}

// (S1) text() и range() - НЕЗАВИСИМЫЕ факты: при раскрытии макроса spelling берётся из определения,
// а range переприсваивается на call-site (см. macro.cpp:assign_call_site_ranges). AST переносит оба
// факта как есть, поэтому текст нельзя выводить из диапазона (и наоборот).
TEST(TextStorageTest, TextAndRangeAreIndependentFacts) {
    Context ctx;
    const MapperFile defFile = ctx.source().add_source("def.src", "shared_name");
    const MapperFile callFile = ctx.source().add_source("call.src", "other_call");
    const std::string_view definitionText = ctx.source().source(defFile);
    ASSERT_EQ(definitionText, "shared_name");

    // Терм макро-раскрытия: spelling из определения, range - сайт вызова.
    const TermPtr term = Term::Create(TermID::NAME, parser::token_type::NAME, definitionText.data(), definitionText.size());
    term->m_mapperRange = MapperRange{MapperLocation::makeLoc(callFile, 1), MapperLocation::makeLoc(callFile, 11)};

    const std::vector<AstNodePtr> roots = TermToAstConverter::termToAst(term, ctx);
    ASSERT_FALSE(roots.empty());
    const AstNodeBase& node = *roots.front();
    ASSERT_NE(node.as_text(), nullptr);

    EXPECT_EQ(node.as_text()->text(), "shared_name") << "spelling приходит из определения макроса";
    EXPECT_EQ(node.range().begin.fileIdx(), callFile) << "range - provenance сайта вызова";
    const std::string_view callSource = ctx.source().source(callFile);
    const std::string_view sourceAtRange = callSource.substr(node.range().begin.offset() - 1, node.range().end.offset() - node.range().begin.offset());
    EXPECT_EQ(sourceAtRange, "other_call");
    EXPECT_NE(node.as_text()->text(), sourceAtRange) << "текст и диапазон - разные факты, текст из range не выводится";
    EXPECT_TRUE(ctx.strings().owns(node.as_text()->text()));
}

// (S6) Аргументы DebugStmt - интернированные view в пул компиляции (без копий текста в узле).
TEST(TextStorageTest, DebugStmtArgsAreInterned) {
    Context ctx;
    const TermPtr marker = Term::Create(TermID::MACRO_CONTEXT, std::string("@__DEBUG__"), {}, parser::token_type::MACRO_CONTEXT);
    marker->m_sequence.push_back(Term::Create(TermID::NAME, std::string("filter_mask")));

    const std::vector<AstNodePtr> roots = TermToAstConverter::termToAst(marker, ctx);
    ASSERT_EQ(roots.size(), 1u);
    const AstNodePtr& node = roots.front();
    ASSERT_NE(node, nullptr);
    ASSERT_EQ(node->kind(), ParserToken::Kind::DebugStmt);
    const auto* debug = node->as<DebugStmt>();
    ASSERT_NE(debug, nullptr);
    ASSERT_EQ(debug->args.size(), 1u);
    EXPECT_EQ(debug->args[0], "filter_mask");
    EXPECT_TRUE(ctx.strings().owns(debug->args[0])) << "DebugStmt args must be interned, not copied";
}

// (S3) AST-уровневый дедуп: одинаковый spelling в разных узлах - ОДИН интернированный view
// (не только `owns`, но и совпадение адреса).
TEST(TextStorageTest, EqualSpellingsShareInternedView) {
    StringInterner pool;
    StringInternerScope scope(pool);

    IdentName first("shared_spelling");
    IdentName second("shared_spelling");

    ASSERT_TRUE(pool.owns(first.text()));
    EXPECT_EQ(second.text().data(), first.text().data()) << "equal spellings must share one interned string";
    EXPECT_EQ(pool.size(), 1u) << "the pool must store the spelling once";
}

// (S3) Носитель текста не зависит от времени жизни Term: владелец текста - пул компиляции.
TEST(TextStorageTest, TextCarrierSurvivesTermDestruction) {
    Context ctx;
    StringInternerScope scope(ctx.strings());

    TermPtr term = Term::Create(TermID::STRCHAR, std::string("survivor_literal"));
    Literal literal(ParserToken::Kind::StrChar, term);
    ASSERT_EQ(literal.text(), "survivor_literal");

    term.reset(); // дерево Term уничтожено, owned-строка освобождена
    EXPECT_EQ(literal.text(), "survivor_literal");
    EXPECT_TRUE(ctx.strings().owns(literal.text())) << "carrier text must live in the compilation pool";
}

} // namespace
} // namespace trust
