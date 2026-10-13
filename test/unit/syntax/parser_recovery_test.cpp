#include "syntax/parser_test_fixture.hpp"

#include <string_view>

using namespace trust;

namespace {

// Рекурсивный поиск текста терма в дереве (Term-поля те же, что обходит convertSeq).
bool termContainsText(const trust::TermPtr& t, std::string_view needle) {
    if (!t) {
        return false;
    }
    if (t->getText() == needle) {
        return true;
    }
    for (const auto& c : t->m_sequence) {
        if (termContainsText(c, needle)) {
            return true;
        }
    }
    if (termContainsText(t->m_left, needle) || termContainsText(t->m_right, needle)) {
        return true;
    }
    if (t->m_args) {
        for (const auto& [name, value] : *t->m_args) {
            (void)name;
            if (termContainsText(value, needle)) {
                return true;
            }
        }
    }
    return false;
}

} // namespace

// Восстановление на границе оператора: ошибочные операторы пропускаются, корректные
// операторы ДО и ПОСЛЕ ошибки сохраняются в дереве; каждая ошибка репортится один раз.
TEST_F(ParserTest, RecoverySequenceKeepsValidStatements) {
    Parse("x := ;\ny := 1;\nz := ;\nw := 2;\n");
    EXPECT_EQ(2, m_ctx.diag().errorCount());
    EXPECT_TRUE(termContainsText(ast, "y"));
    EXPECT_TRUE(termContainsText(ast, "w"));
    // Ошибочные операторы в дерево не попадают.
    EXPECT_FALSE(termContainsText(ast, "x"));
    EXPECT_FALSE(termContainsText(ast, "z"));
}

// Восстановление внутри блока: ошибка не уничтожает ни предыдущий, ни следующий оператор.
TEST_F(ParserTest, RecoveryBlockKeepsValidStatements) {
    Parse("{\n  a := ;\n  b := 1;\n  c := ;\n  d := 2;\n}\n");
    EXPECT_EQ(2, m_ctx.diag().errorCount());
    EXPECT_TRUE(termContainsText(ast, "b"));
    EXPECT_TRUE(termContainsText(ast, "d"));
}

// Восстановление в теле класса (class_props): ошибка члена не уничтожает остальные члены.
TEST_F(ParserTest, RecoveryClassMembersKeepValidMembers) {
    Parse(":Point ::= :Struct {\n  x:Int32 := ;\n  y:Int32 := 1;\n  z:Int32 := ;\n  w:Int32 := 2;\n};\n");
    EXPECT_EQ(2, m_ctx.diag().errorCount());
    EXPECT_TRUE(termContainsText(ast, "y"));
    EXPECT_TRUE(termContainsText(ast, "w"));
}

// Предел ошибок: после kDefaultErrorLimit разбор прекращается, а не продолжается бесконечно.
TEST_F(ParserTest, RecoveryStopsAtErrorLimit) {
    std::string src;
    for (int i = 0; i < 25; ++i) {
        src += "a" + std::to_string(i) + " := ;\n";
    }
    Parse(src);
    // DiagnosticEngine считает только Error (итоговое сообщение — Note, не входит в count).
    EXPECT_EQ(static_cast<int>(trust::kDefaultErrorLimit), m_ctx.diag().errorCount());
    EXPECT_TRUE(m_ctx.diag().errorLimitReached());
}
