#include "target/runtime_symbols.hpp"
#include "semantic/semantic_test_fixture.hpp"
#include "ir/hir_module.hpp"
#include "semantic/ref/definite_assignment.hpp"
#include "semantic/diag.hpp"
#include "../types/type_flag_test_utils.hpp"

namespace trust {
TEST_F(SemanticTest, VarDeclSimple) {
    // x := 42
    const std::string name = "x";
    auto term = Term::Create(TermID::NAME, name, {}, parser::token_type::NAME);
    auto var = std::make_shared<VarDecl>(std::move(term), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "42"));

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(var));

    SemanticPassRunner runner(m_ctx, *m_types);
    bool ok = runner.run(seq);

    EXPECT_TRUE(ok);
    EXPECT_EQ(runner.analysis().symbols().globalSize(), 1);
    auto* sym = runner.analysis().symbols().resolve("x");
    ASSERT_NE(sym, nullptr);
    EXPECT_EQ(sym->name, "x");
    ASSERT_NE(sym->decl, nullptr);
    EXPECT_EQ(sym->decl->kind(), ParserToken::Kind::VarDecl);
    EXPECT_TRUE(static_cast<const VarDecl&>(*sym->decl).m_initializer != nullptr);
}

// Разрешённый тип аннотации — материализованный факт анализа: семантика записывает его в узел
// (`IdentName::resolvedTypeId`), клон HIR наследует поле, и кодоген рендерит тип БЕЗ таблицы
// символов/повторного резолва имени.
TEST_F(SemanticTest, AnnotationTypeMaterializedForBackend) {
    auto term = Term::Create(TermID::NAME, "x", {}, parser::token_type::NAME);
    auto typeNode = std::make_shared<IdentType>("Int32");
    IdentType* typePtr = typeNode.get();
    auto var = std::make_shared<VarDecl>(std::move(term), std::move(typeNode), std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "5"));

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(var));

    SemanticPassRunner runner(m_ctx, *m_types);
    ASSERT_TRUE(runner.run(seq));
    ASSERT_TRUE(typePtr->resolvedTypeId.isValid()) << "semantic analysis must materialize the annotation type on the node";
    const auto int32 = m_ctx.readTypes().findType("Int32");
    ASSERT_TRUE(int32.has_value());
    EXPECT_EQ(typePtr->resolvedTypeId, *int32);

    HirModule hir = buildHir(seq, m_ctx, *m_types);
    ASSERT_EQ(hir.size(), 1u);
    const auto& hirVar = static_cast<const VarDecl&>(*hir.nodes()[0]);
    ASSERT_NE(hirVar.m_type, nullptr);
    const auto* hirType = hirVar.m_type->as<IdentType>();
    ASSERT_NE(hirType, nullptr);
    EXPECT_EQ(hirType->resolvedTypeId, *int32) << "HIR clone must inherit the materialized fact";
}

// Имя объявления типа и база алиаса — материализованные тип-имена (`IdentName::resolvedTypeId`):
// кодоген не резолвит их в реестре.
TEST_F(SemanticTest, TypeNameIdentMaterializedForBackend) {
    auto declName = std::make_shared<IdentName>("MyInt");
    auto base = std::make_shared<IdentType>("Int32");
    auto decl = std::make_shared<Binary>(ParserToken::Kind::TypeDecl, declName, base);

    auto aliasBase = std::make_shared<IdentName>("MyInt");
    auto aliasName = std::make_shared<IdentName>("Big");
    auto alias = std::make_shared<Binary>(ParserToken::Kind::TypeDecl, aliasName, aliasBase);

    std::vector<AstNodePtr> seq;
    seq.push_back(decl);
    seq.push_back(alias);

    SemanticPassRunner runner(m_ctx, *m_types);
    ASSERT_TRUE(runner.run(seq));
    const auto myInt = m_ctx.readTypes().findType("MyInt");
    ASSERT_TRUE(myInt.has_value());
    ASSERT_TRUE(declName->isTypeName());
    EXPECT_EQ(declName->resolvedTypeId, *myInt);
    ASSERT_TRUE(base->resolvedTypeId.isValid()) << "annotation type must be materialized";
    ASSERT_TRUE(aliasBase->isTypeName()) << "alias base denotes a type";
    EXPECT_EQ(aliasBase->resolvedTypeId, *myInt);
    const auto big = m_ctx.readTypes().findType("Big");
    ASSERT_TRUE(big.has_value());
    ASSERT_TRUE(aliasName->isTypeName());
    EXPECT_EQ(aliasName->resolvedTypeId, *big);
}

// Кэш типов выражений хранит ПОЛНОЕ type-state (identity + inferred), а `exprType` отдаёт
// identity-проекцию: derived-квалификаторы не теряются в кэше (аналог QualType в Expr у Clang).
TEST_F(SemanticTest, ExprStateKeepsInferredQualifier) {
    auto term = Term::Create(TermID::NAME, "x", {}, parser::token_type::NAME);
    auto init = std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "42");
    AstNodeBase* initPtr = init.get();
    auto var = std::make_shared<VarDecl>(std::move(term), nullptr, std::move(init));

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(var));

    SemanticPassRunner runner(m_ctx, *m_types);
    ASSERT_TRUE(runner.run(seq));

    const QualType st = runner.analysis().facts().exprState(*initPtr);
    EXPECT_TRUE(st.isInferred());
    EXPECT_NE(st.getTypeId(), INVALID_TYPE_ID);
    // exprType отдаёт IDENTITY (state-биты снимаются); полное состояние - exprState.
    EXPECT_EQ(runner.analysis().facts().exprType(*initPtr), st.getTypeId());
    // identity-проекция - через fromTypeId.
    EXPECT_EQ(QualType::fromTypeId(runner.analysis().facts().exprType(*initPtr)).getTypeId(), st.getTypeId());
}

// @[reftype("shared")] на аннотации ТИПА (`x : @[reftype("shared")@] Int32`) устанавливает вид
// ссылки (RefType) на тип переменной - fast-path бит (первая ссылка на тип без признака).
// Нативные виды (ptr/ref) глобально теперь запрещены (D4) - здесь проверяется сам механизм атрибута.
TEST_F(SemanticTest, VarDeclReftypeSetsRefType) {
    MapperFile input_file = m_ctx.source().add_source("reftype.src", "x:Int32 := 42;", true);
    MapperRange nameRange(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 2));
    auto nameTerm = Term::Create(TermID::NAME, "x", nameRange, parser::token_type::NAME);
    // Вид ссылки - часть ТИПА: @[reftype("shared")] вешаем на аннотацию типа (x : @[reftype] Int32).
    auto typeNode = std::make_shared<IdentType>("Int32");
    auto rid = m_ctx.attrs().lookup(attr::Reftype);
    ASSERT_TRUE(rid.has_value());
    typeNode->add_attr(*rid);
    typeNode->set_attr_args(*rid, {"shared"});
    auto var = std::make_shared<VarDecl>(std::move(nameTerm), std::move(typeNode), std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "42"));

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(var));

    SemanticPassRunner runner(m_ctx, *m_types);
    ASSERT_TRUE(runner.run(seq));

    auto* sym = runner.analysis().symbols().resolve("x");
    ASSERT_NE(sym, nullptr);
    ASSERT_NE(sym->type.getTypeId(), INVALID_TYPE_ID);
    EXPECT_EQ(typeRefOf(sym->type.getTypeId()), RefType::kShared);
}

// Проверка утверждений: runtime-символы (trust::trust__abort__ / trust::formatMessage)
// зарегистрированы в TypeSystem - это база для распознавания их вызовов
// (%trust::trust__abort__ и т.п.) как известных нативных функций (не «undefined name»).
TEST_F(SemanticTest, RuntimeSymbolsRegistered) {
    std::set<std::string> names;
    for (const auto& rs : runtimeSymbols()) {
        names.insert(rs.symbol);
    }
    EXPECT_NE(names.find("trust::trust__abort__"), names.end());
    EXPECT_NE(names.find("trust::formatMessage"), names.end());
}

// Диапазон `start..stop`: элементный тип - join типов операндов (Int+Int → Int64); тип
// выражения/переменной - ПАРАМЕТРИЗОВАННЫЙ структурный `Range<Int64>` (не абстрактный `:Range`).
TEST_F(SemanticTest, RangeExprElementTypeJoin) {
    auto rng = std::make_shared<RangeExpr>(ParserToken::Kind::RangeExpr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "1"),
                                           std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "10"));
    auto nameTerm = Term::Create(TermID::NAME, "r", {}, parser::token_type::NAME);
    auto var = std::make_shared<VarDecl>(std::move(nameTerm), nullptr, rng);
    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(var));

    SemanticPassRunner runner(m_ctx, *m_types);
    ASSERT_TRUE(runner.run(seq));

    // Элементный тип диапазона - Int64 (join двух Int-литералов).
    EXPECT_EQ(rng->elementType.getTypeId(), m_ctx.readTypes().getType(type::Int64));
    // Тип переменной - структурный Range<Int64> (не абстрактный :Range).
    auto* sym = runner.analysis().symbols().resolve("r");
    ASSERT_NE(sym, nullptr);
    const TypeId expected = m_types->interner().getOrCreateRangeType(m_ctx.readTypes().getType(type::Int64));
    EXPECT_EQ(m_ctx.readTypes().store().desugaredTypeId(sym->type.getTypeId()), expected);
    EXPECT_TRUE(m_ctx.readTypes().store().isRangeType(expected));
    EXPECT_EQ(m_ctx.readTypes().store().rangeElementType(expected), m_ctx.readTypes().getType(type::Int64));
}

TEST_F(SemanticTest, DupDeclError) {
    // x := 1; x := 2;
    const std::string name1 = "x";
    auto t1 = Term::Create(TermID::NAME, name1, {}, parser::token_type::NAME);
    auto a1 = std::make_shared<VarDecl>(std::move(t1), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "1"));

    const std::string name2 = "x";
    auto t2 = Term::Create(TermID::NAME, name2, {}, parser::token_type::NAME);
    auto a2 = std::make_shared<VarDecl>(std::move(t2), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "2"));

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(a1));
    seq.push_back(std::move(a2));

    SemanticPassRunner runner(m_ctx, *m_types);
    bool ok = runner.run(seq);

    EXPECT_FALSE(ok);
    EXPECT_GT(m_ctx.diag().errorCount(), 0);
}

TEST_F(SemanticTest, UndefinedNameRef) {
    // y := z; - error: undefined name
    const std::string name = "y";
    auto term = Term::Create(TermID::NAME, name, {}, parser::token_type::NAME);

    const std::string refName = "z";
    auto refTerm = Term::Create(TermID::NAME, refName, {}, parser::token_type::NAME);
    auto ref = std::make_shared<IdentName>(std::move(refTerm));

    auto var = std::make_shared<VarDecl>(std::move(term), nullptr, std::move(ref));

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(var));

    SemanticPassRunner runner(m_ctx, *m_types);
    bool ok = runner.run(seq);

    EXPECT_FALSE(ok);
    EXPECT_GT(m_ctx.diag().errorCount(), 0);
}

