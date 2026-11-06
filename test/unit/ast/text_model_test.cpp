// test/unit/ast/text_model_test.cpp
// Инварианты модели текста: текст хранится ТОЛЬКО у WithText-классов (as_text()), запись - через
// HasText::set_text_interned; у не-текстовых узлов метода text() нет (compile-time).
#include "ast/ast_nodes.hpp"
#include "ast/clone.hpp"
#include "attrs/attr_builtin.hpp"
#include "attrs/attr_pool.hpp"
#include "syntax/term/term.h"
#include "utils/string_interner.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <string_view>
#include <type_traits>

namespace trust {
namespace {

using trust::ParserToken;

TEST(TextModelTest, NonTextFieldHasNoTextStorage) {
    AstNodeAttr ifStmt(ParserToken::Kind::IfStmt);
    EXPECT_EQ(ifStmt.as_text(), nullptr);

    auto semicolon = std::make_shared<SemicolonStmt>(nullptr);
    EXPECT_EQ(semicolon->as_text(), nullptr);

    auto error = std::make_shared<ErrorExpr>(nullptr);
    EXPECT_EQ(error->as_text(), nullptr);

    LastResultCapture capture;
    EXPECT_EQ(capture.as_text(), nullptr);
}

TEST(TextModelTest, TextFieldIsWritableOnlyThroughCarrier) {
    IdentName name("x");
    name.set_text_interned("renamed");
    EXPECT_EQ(name.text(), "renamed");

    AstNodeAttr ifStmt(ParserToken::Kind::IfStmt);
    EXPECT_EQ(ifStmt.as_text(), nullptr) << "non-text node cannot expose a text field";
}

TEST(TextModelTest, ManualWithTextInternsImmediately) {
    Literal literal(ParserToken::Kind::IntLiteral, std::string("x"));
    EXPECT_EQ(literal.text(), "x");
    EXPECT_TRUE(literal.range().isInvalid());
}

TEST(TextModelTest, RawTextKeepsSpellingWithoutNormalization) {
    StringInterner pool;
    StringInternerScope scope(pool);

    RawText doc(ParserToken::Kind::Document, "/// doc ^");
    EXPECT_EQ(doc.text(), "/// doc ^") << "comment text must not be normalized";
    EXPECT_TRUE(pool.owns(doc.text()));

    RawText embed(ParserToken::Kind::EmbedExpr, "@@ cpp ^ @@");
    EXPECT_EQ(embed.text(), "@@ cpp ^ @@") << "raw C++ insert must not be normalized";
    EXPECT_TRUE(pool.owns(embed.text()));

    // Терм-путь (не только manual): конвертация берёт текст как есть.
    const TermPtr docTerm = Term::Create(TermID::DOCUMENT, std::string("/// term doc ^"));
    RawText docFromTerm(ParserToken::Kind::Document, docTerm);
    EXPECT_EQ(docFromTerm.text(), "/// term doc ^");
    EXPECT_TRUE(pool.owns(docFromTerm.text()));

    const TermPtr embedTerm = Term::Create(TermID::EMBED, std::string("@@ term cpp ^ @@"));
    RawText embedFromTerm(ParserToken::Kind::EmbedExpr, embedTerm);
    EXPECT_EQ(embedFromTerm.text(), "@@ term cpp ^ @@");
    EXPECT_TRUE(pool.owns(embedFromTerm.text()));
}

TEST(TextModelTest, BinaryOperatorGlyphLivesInOpcodeOnly) {
    StringInterner pool;
    StringInternerScope scope(pool);
    const TermPtr op = Term::Create(TermID::OP_MATH, "+");
    Binary binary(ParserToken::Kind::MathOp, op);
    EXPECT_EQ(binary.as_text(), nullptr) << "operator glyph is not node text";
    EXPECT_EQ(binary.m_op, BinaryOp::Add);
    EXPECT_FALSE(pool.owns("+")) << "operator glyph must not be interned as AST text";
}

TEST(TextModelTest, LabelRefStoresInternedTextInHasText) {
    StringInterner pool;
    StringInternerScope scope(pool);
    auto label = std::make_shared<LabelRef>(ParserToken::Kind::LabelStmt, "L_break");

    ASSERT_EQ(label->text(), "L_break");
    EXPECT_TRUE(pool.owns(label->text())) << "LabelRef text must be interned like any carrier text";
    EXPECT_TRUE(label->range().isInvalid());

    const std::vector<AstNodePtr> clones = cloneTrees({label});
    ASSERT_EQ(clones.size(), 1u);
    ASSERT_NE(clones.front(), nullptr);
    EXPECT_EQ(clones.front()->as_text()->text().data(), label->text().data()) << "clone must share the interned view";
    EXPECT_EQ(clones.front()->as_text()->text(), "L_break");
}

TEST(TextModelTest, LogicalXorKeepsGlyphSemanticsInOpcode) {
    StringInterner pool;
    StringInternerScope scope(pool);
    const TermPtr op = Term::Create(TermID::OP_LOGICAL, "^^");
    Binary binary(ParserToken::Kind::LogicalOp, op);

    EXPECT_EQ(binary.as_text(), nullptr) << "operator glyph is not node text";
    EXPECT_EQ(binary.m_op, BinaryOp::LogicalXor) << "trailing '^' must not be lost as a qualifier";
    EXPECT_FALSE(pool.owns("^^"));
    EXPECT_FALSE(pool.owns("^"));
}

TEST(TextModelTest, LiteralBodyKeepsTrailingCaret) {
    StringInterner pool;
    StringInternerScope scope(pool);
    const TermPtr term = Term::Create(TermID::STRCHAR, "abc^");
    Literal literal(ParserToken::Kind::StrChar, term);

    EXPECT_EQ(literal.text(), "abc^") << "string literal body is not a name and must stay unchanged";
    EXPECT_TRUE(pool.owns("abc^"));
}

TEST(TextModelTest, IdentifierNormalizationIsSingleSided) {
    StringInterner pool;
    StringInternerScope scope(pool);
    AttrPool attrs;
    const auto readonlyId = attrs.lookup(attr::ReadOnly);
    ASSERT_TRUE(readonlyId.has_value());

    const TermPtr term = Term::Create(TermID::NAME, "x^");
    IdentName name(term, &attrs);

    EXPECT_EQ(name.text(), "x");
    EXPECT_FALSE(pool.owns("x^")) << "raw identifier spelling must not be interned";
    EXPECT_TRUE(name.has_attr(*readonlyId));
    EXPECT_EQ(name.attrs().size(), 1u) << "ReadOnly must be applied exactly once";

    // Повторный вызов (например, конвертером) не добавляет второй атрибут.
    applyReadonlyFromCaret(name, "x^", &attrs);
    EXPECT_EQ(name.attrs().size(), 1u);
}

TEST(TextModelTest, TypeNameStripsColonAndCaret) {
    StringInterner pool;
    StringInternerScope scope(pool);
    AttrPool attrs;
    const TermPtr term = Term::Create(TermID::TYPE, ":Int32^");
    IdentType type(term, std::nullopt, std::nullopt, &attrs);

    EXPECT_EQ(type.text(), "Int32");
    EXPECT_FALSE(pool.owns(":Int32"));
    EXPECT_TRUE(type.has_attr(*attrs.lookup(attr::ReadOnly)));
}

TEST(TextModelTest, ArgNodeNormalizesNameAndAppliesReadonly) {
    StringInterner pool;
    StringInternerScope scope(pool);
    AttrPool attrs;
    const TermPtr term = Term::Create(TermID::NAME, "argv^");
    ArgNode arg(term, nullptr, nullptr, &attrs);

    EXPECT_EQ(arg.text(), "argv");
    EXPECT_TRUE(arg.has_attr(*attrs.lookup(attr::ReadOnly)));
}

TEST(TextModelTest, SpecialLastResultNameKeepsCaretAndReadonly) {
    StringInterner pool;
    StringInternerScope scope(pool);
    AttrPool attrs;
    const TermPtr term = Term::Create(TermID::NAME, "$^");
    IdentName lastResult(term, &attrs);

    EXPECT_EQ(lastResult.text(), "$^");
    EXPECT_TRUE(lastResult.attrs().empty()) << "'^' of '$^' is part of the name, not a qualifier";
}

TEST(TextModelTest, EmptyTextIsNotInterned) {
    StringInterner pool;
    StringInternerScope scope(pool);
    Literal unnamed(ParserToken::Kind::IntLiteral, std::string{});

    EXPECT_TRUE(unnamed.text().empty());
    EXPECT_FALSE(pool.owns(""));
}

// (S5) Вне компиляции ручные узлы интернируются в процессный пул по явному ManualInternerScope.
TEST(TextModelTest, ManualNodesUseProcessInternerUnderManualScope) {
    ASSERT_EQ(detail::activeInternerSlot(), nullptr) << "test must run without an active compilation pool";
    ManualInternerScope manualScope;
    const std::string_view text = "manual_fallback_unique_name";
    Literal node(ParserToken::Kind::StrChar, std::string(text));

    EXPECT_EQ(node.text(), text);
    EXPECT_TRUE(manualStringInterner().owns(node.text()));
}

// (S5) Пустое написание очищает текст узла и не создаёт запись "" в пуле.
TEST(TextModelTest, EmptySpellingClearsTextWithoutInterning) {
    StringInterner pool;
    StringInternerScope scope(pool);
    IdentName name("cleared_name");
    ASSERT_TRUE(pool.owns(name.text()));

    name.set_text_interned("");
    EXPECT_TRUE(name.text().empty());
    EXPECT_FALSE(pool.owns(""));
}

// Не-текстовые узлы не имеют хранилища текста (as_text() == nullptr) — проверяем на
// представителях всех базовых форм: AstNodeAttr/DestructureDecl/TrustContract/TrustElem.
TEST(TextModelTest, NonTextKindsHaveNoStorage) {
    const auto expectNoText = [](const AstNodeBase& node) { EXPECT_EQ(node.as_text(), nullptr); };

    expectNoText(AstNodeAttr(ParserToken::Kind::CallExpr));
    expectNoText(AstNodeAttr(ParserToken::Kind::ReturnStmt));
    expectNoText(AstNodeAttr(ParserToken::Kind::WhileStmt));
    expectNoText(AstNodeAttr(ParserToken::Kind::Program));
    expectNoText(AstNodeAttr(ParserToken::Kind::VarRef));
    expectNoText(DestructureDecl{});
    expectNoText(TrustContract{});
    expectNoText(TrustElem{});
}

TEST(TextModelTest, EndNodeHasNoTextStorage) {
    AstNodeBase node;
    EXPECT_TRUE(node.empty());
    EXPECT_EQ(node.as_text(), nullptr);
}

// S2: Sequence (верхний уровень файла) текста не несёт; именованная область (Block = ScopeBlock)
// и модульная область (ModuleNode) - носители имени.
TEST(TextModelTest, PlainSequenceHasNoTextButNamedScopeDoes) {
    Sequence seq(ParserToken::Kind::sequence);
    EXPECT_EQ(seq.as_text(), nullptr);

    ScopeBlock block("ns::name::");
    ASSERT_NE(block.as_text(), nullptr);
    EXPECT_EQ(block.text(), "ns::name::");
    EXPECT_EQ(block.name(), "ns::name::");

    ModuleNode module(ModuleIndex{0}, std::string("mod"));
    ASSERT_NE(module.as_text(), nullptr);
    EXPECT_EQ(module.importSpelling(), "mod");
}

// S2: вид перехвата TryCatchStmt — поле TryKind, а не текст-маркер `{+`/`{-`/`{*`.
TEST(TextModelTest, TryCatchStmtCarriesTryKindNotText) {
    TryCatchStmt plus(ParserToken::Kind::TryCatchStmt, Term::Create(TermID::BLOCK_PLUS, "+"));
    EXPECT_EQ(plus.m_tryKind, TryKind::Plus);
    EXPECT_EQ(plus.as_text(), nullptr);

    TryCatchStmt minus(ParserToken::Kind::TryCatchStmt, Term::Create(TermID::BLOCK_MINUS, "-"));
    EXPECT_EQ(minus.m_tryKind, TryKind::Minus);

    TryCatchStmt any(ParserToken::Kind::TryCatchStmt, Term::Create(TermID::BLOCK_TRY, "*"));
    EXPECT_EQ(any.m_tryKind, TryKind::Any);
}

// requiredText - только для носителей текста: у не-носителя EXPECT (no silent empty).
TEST(TextModelTest, RequiredTextRejectsNonCarrier) {
    AstNodeAttr ifStmt(ParserToken::Kind::IfStmt);
    EXPECT_THROW((void)requiredText(ifStmt), std::runtime_error);
    EXPECT_EQ(requiredText(*std::make_shared<IdentName>("x")), "x");
}

// (S2) Документация пишется ТОЛЬКО через setDocumentation - интернируется в активный пул
// (единая точка записи, как у HasText::set_text_interned).
TEST(TextModelTest, DocumentationWritingGoesThroughInterner) {
    StringInterner pool;
    StringInternerScope scope(pool);

    AstNodeAttr decl(ParserToken::Kind::VarDecl);
    decl.setDocumentation("/// doc");
    EXPECT_EQ(decl.documentation, "/// doc");
    EXPECT_TRUE(pool.owns(decl.documentation));

    const std::size_t before = pool.size();
    decl.setDocumentation("");
    EXPECT_TRUE(decl.documentation.empty());
    EXPECT_EQ(pool.size(), before) << "empty documentation must not be interned";
}

// (S2) Узел-носитель текста без источника текста - нарушение инварианта, а не тихий пустой текст.
TEST(TextModelTest, CarrierTermConstructorRejectsNullTerm) {
    EXPECT_THROW((void)Literal(ParserToken::Kind::IntLiteral, TermPtr{}), std::runtime_error);
    EXPECT_THROW((void)ContextMacro(ParserToken::Kind::ContextMacro, TermPtr{}, nullptr), std::runtime_error);
}

// (S2) Повторная запись того же интернированного view - no-op (пул не растёт, адрес тот же).
TEST(TextModelTest, RepeatedSetTextDoesNotGrowPool) {
    StringInterner pool;
    StringInternerScope scope(pool);

    IdentName name("repeated_name");
    const std::string_view first = name.text();
    const std::size_t before = pool.size();

    name.set_text_interned("repeated_name");
    EXPECT_EQ(name.text().data(), first.data());
    EXPECT_EQ(pool.size(), before) << "re-interning the same spelling must not grow the pool";
}

// (S3) Политика «какие kinds несут текст» = класс-носитель (`WithText`/`HasText`). Тест сверяет
// ожидаемый набор kinds с фактическим классом из PARSER_TOKEN_KINDS: случайное переотображение
// kind на маркер/носитель ломает тест, а не «тихо» меняет модель текста.
constexpr bool isTextCarrierKind(ParserToken::Kind k) noexcept {
    switch (k) {
#define TEXT_CARRIER_KIND(name)   \
    case ParserToken::Kind::name: \
        return true;
        TEXT_CARRIER_KIND(Ident)
        TEXT_CARRIER_KIND(TypeName)
        TEXT_CARRIER_KIND(EmbedExpr)
        TEXT_CARRIER_KIND(Document)
        TEXT_CARRIER_KIND(ContextMacro)
        TEXT_CARRIER_KIND(CheckAreaStmt)
        TEXT_CARRIER_KIND(DebugStmt)
        TEXT_CARRIER_KIND(IntLiteral)
        TEXT_CARRIER_KIND(FloatLiteral)
        TEXT_CARRIER_KIND(StrChar)
        TEXT_CARRIER_KIND(StrWide)
        TEXT_CARRIER_KIND(RationalLiteral)
        TEXT_CARRIER_KIND(GotoStmt)
        TEXT_CARRIER_KIND(LabelStmt)
        TEXT_CARRIER_KIND(FuncDecl)
        TEXT_CARRIER_KIND(VarDecl)
        TEXT_CARRIER_KIND(ClassDecl)
        TEXT_CARRIER_KIND(StructDecl)
        TEXT_CARRIER_KIND(ArgNode)
        TEXT_CARRIER_KIND(ScopeBlock)
        TEXT_CARRIER_KIND(ModuleDecl)
#undef TEXT_CARRIER_KIND
    default:
        return false;
    }
}

TEST(TextModelTest, KindTextCarrierPolicyMatchesNodeClass) {
#define CHECK_KIND_CARRIER(name, node_type, category) \
    EXPECT_EQ(isTextCarrierKind(ParserToken::Kind::name), (std::is_base_of_v<HasText, node_type>)) << "kind '" #name "' maps to class " #node_type;
    PARSER_TOKEN_KINDS(CHECK_KIND_CARRIER)
#undef CHECK_KIND_CARRIER
}

// (S3) Повторная запись той же документации - no-op: указатель стабилен, пул не растёт.
// Проверяется и fast-path (передан уже интернированный view), и повторный spelling из литерала.
TEST(TextModelTest, RepeatedDocumentationDoesNotGrowPool) {
    StringInterner pool;
    StringInternerScope scope(pool);

    AstNodeAttr decl(ParserToken::Kind::VarDecl);
    decl.setDocumentation("/// repeated doc");
    const std::string_view first = decl.documentation;
    const std::size_t before = pool.size();
    ASSERT_TRUE(pool.owns(first));

    decl.setDocumentation(first); // уже интернированный view - fast-path
    EXPECT_EQ(decl.documentation.data(), first.data());
    EXPECT_EQ(pool.size(), before);

    decl.setDocumentation("/// repeated doc"); // тот же spelling из другого буфера
    EXPECT_EQ(decl.documentation.data(), first.data()) << "equal spelling must resolve to the interned view";
    EXPECT_EQ(pool.size(), before);
}

} // namespace
} // namespace trust
