// test/unit/ast/kind_dispatch_test.cpp - тесты единого механизма kind-диспетчеризации
// (ast/kind_dispatch.hpp): dispatchEmit + hasAllKindHandlers (exhaustive consumer),
// индекс-слоты таблиц partial pass, единая диагностика недостижимого kind и END-инварианты.

#include "ast/kind_dispatch.hpp"
#include "gtest/gtest.h"

#include <memory>
#include <string>
#include <vector>

namespace trust {
namespace {

/// Полный набор обработчиков - база «исчерпывающего» потребителя.
struct AllHandlersStub {
#define KIND_STUB(name, node_type, category) \
    void visit_##name(const node_type&) {}
    PARSER_TOKEN_KINDS(KIND_STUB)
#undef KIND_STUB
};

/// Неполный потребитель: реализован только один kind.
struct PartialHandlers {
    void visit_IntLiteral(const Literal&) {}
};

static_assert(hasAllKindHandlers<AllHandlersStub>(), "потребитель со всеми visit_<Kind> обязан проходить проверку");
static_assert(!hasAllKindHandlers<PartialHandlers>(), "потребитель без части visit_<Kind> обязан НЕ проходить проверку");

/// Записывающий потребитель: диспетчеризация по kind (не по классу узла).
struct RecordingConsumer : AllHandlersStub {
    std::string trace;
    void visit_IntLiteral(const Literal&) { trace += "IntLiteral;"; }
    void visit_FloatLiteral(const Literal&) { trace += "FloatLiteral;"; }
    void visit_Ident(const IdentName&) { trace += "Ident;"; }
    void visit_TypeName(const IdentType&) { trace += "TypeName;"; }
    // Несколько kinds → общий обработчик (одинаковый класс Binary).
    void visit_MathOp(const Binary&) { trace += "Binary;"; }
    void visit_AssignOp(const Binary&) { trace += "Binary;"; }
};

TEST(KindDispatchTest, DispatchesByKindNotByClass) {
    RecordingConsumer c;
    // Оба узла - класс Literal, но разные kinds → разные обработчики.
    dispatchEmit(*std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "1"), c);
    EXPECT_EQ(c.trace, "IntLiteral;");
    c.trace.clear();
    dispatchEmit(*std::make_shared<Literal>(ParserToken::Kind::FloatLiteral, "1.0"), c);
    EXPECT_EQ(c.trace, "FloatLiteral;");
}

TEST(KindDispatchTest, IdentAndTypeNameHaveDistinctHandlers) {
    // kind=Ident → visit_Ident; kind=TypeName → visit_TypeName (не по классу IdentName).
    RecordingConsumer c;
    dispatchEmit(*std::make_shared<IdentType>("Int32"), c);
    EXPECT_EQ(c.trace, "TypeName;");
    c.trace.clear();
    dispatchEmit(*std::make_shared<IdentName>("x"), c);
    EXPECT_EQ(c.trace, "Ident;");
}

TEST(KindDispatchTest, MultipleKindsMayShareOneHandler) {
    RecordingConsumer c;
    dispatchEmit(*std::make_shared<Binary>(ParserToken::Kind::MathOp), c);
    EXPECT_EQ(c.trace, "Binary;");
    c.trace.clear();
    dispatchEmit(*std::make_shared<Binary>(ParserToken::Kind::AssignOp), c);
    EXPECT_EQ(c.trace, "Binary;");
}

TEST(KindDispatchTest, EndIsNotANode) {
    // END - не узел: диспетчер обязан упасть, а не молча вернуться.
    RecordingConsumer c;
    auto end_node = std::make_shared<AstNodeAttr>(); // kind = END по умолчанию
    EXPECT_THROW(dispatchEmit(*end_node, c), std::exception);
    EXPECT_TRUE(c.trace.empty());
}

TEST(KindDispatchTest, FaultUnexpectedKindCarriesKindName) {
    // Единая диагностика «узел недопустим в фазе» обязана называть kind (иначе ошибку не отладить).
    Literal literal(ParserToken::Kind::RationalLiteral, "1/2");
    try {
        faultUnexpectedKind("TestConsumer", literal);
        FAIL() << "faultUnexpectedKind must throw";
    } catch (const std::exception& e) {
        const std::string message = e.what();
        EXPECT_NE(message.find("RationalLiteral"), std::string::npos) << message;
        EXPECT_NE(message.find("TestConsumer"), std::string::npos) << message;
    }
}

TEST(KindDispatchTest, CategoryComesFromSingleKindSource) {
    // Категория выводится из PARSER_TOKEN_KINDS, а не из ручной таблицы потребителя.
    EXPECT_EQ(kindCategory(ParserToken::Kind::IntLiteral), KindCategory::ExprOnly);
    EXPECT_EQ(kindCategory(ParserToken::Kind::IfStmt), KindCategory::StmtOnly);
    EXPECT_EQ(kindCategory(ParserToken::Kind::Ident), KindCategory::Any);
    EXPECT_EQ(kindCategory(ParserToken::Kind::END), KindCategory::Any);
}

TEST(KindDispatchTest, InvalidKindIsFault) {
    // Неизвестное значение enum не получает default-категорию.
    EXPECT_THROW(kindCategory(static_cast<ParserToken::Kind>(9999)), std::exception);
}

TEST(KindDispatchTest, ContextMismatchIsExplicit) {
    // Statement-only kind в expression-контексте обязан давать явную ошибку, а не
    // молчаливую эмиссию. Обратное направление не проверяется: expression-kind допустим
    // как standalone-выражение (block child/unit-тест).
    RecordingConsumer c;
    IfStmt if_stmt;
    EXPECT_THROW(dispatchEmit(if_stmt, c, KindCategory::ExprOnly), std::exception);
}

TEST(KindDispatchTest, KindIndexFitsEverySlot) {
    // END = 0, каждый kind из PARSER_TOKEN_KINDS адресуется своим слотом таблицы.
    static_assert(kindIndex(ParserToken::Kind::END) == 0);
    static_assert(kindIndex(ParserToken::Kind::sequence) == 1);
#define KIND_SLOT_BOUNDS(name, node_type, category) static_assert(kindIndex(ParserToken::Kind::name) < kParserKindSlotCount);
    PARSER_TOKEN_KINDS(KIND_SLOT_BOUNDS)
#undef KIND_SLOT_BOUNDS
}

TEST(KindChildrenTest, EndIsNotANode) {
    // Обход отвергает END явно (FAULT в forEachSlot) — «тихого» листа нет.
    AstNodeAttr end_node; // kind = END по умолчанию
    std::vector<AstNodePtr*> slots;
    EXPECT_THROW(end_node.collectChildren(slots), std::exception);
    EXPECT_TRUE(slots.empty());
}

// S0: смена kind допустима только внутри одного класса AST (sameNodeClass), иначе формат
// хранения/смысл текста могли бы измениться (перевод узла в оператор/блок).
static_assert(sameNodeClass(ParserToken::Kind::DictLiteral, ParserToken::Kind::Tuple));
static_assert(sameNodeClass(ParserToken::Kind::MathOp, ParserToken::Kind::AssignOp));
static_assert(!sameNodeClass(ParserToken::Kind::DictLiteral, ParserToken::Kind::MathOp));
static_assert(!sameNodeClass(ParserToken::Kind::Program, ParserToken::Kind::sequence));
static_assert(sameNodeClass(ParserToken::Kind::Program, ParserToken::Kind::VarRef));

TEST(SetKindTest, RefinesKindWithinSameClassOnly) {
    DictLiteralNode dict(ParserToken::Kind::DictLiteral);
    dict.setKind(ParserToken::Kind::Tuple);
    EXPECT_EQ(dict.kind(), ParserToken::Kind::Tuple);

    EXPECT_THROW(dict.setKind(ParserToken::Kind::MathOp), std::exception) << "operator kind must be rejected";
    EXPECT_EQ(dict.kind(), ParserToken::Kind::Tuple) << "failed setKind must not change kind";

    AstNodeAttr program(ParserToken::Kind::Program);
    program.setKind(ParserToken::Kind::VarRef); // тот же класс AstNodeAttr
    EXPECT_EQ(program.kind(), ParserToken::Kind::VarRef);
    EXPECT_THROW(program.setKind(ParserToken::Kind::sequence), std::exception) << "container kind must be rejected";
    EXPECT_EQ(program.kind(), ParserToken::Kind::VarRef);
}

TEST(SetKindTest, EndIsUnsetKindInitialisation) {
    // END - не kind узла, а «kind не задан»: инициализация разрешена (default-конструирование
    // в тестах/codegen-хелперах), последующая переклассификация - по общему правилу.
    IfStmt ifStmt;
    EXPECT_EQ(ifStmt.kind(), ParserToken::Kind::END);
    ifStmt.setKind(ParserToken::Kind::IfStmt);
    EXPECT_EQ(ifStmt.kind(), ParserToken::Kind::IfStmt);
}

} // namespace
} // namespace trust