TEST_F(SemanticTest, UseAfterDecl) {
    // a := 1; b := a;
    const std::string nameA = "a";
    auto tA = Term::Create(TermID::NAME, nameA, {}, parser::token_type::NAME);
    auto a1 = std::make_shared<VarDecl>(std::move(tA), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "1"));

    const std::string nameB = "b";
    auto tB = Term::Create(TermID::NAME, nameB, {}, parser::token_type::NAME);
    auto a2 = std::make_shared<VarDecl>(std::move(tB), nullptr, std::make_shared<IdentName>("a"));

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(a1));
    seq.push_back(std::move(a2));

    SemanticPassRunner runner(m_ctx, *m_types);
    bool ok = runner.run(seq);

    EXPECT_TRUE(ok);
    EXPECT_EQ(runner.analysis().symbols().globalSize(), 2);
}

TEST_F(SemanticTest, LiteralStandalone) {
    auto litTerm = Term::Create(TermID::INTEGER, "42", {}, parser::token_type::END);

    std::vector<AstNodePtr> seq;
    seq.push_back(std::make_shared<Literal>(ParserToken::Kind::IntLiteral, std::move(litTerm)));

    SemanticPassRunner runner(m_ctx, *m_types);
    bool ok = runner.run(seq);

    EXPECT_TRUE(ok);
}

// -- SymbolTable tests ------------------------------------
// Единая таблица символов: стек вложенных скоупов. Создаётся без DiagnosticEngine -
// диагностику дубликатов формирует ядро (ему нужен range).

// Helper: Color ::= :Enum(RED=1, GREEN=2,) - TypeDecl(left=Ident, right=DictLiteral с аннотацией «Enum»).
static std::shared_ptr<Binary> makeEnumTypeDecl(const char* name, std::initializer_list<std::pair<const char*, const char*>> ms) {
    auto dict = std::make_shared<DictLiteralNode>(ParserToken::Kind::DictLiteral);
    dict->m_type = std::make_shared<IdentType>(std::string("Enum"));
    for (const auto& [mname, mval] : ms) {
        dict->m_body.push_back(
            std::make_shared<ArgNode>(std::string(mname), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, std::string(mval))));
    }
    return std::make_shared<Binary>(ParserToken::Kind::TypeDecl, std::make_shared<IdentName>(std::string(name)), std::move(dict));
}

TEST_F(SemanticTest, EnumDeclRegistersType) {
    // Color ::= :Enum(RED=1, GREEN=2,) → TypeDecl+DictLiteral(аннотация Enum).
    auto enumDecl = makeEnumTypeDecl("Color", {{"RED", "1"}, {"GREEN", "2"}});

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(enumDecl));

    SemanticPassRunner runner(m_ctx, *m_types);
    bool ok = runner.run(seq);
    EXPECT_TRUE(ok);
    EXPECT_EQ(m_ctx.diag().errorCount(), 0);

    auto tid = m_types->read().findType("Color");
    ASSERT_TRUE(tid.has_value());
    EXPECT_TRUE(isEnumType(*tid, *m_types));
    const auto* ed = m_types->read().store().getTypeDataAs<EnumTypeData>(*tid);
    ASSERT_NE(ed, nullptr);
    EXPECT_EQ(ed->members.size(), 2u);
    EXPECT_EQ(ed->members[0].name, "RED");
    EXPECT_EQ(ed->members[1].name, "GREEN");
    // Тип значений по СТАНДАРТНЫМ правилам: 1→Int8, 2→Int8 → valueType Int8.
    EXPECT_EQ(m_types->read().store().desugaredTypeId(ed->valueType), m_types->read().getType(type::Int8));
    // Классические методы зарегистрированы (резолвятся семантикой): count/fromName/fromValue.
    const auto hasMethod = [&](std::string_view n) {
        const auto m = m_types->read().methods().findMethodInfo(*tid, n);
        return m.has_value() && !m->entries.empty();
    };
    EXPECT_TRUE(hasMethod("count"));
    EXPECT_TRUE(hasMethod("fromName"));
    EXPECT_TRUE(hasMethod("fromValue"));
}

TEST_F(SemanticTest, EnumMemberAccessResolvesToEnumType) {
    // Color.RED - значение типа Color (работа только через имя типа); несуществующий член → ошибка.
    auto enumDecl = makeEnumTypeDecl("Color", {{"RED", "1"}, {"GREEN", "2"}});

    auto memberRef = [](const std::string& mname) {
        auto left = std::make_shared<IdentName>(std::string("Color"));
        auto right = std::make_shared<IdentName>(mname);
        return std::make_shared<Binary>(ParserToken::Kind::MemberAccess, std::move(left), std::move(right));
    };

    // 1) b : Color := Color.RED - допустимо, ошибок нет.
    {
        std::vector<AstNodePtr> seq;
        seq.push_back(std::make_shared<Binary>(*enumDecl));
        auto tname = std::make_shared<IdentType>(std::string("Color"));
        seq.push_back(std::make_shared<VarDecl>("b", std::move(tname), memberRef("RED")));
        m_ctx.diag().clear();
        SemanticPassRunner runner(m_ctx, *m_types);
        EXPECT_TRUE(runner.run(seq));
        EXPECT_EQ(m_ctx.diag().errorCount(), 0);
    }
    // 2) b : Color := Color.BLUE - несуществующий член → ошибка «enum has no member».
    {
        m_ctx.diag().clear();
        std::vector<AstNodePtr> seq;
        seq.push_back(std::make_shared<Binary>(*enumDecl));
        auto tname = std::make_shared<IdentType>(std::string("Color"));
        seq.push_back(std::make_shared<VarDecl>("b", std::move(tname), memberRef("BLUE")));
        SemanticPassRunner runner(m_ctx, *m_types);
        EXPECT_FALSE(runner.run(seq));
        EXPECT_GT(m_ctx.diag().errorCount(), 0);
    }
}

TEST_F(SemanticTest, VariantDeclRegistersType) {
    // Value ::= :Variant(RED=5, GREEN='g',) → TypeDecl+DictLiteral(аннотация Variant).
    // Каждый член имеет СВОЙ тип (Int8 и StrChar) - гетерогенный вариант.
    auto dict = std::make_shared<DictLiteralNode>(ParserToken::Kind::DictLiteral);
    dict->m_type = std::make_shared<IdentType>(std::string("Variant"));
    dict->m_body.push_back(std::make_shared<ArgNode>(std::string("RED"), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "5")));
    dict->m_body.push_back(std::make_shared<ArgNode>(std::string("GREEN"), nullptr, std::make_shared<Literal>(ParserToken::Kind::StrChar, "g")));
    auto variantDecl = std::make_shared<Binary>(ParserToken::Kind::TypeDecl, std::make_shared<IdentName>(std::string("Value")), std::move(dict));

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(variantDecl));

    SemanticPassRunner runner(m_ctx, *m_types);
    EXPECT_TRUE(runner.run(seq));
    EXPECT_EQ(m_ctx.diag().errorCount(), 0);

    auto tid = m_types->read().findType("Value");
    ASSERT_TRUE(tid.has_value());
    EXPECT_TRUE(isVariantType(*tid, *m_types));
    const auto* vd = m_types->read().store().getTypeDataAs<VariantTypeData>(*tid);
    ASSERT_NE(vd, nullptr);
    ASSERT_EQ(vd->members.size(), 2u);
    EXPECT_EQ(vd->members[0].name, "RED");
    EXPECT_EQ(m_types->read().store().desugaredTypeId(vd->members[0].type), m_types->read().getType(type::Int8));
    EXPECT_EQ(vd->members[1].name, "GREEN");
    EXPECT_EQ(m_types->read().store().desugaredTypeId(vd->members[1].type), m_types->read().getType(type::StrChar));
}

TEST_F(SemanticTest, VariantUnknownMemberTypeReportsError) {
    // Value ::= :Variant(OK=5, BAD:NonExistentType,) - явный тип члена не резолвится → ОШИБКА
    // «unknown member type» (симметрично enum), а не тихий fallback на тип из значения/ординал.
    auto dict = std::make_shared<DictLiteralNode>(ParserToken::Kind::DictLiteral);
    dict->m_type = std::make_shared<IdentType>(std::string("Variant"));
    dict->m_body.push_back(std::make_shared<ArgNode>(std::string("OK"), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "5")));
    // Явный тип `BAD:NonExistentType` - тип в ArgNode.m_type (без обёртки значения).
    dict->m_body.push_back(std::make_shared<ArgNode>(std::string("BAD"), std::make_shared<IdentType>(std::string("NonExistentType")), nullptr));
    auto variantDecl = std::make_shared<Binary>(ParserToken::Kind::TypeDecl, std::make_shared<IdentName>(std::string("Value")), std::move(dict));

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(variantDecl));

    SemanticPassRunner runner(m_ctx, *m_types);
    EXPECT_FALSE(runner.run(seq));
    EXPECT_GT(m_ctx.diag().errorCount(), 0);
}

// Helper: RecordDecl (Struct/Class) - TypeDecl(left=Ident, right=RecordDecl с базой и полями).
// Инициализатор поля: Default (литерал 0), None (образ `_` - нет значения), Forward (`...`).
enum class FieldInit { Default, None, Forward };
static AstNodePtr makeFieldInit(FieldInit init) {
    switch (init) {
    case FieldInit::Default:
        return std::make_shared<Literal>(ParserToken::Kind::IntLiteral, std::string("0"));
    case FieldInit::None: {
        auto t = Term::Create(TermID::NONE, "_", {}, parser::token_type::END);
        return std::make_shared<IdentName>(ParserToken::Kind::Ident, std::move(t), nullptr);
    }
    case FieldInit::Forward:
        return nullptr;
    }
    return nullptr;
}
static std::shared_ptr<Binary> makeRecordTypeDecl(const char* name, const char* base, std::initializer_list<std::pair<const char*, const char*>> fields,
                                                  FieldInit init = FieldInit::Default) {
    auto rec = std::make_shared<RecordDecl>(std::string(name));
    rec->m_baseTypes.push_back(TypeBase{std::make_shared<IdentType>(std::string(base)), Visibility::Public});
    std::vector<AstNodePtr> members;
    for (const auto& [fname, ftype] : fields) {
        members.push_back(std::make_shared<VarDecl>(std::string(fname), std::make_shared<IdentType>(std::string(ftype)), makeFieldInit(init)));
    }
    rec->m_definition = Definition::fromBody(std::move(members));
    return std::make_shared<Binary>(ParserToken::Kind::TypeDecl, std::make_shared<IdentName>(std::string(name)), std::move(rec));
}

TEST_F(SemanticTest, StructDeclRegistersPodRecord) {
    // Point ::= :Struct{ x:Int32 := _; y:Int32 := _; } → Struct (Group::kStructs); поля БЕЗ значений.
    auto d = makeRecordTypeDecl("Point", "Struct", {{"x", "Int32"}, {"y", "Int32"}}, FieldInit::None);

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(d));

    SemanticPassRunner runner(m_ctx, *m_types);
    EXPECT_TRUE(runner.run(seq));
    EXPECT_EQ(m_ctx.diag().errorCount(), 0);

    auto tid = m_types->read().findType("Point");
    ASSERT_TRUE(tid.has_value());
    EXPECT_TRUE(m_types->read().store().isStructType(*tid));
    EXPECT_FALSE(m_types->read().store().isClassType(*tid));
    const auto* fields = m_types->read().store().fieldsOf(*tid);
    ASSERT_NE(fields, nullptr);
    ASSERT_EQ(fields->size(), 2u);
    EXPECT_EQ((*fields)[0].name, "x");
    EXPECT_EQ(m_types->read().store().desugaredTypeId((*fields)[0].type.getTypeId()), m_types->read().getType(type::Int32));
    EXPECT_TRUE(m_types->read().store().basesOf(*tid).empty());
}

