#include "syntax/parser_test_fixture.hpp"
TEST_F(ParserTest, DocCommentAnywhereInSequence) {
    // Док перед объявлением, standalone док, док внутри блока функции.
    ASSERT_TRUE(Parse("/// перед переменной\nx := 42;\n/// после\n%f():Void := {\n/// внутри функции\n++ 1 ++;\n};"));
    // Доки, привязанные к объявлениям (`x`, `%f`), лежат в m_docs терма-идентификатора и
    // НЕ являются отдельными DOCUMENT-термами. Док перед не-объявлением (`/// внутри функции`
    // перед `++ 1 ++;`) остаётся sibling-узлом DOCUMENT. Без ошибок синтаксиса.
    ASSERT_EQ(countDeclDocs(ast), 2) << "x and %f must carry their leading docs in m_docs";
    ASSERT_EQ(countDocTerms(ast), 1) << "only the non-decl doc remains a DOCUMENT term";
}

TEST_F(ParserTest, DocCommentFullTextInTerm) {
    ASSERT_TRUE(Parse("/// док\nx := 42;"));
    // Док привязан к терму-идентификатору объявления (m_docs) и сохраняется целиком с маркером '///'.
    bool found = false;
    std::function<void(const trust::TermPtr&)> visit = [&](const trust::TermPtr& t) {
        if (!t || found) {
            return;
        }
        for (const auto& d : t->m_docs) {
            EXPECT_EQ("/// док", std::string(d->getText()));
            found = true;
            return;
        }
        for (const auto& c : t->m_sequence) {
            visit(c);
        }
        if (t->m_left) {
            visit(t->m_left);
        }
        if (t->m_right) {
            visit(t->m_right);
        }
        if (t->m_args) {
            for (const auto& [name, v] : *t->m_args) {
                (void)name, visit(v);
            }
        }
    };
    visit(ast);
    ASSERT_TRUE(found) << "doc comment must be attached to the declaration term (m_docs)";
}

// -- Trust-конструкции: пред-/пост-условия и утверждения --
//   trust_pre:   @( <logical> @)
//   trust_post:  @< <logical> @>
//   trust_assert: @{ <logical> @}
// Пред/пост - после имени функции (в :=); утверждение - после имени переменной/типа
// (в :=/::=) и автономным оператором (с ';'). Конд-термы привязываются к терму-имени (m_sequence).

TEST_F(ParserTest, TrustCondPrePostOnFunc) {
    ASSERT_TRUE(Parse("%f(x:Int):Int @{ pre: a > 0 @} @{ post: c >= 0 @} := { r := x; };"));
    ASSERT_EQ(0, m_ctx.diag().errorCount());
    ASSERT_EQ(TermID::CREATE_NAME, ast->getTermID());
    ASSERT_TRUE(ast->m_left);
    ASSERT_EQ(2u, ast->m_left->m_sequence.size());
    EXPECT_EQ(TermID::TRUST_CONTRACT, ast->m_left->m_sequence[0]->getTermID());
    EXPECT_EQ(TermID::TRUST_CONTRACT, ast->m_left->m_sequence[1]->getTermID());
    // Содержимое - логическое выражение (оператор >) в m_right.
    ASSERT_TRUE(ast->m_left->m_sequence[0]->m_right);
    EXPECT_EQ(TermID::OP_COMPARE, ast->m_left->m_sequence[0]->m_right->getTermID());
    // Тело функции - в m_right оператора :=.
    ASSERT_TRUE(ast->m_right);
}

TEST_F(ParserTest, TrustCondAdjacencyNoSeparators) {
    // Несколько trust-контрактов подряд, без разделителей, вперемешку.
    ASSERT_TRUE(Parse("%f(x:Int):Int @{ pre: a > 0 @} @{ pre: b > 1 @} @{ post: c >= 0 @} := { r := x; };"));
    ASSERT_EQ(0, m_ctx.diag().errorCount());
    ASSERT_EQ(TermID::CREATE_NAME, ast->getTermID());
    ASSERT_TRUE(ast->m_left);
    ASSERT_EQ(3u, ast->m_left->m_sequence.size());
    EXPECT_EQ(TermID::TRUST_CONTRACT, ast->m_left->m_sequence[0]->getTermID());
    EXPECT_EQ(TermID::TRUST_CONTRACT, ast->m_left->m_sequence[1]->getTermID());
    EXPECT_EQ(TermID::TRUST_CONTRACT, ast->m_left->m_sequence[2]->getTermID());
}

