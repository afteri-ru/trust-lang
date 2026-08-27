// test/unit/semantic/scope_liveness_test.cpp
// Проверка корректности разрешения имён В МОМЕНТ наличия нужных скоупов (во время обхода).
//
// Контекст: скоуп-модель открывает вложенный скоуп для модуля/блока и выталкивает его после
// анализа. Поэтому верхнеуровневые имена модуля НЕ переживают `NameResolutionPass::run()` и
// недоступны транспайлеру через SymbolTable (см. также transpiler: тип-алиасы и встроенные типы
// транспайлер берёт из реестра, а не из SymbolTable).
//
// Механизм проверки: анализатор-хук (`InlineAnalysisHook`), вызываемый ПАРАЛЛЕЛЬНО ядру
// разрешения имён ВО ВРЕМЯ обхода - когда скоупы (в т.ч. модульный) ещё активны. Это единственная
// точка, где имена уровня модуля наблюдаемы (после run() модульный скоуп вытолкнут).
// NameResolutionPass гоняется напрямую (SemanticPassRunner не даёт подключить произвольный хук).

#include "semantic/name_resolution.hpp"
#include "semantic/pass.hpp"
#include "semantic/inline_hook.hpp"
#include "semantic/symbol_table.hpp"
#include "ast/ast_nodes.hpp"
#include "ast/ident_name.hpp"
#include "ast/token.hpp"
#include "ast/token_type.hpp"
#include "diag/context.hpp"
#include "types/registry.hpp"
#include "types/type_id.hpp"
#include "gtest/gtest.h"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace trust {
namespace {

// -- Пробный хук: фиксирует резолв имени и тип алиаса в момент активных скоупов --
class NameProbeHook : public InlineAnalysisHook {
  public:
    explicit NameProbeHook(AnalysisContext& actx)
    : m_actx(actx) {}

    // onResolve срабатывает для Ident-ссылки (lookupOrError) во время обхода. Захватываем
    // тип (значение TypeId) - на момент вызова скоуп модуля активен, поэтому ссылка на
    // верхнеуровневую переменную обязана разрешиться (sym != nullptr).
    void onResolve(const AstNodeBase& node, const Symbol* sym) override {
        if (node.text() == kRefName) {
            m_refResolved = (sym != nullptr);
            if (sym) {
                m_refType = sym->type;
            }
            ++m_refObservations;
        }
    }

    // onDeclare срабатывает при объявлении тип-алиаса (MyInt ::= :Int32). Фиксируем его TypeId (значение).
    void onDeclare(const Symbol& sym) override {
        if (sym.decl && sym.decl->kind() == ParserToken::Kind::TypeDecl) {
            m_aliasType = sym.type;
        }
    }

    bool refResolved() const { return m_refResolved; }
    TypeId refType() const { return m_refType; }
    int refObservations() const { return m_refObservations; }
    TypeId aliasType() const { return m_aliasType; }

  private:
    static constexpr std::string_view kRefName = "x";
    AnalysisContext& m_actx;
    bool m_refResolved = false;
    TypeId m_refType = INVALID_TYPE_ID;
    int m_refObservations = 0;
    TypeId m_aliasType = INVALID_TYPE_ID;
};

// -- x:Int32 := 5;  MyInt ::= :Int32;  w := x; --
// Скоупы (модульный) активны ТОЛЬКО во время обхода; хук проверяет резолв в этот момент.
TEST(ScopeLiveness, ModuleLevelNamesResolveDuringTraversal) {
    Context ctx;
    TypeRegistry types(ctx.diag(), ctx.opts());
    ctx.setTypes(&types);
    AnalysisContext actx(ctx);
    NameResolutionPass core(actx);
    auto probe = std::make_unique<NameProbeHook>(actx);
    NameProbeHook* probe_ptr = probe.get();
    core.addHook(std::move(probe));

    // Тело модуля (скоуп-контейнер открывает вложенный скоуп на время обхода).
    auto body = std::make_shared<ScopeBlock>(std::string(""));
    // x:Int32 := 5;
    body->m_body.push_back(std::make_shared<VarDecl>("x", std::make_shared<IdentType>("Int32"), std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "5")));
    // MyInt ::= :Int32; - объявление ТИПА (валидно; '::=' создаёт только типы). onDeclare захватит его TypeId.
    body->m_body.push_back(std::make_shared<Binary>(ParserToken::Kind::TypeDecl, std::make_shared<IdentName>("MyInt"), std::make_shared<IdentType>("Int32")));
    // w := x; - ссылка на переменную x (Ident) → onResolve.
    body->m_body.push_back(std::make_shared<VarDecl>("w", nullptr, std::make_shared<IdentName>("x")));

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(body));

    core.run(seq);
    core.finalize();

    EXPECT_EQ(ctx.diag().errorCount(), 0);

    // Во время обхода (скоуп модуля активен) ссылка на x разрешилась корректно.
    EXPECT_GE(probe_ptr->refObservations(), 1);
    EXPECT_TRUE(probe_ptr->refResolved());
    EXPECT_EQ(probe_ptr->refType(), ctx.types().getType("Int32"));

    // MyInt ::= :Int32: семантика в момент активного скоупа зарегистрировала тип-алиас MyInt.
    TypeId alias = probe_ptr->aliasType();
    ASSERT_NE(alias, INVALID_TYPE_ID);
    EXPECT_EQ(ctx.types().getCanonicalTypeId(alias), ctx.types().getType("Int32"));

    // Контраст: ПОСЛЕ обхода модульный скоуп вытолкнут - x уровня модуля в SymbolTable недоступен
    // (поэтому транспайлер, читающий таблицу после анализа, использует реестр).
    EXPECT_EQ(actx.symbols().resolve("x"), nullptr);
}

// Оператор '::=' создаёт ТОЛЬКО типы: ссылка на переменную справа - ошибка, а не «алиас на переменную».
TEST(ScopeLiveness, AliasOnVariableIsError) {
    Context ctx;
    TypeRegistry types(ctx.diag(), ctx.opts());
    ctx.setTypes(&types);
    AnalysisContext actx(ctx);
    NameResolutionPass core(actx);

    // x:Int32 := 5;  y ::= x; - правая часть - переменная (Ident) → ошибка.
    auto body = std::make_shared<ScopeBlock>(std::string(""));
    body->m_body.push_back(std::make_shared<VarDecl>("x", std::make_shared<IdentType>("Int32"), std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "5")));
    body->m_body.push_back(std::make_shared<Binary>(ParserToken::Kind::TypeDecl, std::make_shared<IdentName>("y"), std::make_shared<IdentName>("x")));

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(body));

    core.run(seq);
    core.finalize();

    EXPECT_GT(ctx.diag().errorCount(), 0);
    // y НЕ зарегистрирован как тип.
    EXPECT_FALSE(ctx.types().findType("y").has_value());
}

} // namespace
} // namespace trust