TEST_F(SemanticTest, StructFieldWithDefaultIsError) {
    // Struct - POD: default-значение поля запрещено (NSDMI ломает тривиальность) - анализатор ошибка.
    auto d = makeRecordTypeDecl("Point", "Struct", {{"x", "Int32"}}, FieldInit::Default);
    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(d));
    SemanticPassRunner runner(m_ctx, *m_types);
    EXPECT_FALSE(runner.run(seq));
    EXPECT_GT(m_ctx.diag().errorCount(), 0);
}

TEST_F(SemanticTest, ClassDeclRegistersAndInherits) {
    // Animal ::= :Class{name}; Dog ::= :Animal{age} → C++-наследование; поле базы видно через цепочку.
    std::vector<AstNodePtr> seq;
    seq.push_back(makeRecordTypeDecl("Animal", "Class", {{"name", "Int32"}}));
    seq.push_back(makeRecordTypeDecl("Dog", "Animal", {{"age", "Int32"}}));

    SemanticPassRunner runner(m_ctx, *m_types);
    EXPECT_TRUE(runner.run(seq));
    EXPECT_EQ(m_ctx.diag().errorCount(), 0);

    auto animal = m_types->read().findType("Animal");
    auto dog = m_types->read().findType("Dog");
    ASSERT_TRUE(animal.has_value());
    ASSERT_TRUE(dog.has_value());
    EXPECT_TRUE(m_types->read().store().isClassType(*dog));
    EXPECT_FALSE(m_types->read().store().isStructType(*dog));
    ASSERT_EQ(m_types->read().store().basesOf(*dog).size(), 1u);
    EXPECT_EQ(m_types->read().store().desugaredTypeId(m_types->read().store().basesOf(*dog)[0].type), m_types->read().store().desugaredTypeId(*animal));
    EXPECT_TRUE(m_types->read().store().findField(*dog, "name").isValid()); // унаследованное поле
    EXPECT_TRUE(m_types->read().store().findField(*dog, "age").isValid());
    EXPECT_FALSE(m_types->read().store().findField(*dog, "missing").isValid());
}

TEST_F(SemanticTest, ClassFieldMayBeUninitialized) {
    // Class: значение поля НЕ проверяется - допустимо и `:= 0` (default), и `:= _` (без значения).
    std::vector<AstNodePtr> seq;
    seq.push_back(makeRecordTypeDecl("A", "Class", {{"x", "Int32"}}, FieldInit::None));
    seq.push_back(makeRecordTypeDecl("B", "Class", {{"y", "Int32"}}, FieldInit::Default));

    SemanticPassRunner runner(m_ctx, *m_types);
    EXPECT_TRUE(runner.run(seq));
    EXPECT_EQ(m_ctx.diag().errorCount(), 0);
}

TEST_F(SemanticTest, StructCannotInherit) {
    // Struct - строго POD: наследование от Struct запрещено (ошибка, не тихий fallback).
    std::vector<AstNodePtr> seq;
    seq.push_back(makeRecordTypeDecl("Base", "Struct", {{"a", "Int32"}}, FieldInit::None));
    seq.push_back(makeRecordTypeDecl("Derived", "Base", {{"b", "Int32"}}, FieldInit::None));

    SemanticPassRunner runner(m_ctx, *m_types);
    EXPECT_FALSE(runner.run(seq));
    EXPECT_GT(m_ctx.diag().errorCount(), 0);
}

TEST_F(SemanticTest, EnumTypedAndBareMembersViaArgNode) {
    // Flag ::= (LOW:Int8, HIGH,):Enum - явный тип члена (ArgNode.m_type) + безнарный член
    // (имя в value-Ident). Проверяет каноническое чтение членов из ArgNode (enumVariantMember).
    auto dict = std::make_shared<DictLiteralNode>(ParserToken::Kind::DictLiteral);
    dict->m_type = std::make_shared<IdentType>(std::string("Enum"));
    // LOW:Int8 - безнарный типизированный член: имя в value-Ident, тип в m_type.
    dict->m_body.push_back(
        std::make_shared<ArgNode>(std::string(""), std::make_shared<IdentType>(std::string("Int8")), std::make_shared<IdentName>(std::string("LOW"))));
    // HIGH - безнарный член: имя в value-Ident, без типа/значения.
    dict->m_body.push_back(std::make_shared<ArgNode>(std::string(""), nullptr, std::make_shared<IdentName>(std::string("HIGH"))));
    auto enumDecl = std::make_shared<Binary>(ParserToken::Kind::TypeDecl, std::make_shared<IdentName>(std::string("Flag")), std::move(dict));

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(enumDecl));

    SemanticPassRunner runner(m_ctx, *m_types);
    EXPECT_TRUE(runner.run(seq));
    EXPECT_EQ(m_ctx.diag().errorCount(), 0);

    auto tid = m_types->read().findType("Flag");
    ASSERT_TRUE(tid.has_value());
    EXPECT_TRUE(isEnumType(*tid, *m_types));
    const auto* ed = m_types->read().store().getTypeDataAs<EnumTypeData>(*tid);
    ASSERT_NE(ed, nullptr);
    ASSERT_EQ(ed->members.size(), 2u);
    EXPECT_EQ(ed->members[0].name, "LOW");
    EXPECT_EQ(ed->members[1].name, "HIGH");
    // Явный тип члена LOW:Int8 задаёт единый тип значений.
    EXPECT_EQ(m_types->read().store().desugaredTypeId(ed->valueType), m_types->read().getType(type::Int8));
}

TEST_F(SemanticTest, ForwardVarDecl) {
    auto var = std::make_shared<VarDecl>("x", std::make_shared<IdentType>("Int32"), nullptr);

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(var));

    SemanticPassRunner runner(m_ctx, *m_types);
    EXPECT_TRUE(runner.run(seq));
    auto* sym = runner.analysis().symbols().resolve("x");
    ASSERT_NE(sym, nullptr);
    EXPECT_NE(sym->type.getTypeId(), INVALID_TYPE_ID);
    ASSERT_NE(sym->decl, nullptr);
    EXPECT_EQ(sym->decl->kind(), ParserToken::Kind::VarDecl);
    EXPECT_FALSE(static_cast<const VarDecl&>(*sym->decl).m_initializer) << "forward-переменная без инициализатора";
}

// Forward-объявление ненативной переменной без типа `y := ...;` - тип опционален, ошибки нет.
TEST_F(SemanticTest, ForwardVarDeclNoType) {
    auto var = std::make_shared<VarDecl>("y", nullptr, nullptr);

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(var));

    SemanticPassRunner runner(m_ctx, *m_types);
    EXPECT_TRUE(runner.run(seq));
    auto* sym = runner.analysis().symbols().resolve("y");
    ASSERT_NE(sym, nullptr);
}

// Нативный импорт переменной без типа → ошибка (тип задаёт C++-представление символа).
TEST_F(SemanticTest, NativeImportVarNoTypeError) {
    auto var = std::make_shared<VarDecl>("x", nullptr, nullptr);
    var->m_bindsNativeCpp = true;

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(var));

    SemanticPassRunner runner(m_ctx, *m_types);
    EXPECT_FALSE(runner.run(seq));
    EXPECT_GT(m_ctx.diag().errorCount(), 0);
}

// Forward-объявление переменной завершается определением того же имени в том же скоупе
// (declareOrComplete → Completed): не ошибка, символ обновляется инициализатором.
TEST_F(SemanticTest, ForwardVarCompletedByDefinition) {
    auto fwd = std::make_shared<VarDecl>("x", std::make_shared<IdentType>("Int32"), nullptr);
    auto def = std::make_shared<VarDecl>("x", std::make_shared<IdentType>("Int32"), std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "5"));

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(fwd));
    seq.push_back(std::move(def));

    SemanticPassRunner runner(m_ctx, *m_types);
    EXPECT_TRUE(runner.run(seq));
    auto* sym = runner.analysis().symbols().resolve("x");
    ASSERT_NE(sym, nullptr);
    ASSERT_NE(sym->decl, nullptr);
    EXPECT_EQ(sym->decl->kind(), ParserToken::Kind::VarDecl);
    EXPECT_TRUE(static_cast<const VarDecl&>(*sym->decl).m_initializer != nullptr) << "определение должно заменить forward-объявление";
}

// Два определения переменной одного имени → ошибка duplicate.
TEST_F(SemanticTest, TwoVarDefinitionsError) {
    auto v1 = std::make_shared<VarDecl>("x", nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "1"));
    auto v2 = std::make_shared<VarDecl>("x", nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "2"));

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(v1));
    seq.push_back(std::move(v2));

    SemanticPassRunner runner(m_ctx, *m_types);
    EXPECT_FALSE(runner.run(seq));
    EXPECT_GT(m_ctx.diag().errorCount(), 0);
}

// Определение раньше forward-объявления того же имени → ошибка duplicate (уже определено).
TEST_F(SemanticTest, DefinitionThenForwardError) {
    auto def = std::make_shared<VarDecl>("x", nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "1"));
    auto fwd = std::make_shared<VarDecl>("x", nullptr, nullptr);

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(def));
    seq.push_back(std::move(fwd));

    SemanticPassRunner runner(m_ctx, *m_types);
    EXPECT_FALSE(runner.run(seq));
    EXPECT_GT(m_ctx.diag().errorCount(), 0);
}

TEST_F(SemanticTest, TypeAliasBoundInScopeAndResolvable) {
    // y ::= Int32;  x:y := 1; - алиас связан в скоуп-стеке и резолвится как тип переменной.
    auto opTerm = Term::Create(TermID::CREATE_TYPE, "::=", {}, parser::token_type::END);
    auto typeDecl = std::make_shared<Binary>(ParserToken::Kind::TypeDecl, std::move(opTerm));
    typeDecl->m_left = std::make_shared<IdentName>("y");
    typeDecl->m_right = std::make_shared<IdentType>("Int32");

    auto var = std::make_shared<VarDecl>("x", std::make_shared<IdentType>("y"), std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "1"));

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(typeDecl));
    seq.push_back(std::move(var));

    SemanticPassRunner runner(m_ctx, *m_types);
    ASSERT_TRUE(runner.run(seq));

    // Алиас зарегистрирован в реестре и связан в скоуп-стеке.
    auto y_id = m_ctx.readTypes().findType("y");
    ASSERT_TRUE(y_id.has_value());
    const Symbol* ys = runner.analysis().symbols().resolve("y");
    ASSERT_NE(ys, nullptr);
    EXPECT_EQ(ys->type.getTypeId(), *y_id);
    EXPECT_EQ(m_ctx.readTypes().store().desugaredTypeId(*y_id), m_ctx.readTypes().findType("Int32").value());

    // Переменная, аннотированная y, получила тип алиаса (единый резолв через скоуп).
    const Symbol* xs = runner.analysis().symbols().resolve("x");
    ASSERT_NE(xs, nullptr);
    EXPECT_EQ(xs->type.getTypeId(), *y_id);
}