TEST_F(ParserTest, TrustCondAssertOnVarDecl) {
    ASSERT_TRUE(Parse("y @{ check: y > 0 @} := 42;"));
    ASSERT_EQ(0, m_ctx.diag().errorCount());
    ASSERT_EQ(TermID::CREATE_NAME, ast->getTermID());
    ASSERT_TRUE(ast->m_left);
    ASSERT_EQ(1u, ast->m_left->m_sequence.size());
    EXPECT_EQ(TermID::TRUST_CONTRACT, ast->m_left->m_sequence[0]->getTermID());
    ASSERT_TRUE(ast->m_right);
    EXPECT_EQ(TermID::INTEGER, ast->m_right->getTermID());
}

TEST_F(ParserTest, TrustCondAssertOnTypeDecl) {
    ASSERT_TRUE(Parse("Status @{ check: s.valid @} ::= (OK=0,):Enum;"));
    ASSERT_EQ(0, m_ctx.diag().errorCount());
    ASSERT_EQ(TermID::CREATE_TYPE, ast->getTermID());
    ASSERT_TRUE(ast->m_left);
    ASSERT_EQ(1u, ast->m_left->m_sequence.size());
    EXPECT_EQ(TermID::TRUST_CONTRACT, ast->m_left->m_sequence[0]->getTermID());
}

TEST_F(ParserTest, TrustCondAssertStatement) {
    // Контракт в позиции выражения: автономный оператор с ';'.
    ASSERT_TRUE(Parse("@{ check: data != null @};"));
    ASSERT_EQ(0, m_ctx.diag().errorCount());
    ASSERT_EQ(TermID::TRUST_CONTRACT, ast->getTermID());
    ASSERT_TRUE(ast->m_right);
    EXPECT_EQ(TermID::OP_COMPARE, ast->m_right->getTermID());
}

TEST_F(ParserTest, TrustCondExprWithCall) {
    // Содержимое - полноценное логическое выражение: вызов функции.
    ASSERT_TRUE(Parse("@{ check: f(a) @};"));
    ASSERT_EQ(0, m_ctx.diag().errorCount());
    ASSERT_EQ(TermID::TRUST_CONTRACT, ast->getTermID());
    ASSERT_TRUE(ast->m_right);
}

TEST_F(ParserTest, TrustCondExprWithMemberAccess) {
    // Содержимое - логическое выражение с доступом по полю.
    ASSERT_TRUE(Parse("@{ check: s.valid @};"));
    ASSERT_EQ(0, m_ctx.diag().errorCount());
    ASSERT_EQ(TermID::TRUST_CONTRACT, ast->getTermID());
    ASSERT_TRUE(ast->m_right);
    EXPECT_EQ(TermID::FIELD, ast->m_right->getTermID());
}

TEST_F(ParserTest, TrustCondInvalidUnterminated) {
    // Незакрытый маркер trust-контракта.
    ASSERT_NO_THROW(Parse("@{ x > 0 "));
    ASSERT_GT(m_ctx.diag().errorCount(), 0);
}

TEST_F(ParserTest, TrustCondInvalidPreAtStatementStart) {
    // Пред-условие допустимо только после имени объявления, не как оператор.
    ASSERT_NO_THROW(Parse("@{ pre: x > 0 @} := 5;"));
    ASSERT_GT(m_ctx.diag().errorCount(), 0);
}

TEST_F(ParserTest, TrustCondInvalidAssertAfterNameWithoutOp) {
    // Контракт после имени ОБЯЗАН сопровождаться :=/::= (нет разделителя).
    ASSERT_NO_THROW(Parse("x @{ check: c @};"));
    ASSERT_GT(m_ctx.diag().errorCount(), 0);
}

