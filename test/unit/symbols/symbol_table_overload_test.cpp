// test/unit/symbols/symbol_table_overload_test.cpp
// SymbolTable::declareOrComplete: C++-идентичность наборов перегрузок через FunctionIdentity
// с binder-контекстом конкретного FuncDecl; завершение одной перегрузки в наборе.

#include "symbols/symbol_table.hpp"
#include "ast/ast_nodes.hpp"
#include "ast/token.hpp"
#include "ast/token_type.hpp"
#include "session/context.hpp"
#include "syntax/term/term.h"
#include "syntax/term/term_types.h"
#include "types/type_system.hpp"
#include "types/names.hpp"
#include "gtest/gtest.h"

#include <memory>
#include <vector>

namespace trust {
namespace {

class SymbolTableOverloadTest : public ::testing::Test {
  protected:
    void SetUp() override {
        m_types = std::make_unique<TypeSystem>(m_ctx.diag(), m_ctx.opts());
        m_ctx.setTypes(m_types.get());
        m_symbols.setTypes(*m_types);
        m_i32 = m_types->read().getType(type::Int32);
        m_i64 = m_types->read().getType(type::Int64);
    }

    std::shared_ptr<FuncDecl> makeFunc(std::string_view name, bool definition, std::string_view binder = {}) {
        auto term = Term::Create(TermID::NAME, std::string(name), {}, parser::token_type::NAME);
        auto f = std::make_shared<FuncDecl>(std::move(term));
        if (!binder.empty()) {
            f->m_templateParams = std::vector<AstNodePtr>{std::make_shared<ArgNode>(std::string(binder))};
        }
        if (definition) {
            f->m_body = std::vector<AstNodePtr>{};
        }
        return f;
    }

    std::shared_ptr<FuncDecl> makeFuncWithBinders(std::string_view name, std::vector<std::string> binders) {
        auto term = Term::Create(TermID::NAME, std::string(name), {}, parser::token_type::NAME);
        auto f = std::make_shared<FuncDecl>(std::move(term));
        std::vector<AstNodePtr> params;
        params.reserve(binders.size());
        for (auto& b : binders) {
            params.push_back(std::make_shared<ArgNode>(std::move(b)));
        }
        f->m_templateParams = std::move(params);
        f->m_body = std::vector<AstNodePtr>{};
        return f;
    }

    Symbol symbol(std::string_view name, TypeId signature, FuncDecl* decl) {
        Symbol s;
        s.name = trust::stableSymbolName(name);
        s.type = QualType::fromTypeId(signature);
        s.decl = decl;
        s.overloads.push_back(SymbolOverload{signature, {}, decl});
        return s;
    }