TEST_F(SemanticTest, TypeAliasCollidesWithVar) {
    // y ::= Int32; y := 1; - имя типа и переменной в одном скоупе → ошибка.
    auto opTerm = Term::Create(TermID::CREATE_TYPE, "::=", {}, parser::token_type::END);
    auto typeDecl = std::make_shared<Binary>(ParserToken::Kind::TypeDecl, std::move(opTerm));
    typeDecl->m_left = std::make_shared<IdentName>("y");
    typeDecl->m_right = std::make_shared<IdentType>("Int32");

    auto var = std::make_shared<VarDecl>("y", nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "1"));

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(typeDecl));
    seq.push_back(std::move(var));

    SemanticPassRunner runner(m_ctx, *m_types);
    EXPECT_FALSE(runner.run(seq));
    EXPECT_GT(m_ctx.diag().errorCount(), 0);
}

TEST_F(SemanticTest, TypeSystemResetPerRun) {
    // y ::= Int32; - первый run регистрирует алиас в реестре.
    auto opTerm = Term::Create(TermID::CREATE_TYPE, "::=", {}, parser::token_type::END);
    auto typeDecl = std::make_shared<Binary>(ParserToken::Kind::TypeDecl, std::move(opTerm));
    typeDecl->m_left = std::make_shared<IdentName>("y");
    typeDecl->m_right = std::make_shared<IdentType>("Int32");

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(typeDecl));

    SemanticPassRunner r1(m_ctx, *m_types);
    ASSERT_TRUE(r1.run(seq));
    ASSERT_TRUE(m_ctx.readTypes().findType("y").has_value());

    // Второй run сбрасывает реестр к builtin-состоянию (упорядоченный жизненный цикл).
    SemanticPassRunner r2(m_ctx, *m_types);
    std::vector<AstNodePtr> empty;
    ASSERT_TRUE(r2.run(empty));
    EXPECT_FALSE(m_ctx.readTypes().findType("y").has_value());
}

TEST_F(SemanticTest, LoweringWrapsSemicolonStmt) {
    // x += 3 → в HIR (клон AST + lowering) обёрнут в SemicolonStmt: решение о ';' для
    // statement-выражений зашивается при построении HIR, а не выводится транспилятором.
    // Исходная AST при этом остаётся неизменённой (lowering не мутирует вход анализа).
    MapperFile input_file = m_ctx.source().add_source("low.src", "x := 0; x += 3;", true);
    MapperRange range(m_ctx.source().makeLoc(input_file, 9), m_ctx.source().makeLoc(input_file, 16));

    auto opTerm = Term::Create(TermID::ASSIGN, "+=", range, parser::token_type::END);
    auto b = std::make_shared<Binary>(ParserToken::Kind::AssignOp, std::move(opTerm));
    b->m_left = std::make_shared<IdentName>("x");
    b->m_right = std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "3");

    auto xTerm =
        Term::Create(TermID::NAME, "x", MapperRange(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 8)), parser::token_type::NAME);
    auto xVar = std::make_shared<VarDecl>(std::move(xTerm), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "0"));
    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(xVar));
    seq.push_back(std::move(b));

    SemanticPassRunner runner(m_ctx, *m_types);
    ASSERT_TRUE(runner.run(seq));

    // Lowering выполняется при построении HIR (клон AST + lowering): сама AST остаётся неизменённой,
    // производная форма строится отдельно.
    HirModule hir = buildHir(seq, m_ctx, *m_types);
    ASSERT_EQ(hir.size(), 2u);
    const auto& out = hir.nodes();
    ASSERT_EQ(out[0]->kind(), ParserToken::Kind::VarDecl);
    ASSERT_EQ(out[1]->kind(), ParserToken::Kind::SemicolonStmt);
    // Исходная AST не содержит синтетики: lowering в неё не пишет.
    EXPECT_EQ(seq[1]->kind(), ParserToken::Kind::AssignOp);
    auto* es = static_cast<SemicolonStmt*>(out[1].get());
    ASSERT_NE(es->m_expr, nullptr);
    EXPECT_EQ(es->m_expr->kind(), ParserToken::Kind::AssignOp);
}
// Именованные элементы словаря `(1, two=2, name='3',)` - метки полей, НЕ переменные:
// их имена не резолвятся как ссылки (нет «undefined name») и НЕ регистрируются
// в таблице символов (регистрация имён - только для аргументов функций).
TEST_F(SemanticTest, DictLiteralNamedElementsNotRegistered) {
    auto dictTerm = Term::Create(TermID::DICT, "", {}, parser::token_type::END);
    auto dict = std::make_shared<DictLiteralNode>(ParserToken::Kind::DictLiteral, std::move(dictTerm));
    dict->m_body.push_back(std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "1"));
    dict->m_body.push_back(std::make_shared<ArgNode>(std::string("two"), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "2")));
    dict->m_body.push_back(std::make_shared<ArgNode>(std::string("name"), nullptr, std::make_shared<Literal>(ParserToken::Kind::StrChar, "3")));

    auto nameTerm = Term::Create(TermID::NAME, "d", {}, parser::token_type::NAME);
    auto var = std::make_shared<VarDecl>(std::move(nameTerm), nullptr, std::move(dict));
    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(var));

    SemanticPassRunner runner(m_ctx, *m_types);
    ASSERT_TRUE(runner.run(seq));
    // Никаких ошибок (в т.ч. «undefined name 'two'/'name'»).
    EXPECT_EQ(m_ctx.diag().errorCount(), 0);
    // Имена элементов НЕ объявлены в таблице символов.
    EXPECT_EQ(runner.analysis().symbols().resolve("two"), nullptr);
    EXPECT_EQ(runner.analysis().symbols().resolve("name"), nullptr);
}

TEST_F(SemanticTest, DictFieldTypeInference) {
    // d := (1, two=2, name='3',);  x := d.two;  y := d[0];
    // Вывод типа поля: d.two → Int8, d[0] → Int8 (из Dims литерала).
    auto dictTerm = Term::Create(TermID::DICT, "", {}, parser::token_type::END);
    auto dict = std::make_shared<DictLiteralNode>(ParserToken::Kind::DictLiteral, std::move(dictTerm));
    dict->m_body.push_back(std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "1"));
    dict->m_body.push_back(std::make_shared<ArgNode>(std::string("two"), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "2")));
    dict->m_body.push_back(std::make_shared<ArgNode>(std::string("name"), nullptr, std::make_shared<Literal>(ParserToken::Kind::StrChar, "3")));

    auto dNameTerm = Term::Create(TermID::NAME, "d", {}, parser::token_type::NAME);
    auto dVar = std::make_shared<VarDecl>(std::move(dNameTerm), nullptr, std::move(dict));

    // x := d.two;  (MemberAccess по имени)
    auto accName = std::make_shared<Binary>(ParserToken::Kind::MemberAccess);
    accName->m_left = std::make_shared<IdentName>("d");
    accName->m_right = std::make_shared<IdentName>("two");
    auto xNameTerm = Term::Create(TermID::NAME, "x", {}, parser::token_type::NAME);
    auto xVar = std::make_shared<VarDecl>(std::move(xNameTerm), nullptr, std::move(accName));

    // y := d[0];  (ArrayAccess по статическому индексу)
    auto accIdx = std::make_shared<Binary>(ParserToken::Kind::ArrayAccess);
    accIdx->m_left = std::make_shared<IdentName>("d");
    accIdx->m_right = std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "0");
    auto yNameTerm = Term::Create(TermID::NAME, "y", {}, parser::token_type::NAME);
    auto yVar = std::make_shared<VarDecl>(std::move(yNameTerm), nullptr, std::move(accIdx));

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(dVar));
    seq.push_back(std::move(xVar));
    seq.push_back(std::move(yVar));

    SemanticPassRunner runner(m_ctx, *m_types);
    ASSERT_TRUE(runner.run(seq));
    EXPECT_EQ(m_ctx.diag().errorCount(), 0);

    const TypeId int8 = m_types->read().findType(type::Int8).value_or(INVALID_TYPE_ID);
    // d.two → Int8; d[0] → Int8 (элемент 0 - `1` → Int8).
    EXPECT_EQ(static_cast<VarDecl&>(*seq[1]).inferredType.getTypeId(), int8);
    EXPECT_EQ(static_cast<VarDecl&>(*seq[2]).inferredType.getTypeId(), int8);
    // На символе d: размерность и типы полей.
    const Symbol* dSym = runner.analysis().symbols().resolve("d");
    ASSERT_NE(dSym, nullptr);
    ASSERT_TRUE(dSym->dictElements.has_value());
    EXPECT_EQ(dSym->dictElements->size(), 3u);
    EXPECT_EQ((*dSym->dictElements)[1].name, "two");
    EXPECT_EQ(clearFlag((*dSym->dictElements)[1].type.getTypeId(), TypeFlag::Inferred), int8);
}

TEST_F(SemanticTest, DictAppendSpreadLiteral) {
    // d := (1, two=2,);  d []= ... (three=3, 4,);
    // Spread-merge из литерала: размер и типы полей переносятся (dims 2 → 4).
    auto dictTerm = Term::Create(TermID::DICT, "", {}, parser::token_type::END);
    auto dict = std::make_shared<DictLiteralNode>(ParserToken::Kind::DictLiteral, std::move(dictTerm));
    dict->m_body.push_back(std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "1"));
    dict->m_body.push_back(std::make_shared<ArgNode>(std::string("two"), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "2")));
    auto dNameTerm = Term::Create(TermID::NAME, "d", {}, parser::token_type::NAME);
    auto dVar = std::make_shared<VarDecl>(std::move(dNameTerm), nullptr, std::move(dict));

    // Операнд spread: литерал (three=3, 4,).
    auto srcTerm = Term::Create(TermID::DICT, "", {}, parser::token_type::END);
    auto src = std::make_shared<DictLiteralNode>(ParserToken::Kind::DictLiteral, std::move(srcTerm));
    src->m_body.push_back(std::make_shared<ArgNode>(std::string("three"), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "3")));
    src->m_body.push_back(std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "4"));

    auto ellTerm = Term::Create(TermID::ELLIPSIS, "...");
    auto ell = std::make_shared<Sequence>(ParserToken::Kind::Ellipsis, std::move(ellTerm));
    ell->m_body.push_back(std::move(src));

    auto append = std::make_shared<Binary>(ParserToken::Kind::AppendStmt);
    append->m_left = std::make_shared<IdentName>("d");
    append->m_right = std::move(ell);

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(dVar));
    seq.push_back(std::move(append));

    SemanticPassRunner runner(m_ctx, *m_types);
    ASSERT_TRUE(runner.run(seq));
    EXPECT_EQ(m_ctx.diag().errorCount(), 0);

    const TypeId int8 = m_types->read().findType(type::Int8).value_or(INVALID_TYPE_ID);
    const Symbol* dSym = runner.analysis().symbols().resolve("d");
    ASSERT_NE(dSym, nullptr);
    ASSERT_TRUE(dSym->dictElements.has_value());
    EXPECT_EQ(dSym->dictElements->size(), 4u);
    EXPECT_EQ((*dSym->dictElements)[2].name, "three");
    EXPECT_EQ(clearFlag((*dSym->dictElements)[2].type.getTypeId(), TypeFlag::Inferred), int8);
    EXPECT_EQ((*dSym->dictElements)[3].name, "");
    EXPECT_EQ(clearFlag((*dSym->dictElements)[3].type.getTypeId(), TypeFlag::Inferred), int8);
}