TEST_F(ParserTest, TrustCondInvalidAssertNoSemicolon) {
    // Утверждение-оператор требует ';' перед следующим оператором.
    ASSERT_NO_THROW(Parse("@{ a @} x := 1;"));
    ASSERT_GT(m_ctx.diag().errorCount(), 0);
}

// -- Доступ к полю/методу после вызова: `f(a).b`, чейнинг `g().x.y` (method chaining). --

TEST_F(ParserTest, MemberAccessAfterCall) {
    // `f(a).b` - MemberAccess(left=CallExpr(f,a), right=Ident b).
    ASSERT_TRUE(Parse("x := f(a).b;"));
    ASSERT_EQ(0, m_ctx.diag().errorCount());
    ASSERT_EQ(TermID::CREATE_NAME, ast->getTermID());
    ASSERT_TRUE(ast->m_right);
    EXPECT_EQ(TermID::FIELD, ast->m_right->getTermID());
    ASSERT_TRUE(ast->m_right->m_left);
    EXPECT_EQ(TermID::NAME, ast->m_right->m_left->getTermID());
    ASSERT_TRUE(ast->m_right->m_left->m_args);
}

TEST_F(ParserTest, MemberAccessCallChaining) {
    // Чейнинг: `g().x.y` -> FIELD(FIELD(CallExpr(g), x), y).
    ASSERT_TRUE(Parse("y := g().x.y;"));
    ASSERT_EQ(0, m_ctx.diag().errorCount());
    ASSERT_EQ(TermID::CREATE_NAME, ast->getTermID());
    ASSERT_TRUE(ast->m_right);
    EXPECT_EQ(TermID::FIELD, ast->m_right->getTermID());
    ASSERT_TRUE(ast->m_right->m_left);
    EXPECT_EQ(TermID::FIELD, ast->m_right->m_left->getTermID());
    ASSERT_TRUE(ast->m_right->m_left->m_left);
    EXPECT_EQ(TermID::NAME, ast->m_right->m_left->m_left->getTermID());
}

TEST_F(ParserTest, TrustElemBasic) {
    ASSERT_TRUE(Parse("g(x:Int32):Int32 := { y := @( result @); };"));
    EXPECT_EQ(0, m_ctx.diag().errorCount());
}

// ═══════════════════════════════════════════════════════════════
// Раскрытие предопределённых макросов внутри C++-вставки {% ... %}
// ═══════════════════════════════════════════════════════════════

TEST_F(ParserTest, EmbedExpandsPredefLineMacro) {
    // @__LINE__ внутри {% %} раскрывается в номер строки (вставка на строке 1).
    ASSERT_TRUE(Parse("{% int x = @__LINE__; %}"));
    ASSERT_EQ(0, m_ctx.diag().errorCount());
    ASSERT_EQ(TermID::EMBED, ast->getTermID()) << ast->toString();
    EXPECT_EQ(std::string::npos, ast->getText().find("@__LINE__")) << ast->getText();
    EXPECT_NE(std::string::npos, ast->getText().find("= 1;")) << ast->getText();
}

TEST_F(ParserTest, EmbedKeepsContextMacros) {
    // Контекст-макрос @__FUNCTION__ (требует анализатора) в {% %} НЕ раскрывается здесь,
    // но фиксируется ДИАГНОСТИКА (вместо тихого fallback/глотания ошибки), а текст остаётся.
    ASSERT_TRUE(Parse("{% int x = @__FUNCTION__; %}"));
    ASSERT_EQ(TermID::EMBED, ast->getTermID()) << ast->toString();
    EXPECT_NE(std::string::npos, ast->getText().find("@__FUNCTION__")) << ast->getText();
    EXPECT_GT(m_ctx.diag().errorCount(), 0) << "expected diagnostic for context macro in {% %}";
}

TEST_F(ParserTest, EmbedDiagnosesPragmaMacro) {
    // Прагма-макрос в {% %} не раскрывается - диагностика вместо тихого «ухода в сырой C++».
    ASSERT_TRUE(Parse("{% int x = @__OPTION_PUSH__; %}"));
    ASSERT_EQ(TermID::EMBED, ast->getTermID()) << ast->toString();
    EXPECT_GT(m_ctx.diag().errorCount(), 0) << "expected diagnostic for pragma macro in {% %}";
}