    Context m_ctx;
    std::unique_ptr<TypeSystem> m_types;
    SymbolTable m_symbols;
    TypeId m_i32 = INVALID_TYPE_ID;
    TypeId m_i64 = INVALID_TYPE_ID;
};

TEST_F(SymbolTableOverloadTest, CompletesOneOverloadInExistingSet) {
    const TypeId sig32 = m_types->interner().getOrCreateFunctionType(m_i32, {m_i32});
    const TypeId sig64 = m_types->interner().getOrCreateFunctionType(m_i32, {m_i64});
    auto fwd32 = makeFunc("f", false);
    auto def64 = makeFunc("f", true);
    auto def32 = makeFunc("f", true);

    EXPECT_EQ(m_symbols.declareOrComplete(symbol("f", sig32, fwd32.get())), DeclResult::Inserted);
    EXPECT_EQ(m_symbols.declareOrComplete(symbol("f", sig64, def64.get())), DeclResult::Overloaded);
    EXPECT_EQ(m_symbols.declareOrComplete(symbol("f", sig32, def32.get())), DeclResult::Completed);

    const Symbol* s = m_symbols.resolve("f");
    ASSERT_NE(s, nullptr);
    ASSERT_EQ(s->overloads.size(), 2u);
    EXPECT_EQ(s->overloads[0].signature, getUnqualifiedType(sig32));
    EXPECT_EQ(s->overloads[1].signature, getUnqualifiedType(sig64));
    ASSERT_EQ(s->overloads.size(), 2u);
    EXPECT_EQ(s->overloads[0].decl, def32.get()); // forward заменён определением
}

TEST_F(SymbolTableOverloadTest, SingleFunctionSymbolHasOneTypedEntry) {
    const TypeId sig32 = m_types->interner().getOrCreateFunctionType(m_i32, {m_i32});
    auto def = makeFunc("f", true);
    ASSERT_EQ(m_symbols.declareOrComplete(symbol("f", sig32, def.get())), DeclResult::Inserted);
    const Symbol* s = m_symbols.resolve("f");
    ASSERT_NE(s, nullptr);
    ASSERT_EQ(s->overloads.size(), 1u);
    EXPECT_EQ(s->overloads[0].signature, getUnqualifiedType(sig32));
    EXPECT_EQ(s->overloads[0].decl, def.get());
    EXPECT_EQ(overloadSignatures(*s), (std::vector<TypeId>{getUnqualifiedType(sig32)}));
}

TEST_F(SymbolTableOverloadTest, DuplicateSignatureInSetIsDiagnosticResult) {
    const TypeId sig32 = m_types->interner().getOrCreateFunctionType(m_i32, {m_i32});
    auto def1 = makeFunc("f", true);
    auto def2 = makeFunc("f", true);
    EXPECT_EQ(m_symbols.declareOrComplete(symbol("f", sig32, def1.get())), DeclResult::Inserted);
    EXPECT_EQ(m_symbols.declareOrComplete(symbol("f", sig32, def2.get())), DeclResult::Duplicate);
}

TEST_F(SymbolTableOverloadTest, AliasAndBaseAreDistinctTrustOverloads) {
    // `MyInt ::= :Int32` — отдельный тип Trust: forward(MyInt) и definition(Int32) — РАЗНЫЕ
    // перегрузки (в C++ они различимы identity-суффиксом имени).
    const TypeId alias = m_types->registrar().registerType("MyInt", m_i32);
    ASSERT_NE(alias, INVALID_TYPE_ID);
    const TypeId sigAlias = m_types->interner().getOrCreateFunctionType(m_i32, {alias});
    const TypeId sigCanon = m_types->interner().getOrCreateFunctionType(m_i32, {m_i32});
    auto first = makeFunc("f", false);
    auto second = makeFunc("f", true);

    EXPECT_EQ(m_symbols.declareOrComplete(symbol("f", sigAlias, first.get())), DeclResult::Inserted);
    EXPECT_EQ(m_symbols.declareOrComplete(symbol("f", sigCanon, second.get())), DeclResult::Overloaded);
    const Symbol* s = m_symbols.resolve("f");
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(s->overloads.size(), 2u);
}

TEST_F(SymbolTableOverloadTest, SameAliasSpellingCompletesForwardDeclaration) {
    const TypeId alias = m_types->registrar().registerType("MyInt", m_i32);
    ASSERT_NE(alias, INVALID_TYPE_ID);
    const TypeId sigAlias = m_types->interner().getOrCreateFunctionType(m_i32, {alias});
    auto fwd = makeFunc("f", false);
    auto def = makeFunc("f", true);
    EXPECT_EQ(m_symbols.declareOrComplete(symbol("f", sigAlias, fwd.get())), DeclResult::Inserted);
    EXPECT_EQ(m_symbols.declareOrComplete(symbol("f", sigAlias, def.get())), DeclResult::Completed);
}

TEST_F(SymbolTableOverloadTest, NativeAliasCollisionIsDuplicate) {
    // Внешний C++-символ не различает `using`-алиасы: два нативных объявления с alias/base
    // неразличимы для C++ -> Duplicate, а не две «перегрузки».
    const TypeId alias = m_types->registrar().registerType("MyInt", m_i32);
    ASSERT_NE(alias, INVALID_TYPE_ID);
    const TypeId sigAlias = m_types->interner().getOrCreateFunctionType(m_i32, {alias});
    const TypeId sigCanon = m_types->interner().getOrCreateFunctionType(m_i32, {m_i32});
    auto first = makeFunc("%lib::f", false);
    auto second = makeFunc("%lib::f", false);

    EXPECT_EQ(m_symbols.declareOrComplete(symbol("%lib::f", sigAlias, first.get())), DeclResult::Inserted);
    EXPECT_EQ(m_symbols.declareOrComplete(symbol("%lib::f", sigCanon, second.get())), DeclResult::Duplicate);
}

TEST_F(SymbolTableOverloadTest, BinderPermutationIsADistinctOverload) {
    // `<T,U> f(a:U)` и `<U,T> f(a:U)`: одинаковый declared TypeId, но C++-позиции
    // используемого параметра разные (1 vs 0) - это РАЗНЫЕ шаблоны (C++ их допускает).
    // Identity берётся из binder-контекста, поэтому все объявления набора сохраняются.
    const TypeId u = m_types->interner().getOrCreateTemplateParamType("U");
    const TypeId sig = m_types->interner().getOrCreateFunctionType(m_i32, {u});
    auto first = makeFuncWithBinders("f", {"T", "U"});
    auto second = makeFuncWithBinders("f", {"U", "T"});

    EXPECT_EQ(m_symbols.declareOrComplete(symbol("f", sig, first.get())), DeclResult::Inserted);
    EXPECT_EQ(m_symbols.declareOrComplete(symbol("f", sig, second.get())), DeclResult::Overloaded);
}

TEST_F(SymbolTableOverloadTest, ReturnTypeMismatchIsSignatureMismatch) {
    // C++-идентичность параметров совпадает, но return type declaration/definition обязан быть
    // одним и тем же (в сигнатуру перегрузки return не входит).
    const TypeId sigRet32 = m_types->interner().getOrCreateFunctionType(m_i32, {m_i32});
    const TypeId sigRet64 = m_types->interner().getOrCreateFunctionType(m_i64, {m_i32});
    auto fwd = makeFunc("f", false);
    auto def = makeFunc("f", true);

    EXPECT_EQ(m_symbols.declareOrComplete(symbol("f", sigRet32, fwd.get())), DeclResult::Inserted);
    EXPECT_EQ(m_symbols.declareOrComplete(symbol("f", sigRet64, def.get())), DeclResult::SignatureMismatch);
}

TEST_F(SymbolTableOverloadTest, TrustMismatchIsSignatureMismatch) {
    // trust-условия — часть контракта объявления (отдельный функциональный TypeId).
    const TypeId plain = m_types->interner().getOrCreateFunctionType(m_i32, {m_i32});
    const TypeId trusted = m_types->interner().getOrCreateFunctionType(m_i32, {m_i32}, INVALID_TYPE_ID, /*hasTrust=*/true);
    auto fwd = makeFunc("f", false);
    auto def = makeFunc("f", true);

    EXPECT_EQ(m_symbols.declareOrComplete(symbol("f", plain, fwd.get())), DeclResult::Inserted);
    EXPECT_EQ(m_symbols.declareOrComplete(symbol("f", trusted, def.get())), DeclResult::SignatureMismatch);
}

TEST_F(SymbolTableOverloadTest, BinderRenameCompletesForwardDeclaration) {
    // Переименование template-параметра между объявлением и определением - валидный C++
    // (согласованное переименование): позиционная identity совпадает.
    const TypeId t = m_types->interner().getOrCreateTemplateParamType("T");
    const TypeId u = m_types->interner().getOrCreateTemplateParamType("U");
    const TypeId sigT = m_types->interner().getOrCreateFunctionType(m_i32, {t});
    const TypeId sigU = m_types->interner().getOrCreateFunctionType(m_i32, {u});
    auto fwd = makeFunc("f", false, "T");
    auto def = makeFunc("f", true, "U");
    EXPECT_EQ(m_symbols.declareOrComplete(symbol("f", sigT, fwd.get())), DeclResult::Inserted);
    EXPECT_EQ(m_symbols.declareOrComplete(symbol("f", sigU, def.get())), DeclResult::Completed);
}

TEST_F(SymbolTableOverloadTest, VoidReturnSpellingCompletesForwardDeclaration) {
    // Нет аннотации возврата, `:Void` и `:None` кодогенер эмитит как `void` — для C++ это один
    // return type, поэтому forward/definition с разным написанием НЕ является mismatch.
    const TypeId voidType = m_types->read().getType(type::Void);
    const TypeId sigNoRet = m_types->interner().getOrCreateFunctionType(INVALID_TYPE_ID, {m_i32});
    const TypeId sigVoid = m_types->interner().getOrCreateFunctionType(voidType, {m_i32});
    auto fwd = makeFunc("f", false);
    auto def = makeFunc("f", true);

    EXPECT_EQ(m_symbols.declareOrComplete(symbol("f", sigNoRet, fwd.get())), DeclResult::Inserted);
    EXPECT_EQ(m_symbols.declareOrComplete(symbol("f", sigVoid, def.get())), DeclResult::Completed);
}

TEST_F(SymbolTableOverloadTest, VoidToIntReturnMismatchIsSignatureMismatch) {
    const TypeId voidType = m_types->read().getType(type::Void);
    const TypeId sigVoid = m_types->interner().getOrCreateFunctionType(voidType, {m_i32});
    const TypeId sigInt = m_types->interner().getOrCreateFunctionType(m_i32, {m_i32});
    auto fwd = makeFunc("f", false);
    auto def = makeFunc("f", true);

    EXPECT_EQ(m_symbols.declareOrComplete(symbol("f", sigVoid, fwd.get())), DeclResult::Inserted);
    EXPECT_EQ(m_symbols.declareOrComplete(symbol("f", sigInt, def.get())), DeclResult::SignatureMismatch);
}

} // namespace
} // namespace trust