TEST_F(SemanticTest, DictAppendSpreadVar) {
    // d := (1,);  d2 := (a=5, b=6,);  d []= ... d2;
    // Spread-merge из переменной: dims и типы полей переносятся (1 → 3).
    auto dictTerm = Term::Create(TermID::DICT, "", {}, parser::token_type::END);
    auto dict = std::make_shared<DictLiteralNode>(ParserToken::Kind::DictLiteral, std::move(dictTerm));
    dict->m_body.push_back(std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "1"));
    auto dNameTerm = Term::Create(TermID::NAME, "d", {}, parser::token_type::NAME);
    auto dVar = std::make_shared<VarDecl>(std::move(dNameTerm), nullptr, std::move(dict));

    auto d2Term = Term::Create(TermID::DICT, "", {}, parser::token_type::END);
    auto d2 = std::make_shared<DictLiteralNode>(ParserToken::Kind::DictLiteral, std::move(d2Term));
    d2->m_body.push_back(std::make_shared<ArgNode>(std::string("a"), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "5")));
    d2->m_body.push_back(std::make_shared<ArgNode>(std::string("b"), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "6")));
    auto d2NameTerm = Term::Create(TermID::NAME, "d2", {}, parser::token_type::NAME);
    auto d2Var = std::make_shared<VarDecl>(std::move(d2NameTerm), nullptr, std::move(d2));

    auto ellTerm = Term::Create(TermID::ELLIPSIS, "...");
    auto ell = std::make_shared<Sequence>(ParserToken::Kind::Ellipsis, std::move(ellTerm));
    ell->m_body.push_back(std::make_shared<IdentName>("d2"));

    auto append = std::make_shared<Binary>(ParserToken::Kind::AppendStmt);
    append->m_left = std::make_shared<IdentName>("d");
    append->m_right = std::move(ell);

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(dVar));
    seq.push_back(std::move(d2Var));
    seq.push_back(std::move(append));

    SemanticPassRunner runner(m_ctx, *m_types);
    ASSERT_TRUE(runner.run(seq));
    EXPECT_EQ(m_ctx.diag().errorCount(), 0);

    const Symbol* dSym = runner.analysis().symbols().resolve("d");
    ASSERT_NE(dSym, nullptr);
    ASSERT_TRUE(dSym->dictElements.has_value());
    EXPECT_EQ(dSym->dictElements->size(), 3u);
    EXPECT_EQ((*dSym->dictElements)[1].name, "a");
    EXPECT_EQ((*dSym->dictElements)[2].name, "b");
}

TEST_F(SemanticTest, LoweringRewritesNamedBreakToGoto) {

    // Строим тело функции с безымянным while и именованным break на цикл (label "L").
    auto funcTerm = Term::Create(TermID::NAME, "f", {}, parser::token_type::NAME);
    auto func = std::make_shared<FuncDecl>(std::move(funcTerm));

    // Именованный break: L:: |>| (BreakStmt с label L).
    auto breakTerm = Term::Create(TermID::INT_BREAK, "L::");
    auto breakStmt = std::make_shared<JumpStmt>(ParserToken::Kind::BreakStmt, std::move(breakTerm));

    func->m_definition = Definition::fromBody({std::move(breakStmt)});

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(func));

    SemanticPassRunner runner(m_ctx, *m_types);
    ASSERT_TRUE(runner.run(seq));

    // Тело функции в HIR: [BreakStmt] → именованный break стал GotoStmt (LabelRef).
    HirModule hir = buildHir(seq, m_ctx, *m_types);
    ASSERT_EQ(hir.size(), 1u);
    auto* fd = static_cast<FuncDecl*>(hir.nodes()[0].get());
    ASSERT_TRUE(fd->hasBody());
    ASSERT_EQ((*fd->body()).size(), 1u);
    ASSERT_EQ((*fd->body())[0]->kind(), ParserToken::Kind::GotoStmt);
    auto* gs = static_cast<LabelRef*>((*fd->body())[0].get());
    EXPECT_EQ(requiredText(*gs), "L_break");
}

// -- Опциональный анализатор LintHook (управляется флагом semantic::FlagKind::Lint) --

TEST_F(SemanticTest, LintDisabledByDefault) {
    // x := 42; - lint выключен по умолчанию → диагностик unused-variable нет.
    auto t = Term::Create(TermID::NAME, "x", {}, parser::token_type::NAME);
    auto var = std::make_shared<VarDecl>(std::move(t), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "42"));
    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(var));

    SemanticPassRunner runner(m_ctx, *m_types);
    EXPECT_TRUE(runner.run(seq));
    EXPECT_EQ(m_ctx.diag().warningCount(), 0);
}

TEST_F(SemanticTest, LintUnusedVarWarning) {
    // -Wlint → включён LintHook: неиспользуемая x порождает warning (semantic::DiagId::UnusedVariable).
    m_ctx.opts().set_enabled(semantic::FlagKind::Lint, true);
    auto t = Term::Create(TermID::NAME, "x", {}, parser::token_type::NAME);
    auto var = std::make_shared<VarDecl>(std::move(t), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "42"));
    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(var));

    SemanticPassRunner runner(m_ctx, *m_types);
    EXPECT_TRUE(runner.run(seq));
    EXPECT_GT(m_ctx.diag().warningCount(), 0);
}

TEST_F(SemanticTest, LintUsedVarNoWarning) {
    // -Wlint: a используется в b := a → a без warning, b (неиспользуемый) - с warning.
    m_ctx.opts().set_enabled(semantic::FlagKind::Lint, true);

    auto ta = Term::Create(TermID::NAME, "a", {}, parser::token_type::NAME);
    auto a = std::make_shared<VarDecl>(std::move(ta), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "1"));
    auto tb = Term::Create(TermID::NAME, "b", {}, parser::token_type::NAME);
    auto b = std::make_shared<VarDecl>(std::move(tb), nullptr, std::make_shared<IdentName>("a"));

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(a));
    seq.push_back(std::move(b));

    SemanticPassRunner runner(m_ctx, *m_types);
    EXPECT_TRUE(runner.run(seq));

    // Ровно одно unused-variable (b); a используется в инициализаторе b.
    int unusedWarnings = 0;
    for (const auto& d : m_ctx.diag().diagnostics()) {
        if (d.severity == Severity::Warning && d.message.find("unused variable") != std::string::npos) {
            ++unusedWarnings;
        }
    }
    EXPECT_EQ(unusedWarnings, 1);
}

TEST_F(SemanticTest, LintUnusedVarInModule) {
    // -Wlint: корневой узел реального pipeline - ModuleNode; ядро обходит m_body модуля.
    m_ctx.opts().set_enabled(semantic::FlagKind::Lint, true);

    auto t = Term::Create(TermID::NAME, "x", {}, parser::token_type::NAME);
    auto var = std::make_shared<VarDecl>(std::move(t), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "42"));
    auto mod = std::make_shared<ModuleNode>(ModuleIndex{0}, "m");
    mod->m_body = std::vector<AstNodePtr>{std::move(var)};

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(mod));

    SemanticPassRunner runner(m_ctx, *m_types);
    EXPECT_TRUE(runner.run(seq));
    EXPECT_GT(m_ctx.diag().warningCount(), 0);
}

TEST_F(SemanticTest, LintUnusedParameterWarning) {
    // -Wlint: неиспользуемый параметр функции → диагностика semantic::DiagId::UnusedParameter
    // ("unused parameter"), отдельная от unused-variable.
    m_ctx.opts().set_enabled(semantic::FlagKind::Lint, true);

    auto funcTerm = Term::Create(TermID::NAME, "func", {}, parser::token_type::NAME);
    auto func = std::make_shared<FuncDecl>(std::move(funcTerm));
    func->m_params = std::vector<AstNodePtr>{};
    func->m_params->push_back(std::make_shared<ArgNode>("arg", std::make_shared<IdentType>("Int32")));
    func->m_type = std::make_shared<IdentType>("Int32");

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(func));

    SemanticPassRunner runner(m_ctx, *m_types);
    ASSERT_TRUE(runner.run(seq));

    int paramWarnings = 0;
    for (const auto& d : m_ctx.diag().diagnostics()) {
        if (d.severity == Severity::Warning && d.message.find("unused parameter") != std::string::npos) {
            ++paramWarnings;
        }
    }
    EXPECT_GT(paramWarnings, 0);
}

TEST_F(SemanticTest, ContextMacroNamespaceStringified) {
    // ns:: { x := @# @::; };  → инициализатор становится StrChar "::ns::"
    auto ns = std::make_shared<ScopeBlock>("ns::", 0);
    auto var = std::make_shared<VarDecl>("x", nullptr, std::make_shared<ContextMacro>(ParserToken::Kind::ContextMacro, "@#@::"));
    ns->m_body.push_back(std::move(var));

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(ns));

    SemanticPassRunner runner(m_ctx, *m_types);
    EXPECT_TRUE(runner.run(seq));

    const auto& v = static_cast<const VarDecl&>(*seq[0]->as_sequence()->m_body[0]);
    ASSERT_NE(v.m_initializer, nullptr);
    EXPECT_EQ(v.m_initializer->kind(), ParserToken::Kind::StrChar);
    EXPECT_EQ(requiredText(*v.m_initializer), "::ns::");
}

TEST_F(SemanticTest, ContextMacroFuncSigAlwaysString) {
    // @__FUNCSIG__ внутри функции → сразу StrChar с сигнатурой.
    auto fn = std::make_shared<FuncDecl>("f");
    fn->m_type = std::make_shared<IdentType>("Void");
    fn->m_definition =
        Definition::fromBody({std::make_shared<VarDecl>("s", nullptr, std::make_shared<ContextMacro>(ParserToken::Kind::ContextMacro, "@__FUNCSIG__"))});

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(fn));

    SemanticPassRunner runner(m_ctx, *m_types);
    EXPECT_TRUE(runner.run(seq));

    auto& fd = static_cast<FuncDecl&>(*seq[0]);
    ASSERT_TRUE(fd.hasBody());
    const auto& v = static_cast<const VarDecl&>(*(*fd.body()).at(0));
    ASSERT_NE(v.m_initializer, nullptr);
    EXPECT_EQ(v.m_initializer->kind(), ParserToken::Kind::StrChar);
    EXPECT_EQ(requiredText(*v.m_initializer), "f():Void");
}

TEST_F(SemanticTest, ContextMacroFuncOutsideFunctionError) {
    // @# @__FUNCTION__ вне функции → диагностика ошибки.
    auto var = std::make_shared<VarDecl>("x", nullptr, std::make_shared<ContextMacro>(ParserToken::Kind::ContextMacro, "@#@__FUNCTION__"));
    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(var));

    SemanticPassRunner runner(m_ctx, *m_types);
    EXPECT_FALSE(runner.run(seq));
    EXPECT_GT(m_ctx.diag().errorCount(), 0);
}

// -- Встроенный маркер @__CHECK_AREA__ (проверка области применения макроса) --

TEST_F(SemanticTest, CheckAreaOkInsideFunction) {
    // Маркер area=Function ВНУТРИ функции -> проходит, маркер удаляется, ошибок нет.
    auto fn = std::make_shared<FuncDecl>("f");
    fn->m_type = std::make_shared<IdentType>("Void");
    auto marker = std::make_shared<CheckAreaStmt>(ParserToken::Kind::CheckAreaStmt, "@__CHECK_AREA__");
    marker->area = AreaKind::Function;
    fn->m_definition = Definition::fromBody({std::move(marker)});

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(fn));

    SemanticPassRunner runner(m_ctx, *m_types);
    EXPECT_TRUE(runner.run(seq));
    EXPECT_EQ(m_ctx.diag().errorCount(), 0);
}

