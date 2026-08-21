// test/unit/ast/visit_test.cpp - unit-тесты для единой диспетчеризации AST по kind
// (ast/kind_visitor.hpp). Проверяют: вызов по kind (не по классу), типизацию методов
// visit_<Kind>(const <класс>&), группировку нескольких kinds в один обработчик и
// notImplemented для нереализованного kind.

#include "ast/kind_visitor.hpp"
#include "gtest/gtest.h"
#include <memory>
#include <string>

namespace trust {
namespace {

/// Хелпер для тестов: громкий сбой для осознанно нереализованного kind-обработчика
/// (замещает notImplemented, который был в продуктовом kind_visitor.hpp до переноса сюда).
[[noreturn]] void notImplemented(const char* kind_name) {
    FAULT("kind visitor: обработчик для kind '{}' не реализован", kind_name);
}

/// Базовая реализация KindVisitor с notImplemented-заглушками - нужна только тестам,
/// поэтому определяется здесь, а не в продуктовом ast/kind_visitor.hpp.
struct KindVisitorDefault : KindVisitor {
#define KIND_VISITOR_DEFAULT(name, node_type)           \
    void visit_##name(const node_type& node) override { \
        notImplemented(#name);                          \
        (void)node;                                     \
    }
    PARSER_TOKEN_KINDS(KIND_VISITOR_DEFAULT)
#undef KIND_VISITOR_DEFAULT
};

// Visitor на KindVisitorDefault, записывающий сработавшие kind-методы в порядке вызовов.
struct RecordingVisitor : KindVisitorDefault {
    std::string trace;
    void visit_IntLiteral(const Literal&) override { trace += "IntLiteral;"; }
    void visit_FloatLiteral(const Literal&) override { trace += "FloatLiteral;"; }
    void visit_sequence(const Sequence&) override { trace += "sequence;"; }
    void visit_Ident(const IdentName&) override { trace += "Ident;"; }
    void visit_TypeName(const IdentType&) override { trace += "TypeName;"; }
    // Несколько kinds → общий обработчик (например все бинарные statement'ы).
    void visit_MathOp(const Binary&) override { trace += "Binary;"; }
    void visit_AssignOp(const Binary&) override { trace += "Binary;"; }
};

TEST(KindVisitTest, DispatchesByKindNotByClass) {
    RecordingVisitor v;
    // Оба узла - класс Literal, но разные kinds → разные методы.
    dispatchKind(*std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "1"), v);
    EXPECT_EQ(v.trace, "IntLiteral;");
    v.trace.clear();
    dispatchKind(*std::make_shared<Literal>(ParserToken::Kind::FloatLiteral, "1.0"), v);
    EXPECT_EQ(v.trace, "FloatLiteral;");
}

TEST(KindVisitTest, IdentTypeAndIdentHaveDistinctMethods) {
    // kind=Ident → visit_Ident; kind=TypeName → visit_TypeName (не делегирование по классу).
    RecordingVisitor v;
    dispatchKind(*std::make_shared<IdentType>("Int32"), v);
    EXPECT_EQ(v.trace, "TypeName;");
    v.trace.clear();
    dispatchKind(*std::make_shared<IdentName>("x"), v);
    EXPECT_EQ(v.trace, "Ident;");
}

TEST(KindVisitTest, MultipleKindsGroupToSharedHandler) {
    // MathOp и AssignOp - оба класс Binary, но разные kinds; оба → общий обработчик.
    RecordingVisitor v;
    auto mkBinary = [](ParserToken::Kind k) { return std::make_shared<Binary>(k); };
    dispatchKind(*mkBinary(ParserToken::Kind::MathOp), v);
    EXPECT_EQ(v.trace, "Binary;");
    v.trace.clear();
    dispatchKind(*mkBinary(ParserToken::Kind::AssignOp), v);
    EXPECT_EQ(v.trace, "Binary;");
}

TEST(KindVisitTest, NotImplementedForUnhandledKind) {
    // kind=StrChar не переопределён в RecordingVisitor → необработанный kind.
    struct Strict : KindVisitorDefault {
        std::string trace;
    } v;
    // Sequence-метод visit_sequence переопределён выше, но здесь Strict его не имеет -
    // используем необработанный kind=StrChar → notImplemented кидает.
    EXPECT_THROW(dispatchKind(*std::make_shared<Literal>(ParserToken::Kind::StrChar, "s"), v), std::exception);
}

} // namespace
} // namespace trust