TEST_F(SemanticTest, CheckAreaViolationTopLevelDefaultWarning) {
    // Маркер area=Function вне функции: default severity (Warning из -Wcheck-area) - не блокирует.
    auto marker = std::make_shared<CheckAreaStmt>(ParserToken::Kind::CheckAreaStmt, "@__CHECK_AREA__");
    marker->area = AreaKind::Function;

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(marker));

    SemanticPassRunner runner(m_ctx, *m_types);
    EXPECT_TRUE(runner.run(seq));
    EXPECT_EQ(m_ctx.diag().errorCount(), 0);
    bool found = false;
    for (const auto& d : m_ctx.diag().diagnostics()) {
        if (d.severity == Severity::Warning && d.message.find("@__CHECK_AREA__") != std::string::npos) {
            found = true;
        }
    }
    EXPECT_TRUE(found);
}

TEST_F(SemanticTest, CheckAreaViolationTopLevelError) {
    // Переопределение behavior=Severity::Error -> блокирующая ошибка.
    auto marker = std::make_shared<CheckAreaStmt>(ParserToken::Kind::CheckAreaStmt, "@__CHECK_AREA__");
    marker->area = AreaKind::Function;
    marker->behavior = Severity::Error;

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(marker));

    SemanticPassRunner runner(m_ctx, *m_types);
    EXPECT_FALSE(runner.run(seq));
    EXPECT_GT(m_ctx.diag().errorCount(), 0);
}

TEST_F(SemanticTest, CheckAreaMissingAttributeError) {
    // Требуемый атрибут (лежит в attrs() самого маркера) отсутствует у текущей области -> error.
    auto fn = std::make_shared<FuncDecl>("g");
    fn->m_type = std::make_shared<IdentType>("Void");
    auto marker = std::make_shared<CheckAreaStmt>(ParserToken::Kind::CheckAreaStmt, "@__CHECK_AREA__");
    marker->area = AreaKind::Function;
    marker->behavior = Severity::Error;
    const auto ro = m_ctx.attrs().lookup(attr::ReadOnly);
    ASSERT_TRUE(ro.has_value());
    marker->add_attr(*ro); // требование: у текущей области должен быть @[readonly]; у g его нет
    fn->m_definition = Definition::fromBody({std::move(marker)});

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(fn));

    SemanticPassRunner runner(m_ctx, *m_types);
    EXPECT_FALSE(runner.run(seq));
    EXPECT_GT(m_ctx.diag().errorCount(), 0);
}

// -- Инференс типов выражений (inferred vs explicit) --

TEST_F(SemanticTest, InferredLiteralType) {
    // x := 42  →  выведенный минимальный Int8 (42 ≤ 127); тип на узле и в Symbol.
    auto t = Term::Create(TermID::NAME, "x", {}, parser::token_type::NAME);
    auto var = std::make_shared<VarDecl>(std::move(t), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "42"));
    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(var));

    SemanticPassRunner runner(m_ctx, *m_types);
    ASSERT_TRUE(runner.run(seq));

    const TypeId int8 = m_types->read().findType(type::Int8).value_or(INVALID_TYPE_ID);
    auto* sym = runner.analysis().symbols().resolve("x");
    ASSERT_NE(sym, nullptr);
    EXPECT_TRUE(sym->type.isInferred());
    EXPECT_EQ(sym->type.getTypeId(), int8);
    // Выведенный тип записан на узле объявления - для кодогенерации после сброса скоуп-стека.
    EXPECT_EQ(static_cast<VarDecl&>(*seq[0]).inferredType.getTypeId(), int8);
}

TEST_F(SemanticTest, InferredWideningByAssignment) {
    // x := 1; x = 1000;  →  join {Int8, Int16} = Int16 (расширение по истории присвоений).
    auto t = Term::Create(TermID::NAME, "x", {}, parser::token_type::NAME);
    auto var = std::make_shared<VarDecl>(std::move(t), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "1"));

    auto opTerm = Term::Create(TermID::ASSIGN, "=", {}, parser::token_type::END);
    auto assign = std::make_shared<Binary>(ParserToken::Kind::AssignOp, std::move(opTerm));
    assign->m_left = std::make_shared<IdentName>("x");
    assign->m_right = std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "1000");

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(var));
    seq.push_back(std::move(assign));

    SemanticPassRunner runner(m_ctx, *m_types);
    ASSERT_TRUE(runner.run(seq));

    const TypeId int16 = m_types->read().findType(type::Int16).value_or(INVALID_TYPE_ID);
    auto* sym = runner.analysis().symbols().resolve("x");
    ASSERT_NE(sym, nullptr);
    EXPECT_TRUE(sym->type.isInferred());
    EXPECT_EQ(sym->type.getTypeId(), int16);
    EXPECT_EQ(static_cast<VarDecl&>(*seq[0]).inferredType.getTypeId(), int16);
}

TEST_F(SemanticTest, InferredIntWidenedByCompoundArith) {
    // mult := 1 (выведенный Int8); mult += 5 → составная арифметика малых целых даёт Int32
    // (C++ promotion int8→int), невыводимая переменная расширяется до результата операции.
    auto t = Term::Create(TermID::NAME, "mult", {}, parser::token_type::NAME);
    auto var = std::make_shared<VarDecl>(std::move(t), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "1"));
    auto opTerm = Term::Create(TermID::ASSIGN, "+=", {}, parser::token_type::END);
    auto assign = std::make_shared<Binary>(ParserToken::Kind::AssignOp, std::move(opTerm));
    assign->m_left = std::make_shared<IdentName>("mult");
    assign->m_right = std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "5");

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(var));
    seq.push_back(std::move(assign));

    SemanticPassRunner runner(m_ctx, *m_types);
    ASSERT_TRUE(runner.run(seq));

    const TypeId int32 = m_types->read().findType(type::Int32).value_or(INVALID_TYPE_ID);
    auto* sym = runner.analysis().symbols().resolve("mult");
    ASSERT_NE(sym, nullptr);
    EXPECT_TRUE(sym->type.isInferred());
    EXPECT_EQ(sym->type.getTypeId(), int32);
    EXPECT_EQ(static_cast<VarDecl&>(*seq[0]).inferredType.getTypeId(), int32);
}

TEST_F(SemanticTest, ExplicitBoolNotWidenedByCompoundArith) {
    // x:Bool := 1; x += 5 → явный Bool НЕ расширяется до Int64 → ошибка.
    auto t = Term::Create(TermID::NAME, "x", {}, parser::token_type::NAME);
    auto var = std::make_shared<VarDecl>(std::move(t), std::make_shared<IdentType>("Bool"), std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "1"));
    auto opTerm = Term::Create(TermID::ASSIGN, "+=", {}, parser::token_type::END);
    auto assign = std::make_shared<Binary>(ParserToken::Kind::AssignOp, std::move(opTerm));
    assign->m_left = std::make_shared<IdentName>("x");
    assign->m_right = std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "5");

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(var));
    seq.push_back(std::move(assign));

    SemanticPassRunner runner(m_ctx, *m_types);
    bool ok = runner.run(seq);

    EXPECT_FALSE(ok);
    EXPECT_GT(m_ctx.diag().errorCount(), 0);
}

TEST_F(SemanticTest, ExpressionResultTypeCppPromotion) {
    // x := 2 (Int8); y := x + 3  →  Int8 + Int8 = Int32 (обычные арифметические преобразования C++).
    auto tx = Term::Create(TermID::NAME, "x", {}, parser::token_type::NAME);
    auto xvar = std::make_shared<VarDecl>(std::move(tx), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "2"));

    auto ty = Term::Create(TermID::NAME, "y", {}, parser::token_type::NAME);
    auto opTerm = Term::Create(TermID::OP_MATH, "+", {}, parser::token_type::END);
    auto add = std::make_shared<Binary>(ParserToken::Kind::MathOp, std::move(opTerm));
    add->m_left = std::make_shared<IdentName>("x");
    add->m_right = std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "3");
    auto yvar = std::make_shared<VarDecl>(std::move(ty), nullptr, std::move(add));

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(xvar));
    seq.push_back(std::move(yvar));

    SemanticPassRunner runner(m_ctx, *m_types);
    ASSERT_TRUE(runner.run(seq));

    EXPECT_EQ(static_cast<VarDecl&>(*seq[1]).inferredType.getTypeId(), m_types->read().findType(type::Int32).value_or(INVALID_TYPE_ID));
}

TEST_F(SemanticTest, ExplicitTypeNotWidened) {
    // x:Int32 := 1; x = 1000;  →  явный тип фиксирован: x остаётся Int32.
    auto t = Term::Create(TermID::NAME, "x", {}, parser::token_type::NAME);
    auto xvar = std::make_shared<VarDecl>(std::move(t), std::make_shared<IdentType>("Int32"), std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "1"));

    auto opTerm = Term::Create(TermID::ASSIGN, "=", {}, parser::token_type::END);
    auto assign = std::make_shared<Binary>(ParserToken::Kind::AssignOp, std::move(opTerm));
    assign->m_left = std::make_shared<IdentName>("x");
    assign->m_right = std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "1000");

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(xvar));
    seq.push_back(std::move(assign));

    SemanticPassRunner runner(m_ctx, *m_types);
    ASSERT_TRUE(runner.run(seq));

    const TypeId int32 = m_types->read().findType(type::Int32).value_or(INVALID_TYPE_ID);
    auto* sym = runner.analysis().symbols().resolve("x");
    ASSERT_NE(sym, nullptr);
    EXPECT_FALSE(sym->type.isInferred());
    EXPECT_EQ(sym->type.getTypeId(), int32); // явный тип - структурный, без бита inferred
}

// -- Константность (kConstFlag) на переменных ('^' → attr::ReadOnly) --

TEST_F(SemanticTest, ReadOnlyVarSetsConstBitTyped) {
    // x^: Int32 := 1 → тип несёт бит kConstFlag («константность в типе»); структурный базис - Int32.
    auto t = Term::Create(TermID::NAME, "x", {}, parser::token_type::NAME);
    auto var = std::make_shared<VarDecl>(std::move(t), std::make_shared<IdentType>("Int32"), std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "1"));
    var->add_attr(m_ctx.attrs().lookup(attr::ReadOnly).value());
    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(var));

    SemanticPassRunner runner(m_ctx, *m_types);
    ASSERT_TRUE(runner.run(seq));

    const TypeId int32 = m_types->read().findType(type::Int32).value_or(INVALID_TYPE_ID);
    auto* sym = runner.analysis().symbols().resolve("x");
    ASSERT_NE(sym, nullptr);
    EXPECT_TRUE(sym->type.isConst());
    EXPECT_EQ(sym->type.getTypeId(), int32);
    EXPECT_FALSE(sym->type.isInferred()); // явный тип - без бита inferred
}

TEST_F(SemanticTest, ReadOnlyVarSetsConstBitUntyped) {
    // x^ := 42 → выведенный минимальный Int8 несёт биты inferred и const.
    auto t = Term::Create(TermID::NAME, "x", {}, parser::token_type::NAME);
    auto var = std::make_shared<VarDecl>(std::move(t), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "42"));
    var->add_attr(m_ctx.attrs().lookup(attr::ReadOnly).value());
    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(var));

    SemanticPassRunner runner(m_ctx, *m_types);
    ASSERT_TRUE(runner.run(seq));

    const TypeId int8 = m_types->read().findType(type::Int8).value_or(INVALID_TYPE_ID);
    auto* sym = runner.analysis().symbols().resolve("x");
    ASSERT_NE(sym, nullptr);
    EXPECT_TRUE(sym->type.isConst());
    EXPECT_TRUE(sym->type.isInferred());
    EXPECT_EQ(sym->type.getTypeId(), int8);
    // inferredType на узле - структурный (без битов), для кодогенерации.
    EXPECT_EQ(static_cast<VarDecl&>(*seq[0]).inferredType.getTypeId(), int8);
}

// -- Become-const (`x^ = ...`) и защита от записи в константу --
namespace {
AttrId ReadOnlyAttr(Context& ctx) {
    auto id = ctx.attrs().lookup(attr::ReadOnly);
    EXPECT_TRUE(id.has_value()) << "attr::ReadOnly must be registered";
    return id.value();
}
} // namespace

TEST_F(SemanticTest, BecomeConstViaCaretAssignment) {
    // x := 42; x^ += 1 → became-const отслеживает DA-проход (flow-факт), ошибок нет.
    auto t = Term::Create(TermID::NAME, "x", {}, parser::token_type::NAME);
    auto var = std::make_shared<VarDecl>(std::move(t), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "42"));

    // x^ += 1 - LHS IdentName с attr::ReadOnly (как после парсинга каретки на имени).
    auto opTerm = Term::Create(TermID::ASSIGN, "+=", {}, parser::token_type::END);
    auto assign = std::make_shared<Binary>(ParserToken::Kind::AssignOp, std::move(opTerm));
    auto lhs = std::make_shared<IdentName>("x");
    lhs->add_attr(ReadOnlyAttr(m_ctx));
    assign->m_left = std::move(lhs);
    assign->m_right = std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "1");

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(var));
    seq.push_back(std::move(assign));

    SemanticPassRunner runner(m_ctx, *m_types);
    EXPECT_TRUE(runner.run(seq));
    EXPECT_EQ(m_ctx.diag().errorCount(), 0);

    // QualType не носитель became-const: DA хранит flow-факт отдельно.
    auto* sym = runner.analysis().symbols().resolve("x");
    ASSERT_NE(sym, nullptr);
    EXPECT_FALSE(sym->type.isConst());
}

TEST_F(SemanticTest, WriteToConstDeclarationIsError) {
    // x^ := 42; x = 5 → ошибка записи в константную переменную.
    auto t = Term::Create(TermID::NAME, "x", {}, parser::token_type::NAME);
    auto var = std::make_shared<VarDecl>(std::move(t), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "42"));
    var->add_attr(ReadOnlyAttr(m_ctx));

    auto opTerm = Term::Create(TermID::ASSIGN, "=", {}, parser::token_type::END);
    auto assign = std::make_shared<Binary>(ParserToken::Kind::AssignOp, std::move(opTerm));
    assign->m_left = std::make_shared<IdentName>("x");
    assign->m_right = std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "5");

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(var));
    seq.push_back(std::move(assign));

    SemanticPassRunner runner(m_ctx, *m_types);
    EXPECT_FALSE(runner.run(seq));
    EXPECT_GT(m_ctx.diag().errorCount(), 0);
}

TEST_F(SemanticTest, WriteToBecomeConstIsError) {
    // x := 42; x^ += 1; x = 5 → x константна после x^ += 1, запись - ошибка.
    auto t = Term::Create(TermID::NAME, "x", {}, parser::token_type::NAME);
    auto var = std::make_shared<VarDecl>(std::move(t), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "42"));

    auto makeCaretAssign = [&]() {
        auto opTerm = Term::Create(TermID::ASSIGN, "+=", {}, parser::token_type::END);
        auto assign = std::make_shared<Binary>(ParserToken::Kind::AssignOp, std::move(opTerm));
        auto lhs = std::make_shared<IdentName>("x");
        lhs->add_attr(ReadOnlyAttr(m_ctx));
        assign->m_left = std::move(lhs);
        assign->m_right = std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "1");
        return assign;
    };
    auto assignConst = std::make_shared<Binary>(ParserToken::Kind::AssignOp, Term::Create(TermID::ASSIGN, "=", {}, parser::token_type::END));
    assignConst->m_left = std::make_shared<IdentName>("x");
    assignConst->m_right = std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "5");

    std::vector<AstNodePtr> body;
    body.push_back(std::move(var));
    body.push_back(makeCaretAssign());
    body.push_back(std::move(assignConst));

    auto fn = std::make_shared<FuncDecl>("f");
    fn->m_definition = Definition::fromBody(std::move(body));

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(fn));

    SemanticPassRunner runner(m_ctx, *m_types);
    // Семантика проходит: запись в ставшую константной переменную ловит flow-анализ DA, который
    // выполняется в HIR-фазе Pipeline (semantic → buildHir → verify → flow), а не в runner'е.
    ASSERT_TRUE(runner.run(seq));
    HirModule hir = buildHir(seq, m_ctx, *m_types);
    flow::MirModule mir = flow::buildMir(hir.nodes(), m_ctx.attrs());
    semantic::DefiniteAssignmentPass(m_ctx).run(mir);
    EXPECT_GT(m_ctx.diag().errorCount(), 0);
}

TEST_F(SemanticTest, NarrowingVariableErrorWithFixit) {
    // a:Int64 := 5; b:Int8 := a;  →  сужение Int64→Int8: ошибка.
    // (fixit-подсказка «use cast :Int8(a)» проверяется в lit-тесте narrowing_error.src -
    //  для ручных узлов range невалиден, и fixit не прикрепляется.)
    auto ta = Term::Create(TermID::NAME, "a", {}, parser::token_type::NAME);
    auto avar = std::make_shared<VarDecl>(std::move(ta), std::make_shared<IdentType>("Int64"), std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "5"));
    auto tb = Term::Create(TermID::NAME, "b", {}, parser::token_type::NAME);
    auto bvar = std::make_shared<VarDecl>(std::move(tb), std::make_shared<IdentType>("Int8"), std::make_shared<IdentName>("a"));

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(avar));
    seq.push_back(std::move(bvar));

    SemanticPassRunner runner(m_ctx, *m_types);
    EXPECT_FALSE(runner.run(seq));
    EXPECT_GT(m_ctx.diag().errorCount(), 0);
}

TEST_F(SemanticTest, NarrowingLiteralFitsNoError) {
    // y:Int8 := 100;  →  литерал влезает в Int8 → без ошибки.
    auto ty = Term::Create(TermID::NAME, "y", {}, parser::token_type::NAME);
    auto yvar = std::make_shared<VarDecl>(std::move(ty), std::make_shared<IdentType>("Int8"), std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "100"));
    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(yvar));

    SemanticPassRunner runner(m_ctx, *m_types);
    EXPECT_TRUE(runner.run(seq));
    EXPECT_EQ(m_ctx.diag().errorCount(), 0);
}

TEST_F(SemanticTest, NarrowingLiteralTooWideError) {
    // x:Int8 := 1000;  →  литерал 1000 (Int16) не влезает в Int8 → ошибка.
    auto tx = Term::Create(TermID::NAME, "x", {}, parser::token_type::NAME);
    auto xvar = std::make_shared<VarDecl>(std::move(tx), std::make_shared<IdentType>("Int8"), std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "1000"));
    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(xvar));

    SemanticPassRunner runner(m_ctx, *m_types);
    EXPECT_FALSE(runner.run(seq));
    EXPECT_GT(m_ctx.diag().errorCount(), 0);
}

TEST_F(SemanticTest, AnyOperandResultPromotion) {
    // x:Any := 5; y := x + 2;  →  результат Int32 (any + конкретный → продвинутый конкретный).
    auto tx = Term::Create(TermID::NAME, "x", {}, parser::token_type::NAME);
    auto xvar = std::make_shared<VarDecl>(std::move(tx), std::make_shared<IdentType>("Any"), std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "5"));
    auto ty = Term::Create(TermID::NAME, "y", {}, parser::token_type::NAME);
    auto opTerm = Term::Create(TermID::OP_MATH, "+", {}, parser::token_type::END);
    auto add = std::make_shared<Binary>(ParserToken::Kind::MathOp, std::move(opTerm));
    add->m_left = std::make_shared<IdentName>("x");
    add->m_right = std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "2");
    auto yvar = std::make_shared<VarDecl>(std::move(ty), nullptr, std::move(add));

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(xvar));
    seq.push_back(std::move(yvar));

    SemanticPassRunner runner(m_ctx, *m_types);
    ASSERT_TRUE(runner.run(seq));
    EXPECT_EQ(static_cast<VarDecl&>(*seq[1]).inferredType.getTypeId(), m_types->read().findType(type::Int32).value_or(INVALID_TYPE_ID));
}

TEST_F(SemanticTest, LiteralBoolAndStringWidthTyping) {
    // t := 1 → Int8; c := "w" (двойные кавычки, узкая строка) → StrChar;
    // s := 'hi' (одинарные кавычки, широкая строка) → StrWide.
    auto tt = Term::Create(TermID::NAME, "t", {}, parser::token_type::NAME);
    auto tvar = std::make_shared<VarDecl>(std::move(tt), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "1"));
    auto tc = Term::Create(TermID::NAME, "c", {}, parser::token_type::NAME);
    auto cvar = std::make_shared<VarDecl>(std::move(tc), nullptr, std::make_shared<Literal>(ParserToken::Kind::StrChar, "w"));
    auto ts = Term::Create(TermID::NAME, "s", {}, parser::token_type::NAME);
    auto svar = std::make_shared<VarDecl>(std::move(ts), nullptr, std::make_shared<Literal>(ParserToken::Kind::StrWide, "hi"));

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(tvar));
    seq.push_back(std::move(cvar));
    seq.push_back(std::move(svar));

    SemanticPassRunner runner(m_ctx, *m_types);
    ASSERT_TRUE(runner.run(seq));

    EXPECT_EQ(static_cast<VarDecl&>(*seq[0]).inferredType.getTypeId(), m_types->read().findType(type::Int8).value_or(INVALID_TYPE_ID));
    EXPECT_EQ(static_cast<VarDecl&>(*seq[1]).inferredType.getTypeId(), m_types->read().findType(type::StrChar).value_or(INVALID_TYPE_ID));
    EXPECT_EQ(static_cast<VarDecl&>(*seq[2]).inferredType.getTypeId(), m_types->read().findType(type::StrWide).value_or(INVALID_TYPE_ID));
}

TEST_F(SemanticTest, DictIntElementArithmeticIsInt32) {
    // d := (1, 2); x := d[0] + d[1];  →  элементы словаря из литералов 0/1 - Int8 (НЕ Bool);
    // Int8 + Int8 → Int32 (обычное C++-продвижение). Bool в арифметике запрещён, но здесь его нет.
    auto dictTerm = Term::Create(TermID::DICT, "", {}, parser::token_type::END);
    auto dict = std::make_shared<DictLiteralNode>(ParserToken::Kind::DictLiteral, std::move(dictTerm));
    dict->m_body.push_back(std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "1"));
    dict->m_body.push_back(std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "2"));

    auto dNameTerm = Term::Create(TermID::NAME, "d", {}, parser::token_type::NAME);
    auto dVar = std::make_shared<VarDecl>(std::move(dNameTerm), nullptr, std::move(dict));

    auto acc0 = std::make_shared<Binary>(ParserToken::Kind::ArrayAccess);
    acc0->m_left = std::make_shared<IdentName>("d");
    acc0->m_right = std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "0");
    auto acc1 = std::make_shared<Binary>(ParserToken::Kind::ArrayAccess);
    acc1->m_left = std::make_shared<IdentName>("d");
    acc1->m_right = std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "1");
    auto opTerm = Term::Create(TermID::OP_MATH, "+", {}, parser::token_type::END);
    auto add = std::make_shared<Binary>(ParserToken::Kind::MathOp, std::move(opTerm));
    add->m_left = std::move(acc0);
    add->m_right = std::move(acc1);
    auto xNameTerm = Term::Create(TermID::NAME, "x", {}, parser::token_type::NAME);
    auto xVar = std::make_shared<VarDecl>(std::move(xNameTerm), nullptr, std::move(add));

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(dVar));
    seq.push_back(std::move(xVar));

    SemanticPassRunner runner(m_ctx, *m_types);
    ASSERT_TRUE(runner.run(seq));
    EXPECT_EQ(m_ctx.diag().errorCount(), 0);
    EXPECT_EQ(static_cast<VarDecl&>(*seq[1]).inferredType.getTypeId(), m_types->read().findType(type::Int32).value_or(INVALID_TYPE_ID));
}

TEST_F(SemanticTest, LiteralIntArithmeticIsInt32) {
    // x := 1 + 2;  →  литералы `1`/`2` - Int8 (НЕ Bool); Int8 + Int8 → Int32.
    auto opTerm = Term::Create(TermID::OP_MATH, "+", {}, parser::token_type::END);
    auto add = std::make_shared<Binary>(ParserToken::Kind::MathOp, std::move(opTerm));
    add->m_left = std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "1");
    add->m_right = std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "2");
    auto t = Term::Create(TermID::NAME, "x", {}, parser::token_type::NAME);
    auto var = std::make_shared<VarDecl>(std::move(t), nullptr, std::move(add));
    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(var));

    SemanticPassRunner runner(m_ctx, *m_types);
    ASSERT_TRUE(runner.run(seq));
    EXPECT_EQ(m_ctx.diag().errorCount(), 0);
    EXPECT_EQ(static_cast<VarDecl&>(*seq[0]).inferredType.getTypeId(), m_types->read().findType(type::Int32).value_or(INVALID_TYPE_ID));
}

TEST_F(SemanticTest, ExplicitBoolInArithmeticError) {
    // x:Bool := 1; y := x + 2;  →  явный Bool в арифметике → ошибка компиляции.
    auto tx = Term::Create(TermID::NAME, "x", {}, parser::token_type::NAME);
    auto xvar = std::make_shared<VarDecl>(std::move(tx), std::make_shared<IdentType>("Bool"), std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "1"));
    auto opTerm = Term::Create(TermID::OP_MATH, "+", {}, parser::token_type::END);
    auto add = std::make_shared<Binary>(ParserToken::Kind::MathOp, std::move(opTerm));
    add->m_left = std::make_shared<IdentName>("x");
    add->m_right = std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "2");
    auto ty = Term::Create(TermID::NAME, "y", {}, parser::token_type::NAME);
    auto yvar = std::make_shared<VarDecl>(std::move(ty), nullptr, std::move(add));
    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(xvar));
    seq.push_back(std::move(yvar));

    SemanticPassRunner runner(m_ctx, *m_types);
    EXPECT_FALSE(runner.run(seq));
    EXPECT_GT(m_ctx.diag().errorCount(), 0);
}

// -- Storage (месторасположение переменной) + нормализация имён без сигила --

// Хук, записывающий месторасположение (storage) объявляемых переменных во время обхода.
class StorageProbe : public InlineAnalysisHook {
  public:
    [[nodiscard]] std::string_view name() const override { return "storage-probe"; }

    void onDeclare(const Symbol& sym) override {
        // sym не moved-from (declareOrComplete копирует): storage/decl и name валидны.
        if (sym.decl && sym.decl->kind() == ParserToken::Kind::VarDecl) {
            m_storage[std::string(requiredText(*sym.decl))] = sym.storage;
        }
    }
    std::map<std::string, Storage> m_storage;
};

TEST_F(SemanticTest, StorageGlobalIsGlobal) {
    // g := 1; на глобальном уровне → Storage::Global, имя без нормализации.
    auto var = std::make_shared<VarDecl>("g", nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "1"));
    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(var));
    SemanticPassRunner runner(m_ctx, *m_types);
    ASSERT_TRUE(runner.run(seq));
    auto* sym = runner.analysis().symbols().resolve("g");
    ASSERT_NE(sym, nullptr);
    EXPECT_EQ(sym->name, "g");
    EXPECT_EQ(sym->storage, Storage::Global);
}

TEST_F(SemanticTest, StorageLocalNormalizedToSigil) {
    // f() { x := 1; } - локальная переменная без сигила нормализуется в $x, Storage::Local.
    AnalysisContext actx(m_ctx, *m_types);
    NameResolutionPass core(actx);
    auto probe = std::make_unique<StorageProbe>();
    StorageProbe* probePtr = probe.get();
    core.addHook(std::move(probe));

    auto funcTerm = Term::Create(TermID::NAME, "f", {}, parser::token_type::NAME);
    auto func = std::make_shared<FuncDecl>(std::move(funcTerm));
    func->m_params = std::vector<AstNodePtr>{};
    func->m_definition = Definition::fromBody({});
    (*func->body()).push_back(std::make_shared<VarDecl>("x", nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "1")));

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(func));
    core.run(seq);
    core.finalize();

    EXPECT_EQ(m_ctx.diag().errorCount(), 0);
    EXPECT_EQ(probePtr->m_storage["$x"], Storage::Local);
}

TEST_F(SemanticTest, StorageNamespaceIsStatic) {
    // ns::x := 1; - имя с областью имён (::) → Storage::Static, без нормализации в $.
    AnalysisContext actx(m_ctx, *m_types);
    NameResolutionPass core(actx);
    auto probe = std::make_unique<StorageProbe>();
    StorageProbe* probePtr = probe.get();
    core.addHook(std::move(probe));

    auto var = std::make_shared<VarDecl>("ns::x", nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "1"));
    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(var));
    core.run(seq);
    core.finalize();

    EXPECT_EQ(m_ctx.diag().errorCount(), 0);
    EXPECT_EQ(probePtr->m_storage["ns::x"], Storage::Static);
}

TEST_F(SemanticTest, NoSigilLocalResolution) {
    // f() { x := 1; y := x; } - x/y нормализуются в $x/$y; ссылка x резолвится в $x
    // (без «undefined name»), единый алгоритм разрешения простых имён.
    AnalysisContext actx(m_ctx, *m_types);
    NameResolutionPass core(actx);
    auto funcTerm = Term::Create(TermID::NAME, "f", {}, parser::token_type::NAME);
    auto func = std::make_shared<FuncDecl>(std::move(funcTerm));
    func->m_params = std::vector<AstNodePtr>{};
    func->m_definition = Definition::fromBody({});
    (*func->body()).push_back(std::make_shared<VarDecl>("x", nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "1")));
    (*func->body()).push_back(std::make_shared<VarDecl>("y", nullptr, std::make_shared<IdentName>("x")));

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(func));
    core.run(seq);
    core.finalize();

    EXPECT_EQ(m_ctx.diag().errorCount(), 0);
}

TEST_F(SemanticTest, BareNameResolvesDeclarationAndParam) {
    // f(n:Int32):Void := { x := f($n); }; - bare `f` резолвится в объявление `f`,
    // а `$n` - в параметр `n` (правила вывода сигилов). Никаких «undefined name».
    AnalysisContext actx(m_ctx, *m_types);
    NameResolutionPass core(actx);

    auto funcTerm = Term::Create(TermID::NAME, "f", {}, parser::token_type::NAME);
    auto func = std::make_shared<FuncDecl>(std::move(funcTerm));
    auto pTerm = Term::Create(TermID::NAME, "n", {}, parser::token_type::NAME);
    func->m_params = std::vector<AstNodePtr>{};
    func->m_params->push_back(std::make_shared<ArgNode>(std::move(pTerm), std::make_shared<IdentType>("Int32")));
    func->m_definition = Definition::fromBody({});
    auto call = std::make_shared<CallExpr>(ParserToken::Kind::CallExpr, std::make_shared<IdentName>("f"));
    call->m_args = std::vector<AstNodePtr>{std::make_shared<IdentName>("$n")};
    (*func->body()).push_back(std::make_shared<VarDecl>("x", nullptr, std::move(call)));

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(func));
    core.run(seq);
    core.finalize();

    // Ни «undefined name 'f'», ни «undefined name '$n'» ($n → параметр n).
    EXPECT_EQ(m_ctx.diag().errorCount(), 0);
}

TEST_F(SemanticTest, EmbedUsageOptionDefaultWarns) {
    // Опция -Wembed (default Warning): сам факт использования узла {% ... %} даёт предупреждение.
    auto embedTerm = Term::Create(TermID::EMBED, "int x = 5;", {}, parser::token_type::END);
    auto embed = std::make_shared<RawText>(ParserToken::Kind::EmbedExpr, std::move(embedTerm));
    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(embed));
    SemanticPassRunner runner(m_ctx, *m_types);
    const int before = m_ctx.diag().warningCount();
    ASSERT_TRUE(runner.run(seq));
    EXPECT_GT(m_ctx.diag().warningCount(), before);
}

TEST_F(SemanticTest, EmbedUsageOptionIgnore) {
    // -Wembed=ignore → предупреждение за сам факт {% ... %} подавляется.
    m_ctx.opts().set(semantic::DiagId::Embed, Severity::Ignore);
    auto embedTerm = Term::Create(TermID::EMBED, "int x = 5;", {}, parser::token_type::END);
    auto embed = std::make_shared<RawText>(ParserToken::Kind::EmbedExpr, std::move(embedTerm));
    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(embed));
    SemanticPassRunner runner(m_ctx, *m_types);
    const int before = m_ctx.diag().warningCount();
    ASSERT_TRUE(runner.run(seq));
    EXPECT_EQ(m_ctx.diag().warningCount(), before);
}

TEST_F(SemanticTest, CheckAreaOverrideBehaviorIgnore) {
    // behavior = Severity::Ignore - явный per-macro override, подавляет диагностику
    // независимо от -Wcheck-area (отличие от default). Маркер вне функции.
    auto marker = std::make_shared<CheckAreaStmt>(ParserToken::Kind::CheckAreaStmt, "@__CHECK_AREA__");
    marker->area = AreaKind::Function;
    marker->behavior = Severity::Ignore;

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(marker));

    SemanticPassRunner runner(m_ctx, *m_types);
    EXPECT_TRUE(runner.run(seq));
    EXPECT_EQ(m_ctx.diag().errorCount(), 0);
    for (const auto& d : m_ctx.diag().diagnostics()) {
        EXPECT_EQ(d.message.find("@__CHECK_AREA__"), std::string::npos);
    }
}

} // namespace trust
