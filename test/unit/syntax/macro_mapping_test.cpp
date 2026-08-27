#include "syntax/macro_test_fixture.hpp"
TEST_F(MacroTest, PredefRootDir) {
    MacroPtr macro = std::make_shared<Macro>(m_ctx);

    ASSERT_NO_THROW(Parse("@__ROOT_DIR__", macro));
    ASSERT_EQ(TermID::NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_FALSE(LexOut().empty()) << LexOut();
}

// Вложенный Parser (через Parser::ParseTerm) наследует Macro из Context:
// dsl-макрос, загруженный в m_ctx, раскрывается во вложенном парсере.
// macro_expand=false - специальный случай «без раскрытия макросов».
TEST_F(MacroTest, NestedParserInheritsMacroFromContext) {
    MacroPtr macro = std::make_shared<Macro>(m_ctx);
    ASSERT_NO_THROW(Parse("@@ true @@ 1 @@@@;", macro));

    TermPtr term = Parser::ParseTerm("@true;", m_ctx);
    ASSERT_TRUE(term);
    ASSERT_EQ(TermID::INTEGER, term->getTermID()) << term->toString();
    ASSERT_EQ("1", term->getText());

    // macro_expand=false - раскрытие отключено, остаётся токен MACRO.
    term = Parser::ParseTerm("@true;", m_ctx, true, /*macro_expand=*/false);
    ASSERT_TRUE(term);
    ASSERT_EQ(TermID::MACRO, term->getTermID()) << term->toString();
}

// ══════════════════════════════════════════════════════════════
//  Source-map маппинг макросов (addMacroMapping при раскрытии)
//  Проверяет, что bodyRange вызова макроса покрывает ровно токены
//  вызова (а не следующий за ним токен), а defRange указывает на
//  тело макроса.
// ══════════════════════════════════════════════════════════════

namespace {

// 0-based индекс в строке → 1-based offset (используется makeLoc/getText)
uint32_t off1(size_t idx0) {
    return static_cast<uint32_t>(idx0 + 1);
}

} // namespace

// Определение `@@alias@@ replace @@@@;`, затем вызов `alias;`.
// Запрос на позиции вызова должен дать определение макроса (тело `replace`).
TEST_F(MacroTest, MacroMapping_SimpleCall_MapsToBody) {
    MacroPtr macro = std::make_shared<Macro>(m_ctx);

    std::string src = "@@alias@@ replace @@@@; alias;";
    ASSERT_NO_THROW(Parse(src, macro));

    auto* reader = m_ctx.source().toReader();
    ASSERT_NE(reader, nullptr);
    ReaderFile rFile = reader->findFileIdx("@input");
    ASSERT_FALSE(rFile.isInvalid());
    MapperFile mFile = m_ctx.source().findFileIdx("@input");
    ASSERT_FALSE(mFile.isInvalid());

    size_t defIdx = src.find("replace");
    size_t callIdx = src.rfind("alias");
    ASSERT_NE(defIdx, std::string::npos);
    ASSERT_NE(callIdx, std::string::npos);
    ASSERT_GT(callIdx, defIdx); // определение макроса расположено до вызова

    auto atCall = reader->getMacroDefRange(static_cast<ReaderLocation>(m_ctx.source().makeLoc(mFile, off1(callIdx))));
    ASSERT_TRUE(atCall.has_value());

    // defRange указывает на ВЕСЬ макрос (имя + тело): `@@alias@@ replace`
    EXPECT_EQ(reader->getText(*atCall), "@@alias@@ replace");
    EXPECT_EQ(atCall->begin.fileIdx(), rFile);

    // Позиция внутри последнего символа вызова тоже отображается (delta-проекция)
    auto inCall = reader->getMacroDefRange(static_cast<ReaderLocation>(m_ctx.source().makeLoc(mFile, off1(callIdx + 4))));
    ASSERT_TRUE(inCall.has_value());
}

// Определение `@@foo($a)@@ bar(@$a) @@@@;`, вызов `foo(1);`.
// Весь вызов (включая скобки и аргумент) отображается на тело макроса.
// Также проверяет, что ссылка на аргумент @$a в теле корректно резолвится.
TEST_F(MacroTest, MacroMapping_CallWithArgs_MapsWholeCall) {
    MacroPtr macro = std::make_shared<Macro>(m_ctx);

    std::string src = "@@foo($a)@@ bar(@$a) @@@@; foo(1);";
    ASSERT_NO_THROW(Parse(src, macro));

    auto* reader = m_ctx.source().toReader();
    ASSERT_NE(reader, nullptr);
    ReaderFile rFile = reader->findFileIdx("@input");
    ASSERT_FALSE(rFile.isInvalid());
    MapperFile mFile = m_ctx.source().findFileIdx("@input");
    ASSERT_FALSE(mFile.isInvalid());

    size_t callIdx = src.find("foo(1)");
    ASSERT_NE(callIdx, std::string::npos);

    auto atCall = reader->getMacroDefRange(static_cast<ReaderLocation>(m_ctx.source().makeLoc(mFile, off1(callIdx))));
    ASSERT_TRUE(atCall.has_value());
    EXPECT_EQ(reader->getText(*atCall), "@@foo($a)@@ bar(@$a)");

    // Позиция внутри аргументов вызова (открывающая скобка) тоже в пределах вызова
    size_t argIdx = src.find('(', callIdx);
    ASSERT_NE(argIdx, std::string::npos);
    auto inArgs = reader->getMacroDefRange(static_cast<ReaderLocation>(m_ctx.source().makeLoc(mFile, off1(argIdx))));
    ASSERT_TRUE(inArgs.has_value());
}

// Регрессия на off-by-one: раньше bodyRange захватывал следующий за вызовом
// токен. При inline-вызове `alias + 1` позиция оператора `+` (строго за концом
// вызова) НЕ должна отображаться в определение макроса.
TEST_F(MacroTest, MacroMapping_DoesNotSwallowNextToken) {
    MacroPtr macro = std::make_shared<Macro>(m_ctx);

    std::string src = "@@alias@@ replace @@@@; x := alias + 1;";
    ASSERT_NO_THROW(Parse(src, macro));

    auto* reader = m_ctx.source().toReader();
    ASSERT_NE(reader, nullptr);
    ReaderFile rFile = reader->findFileIdx("@input");
    ASSERT_FALSE(rFile.isInvalid());
    MapperFile mFile = m_ctx.source().findFileIdx("@input");
    ASSERT_FALSE(mFile.isInvalid());

    size_t plusIdx = src.find('+');
    ASSERT_NE(plusIdx, std::string::npos);

    // Позиция на операторе '+' находится после конца вызова `alias` - маппинга быть не должно
    auto atPlus = reader->getMacroDefRange(static_cast<ReaderLocation>(m_ctx.source().makeLoc(mFile, off1(plusIdx))));
    EXPECT_FALSE(atPlus.has_value());
}

// -- @__MODULE_NAME__ --

// Прямое раскрытие предопределённого макроса @__MODULE_NAME__: имя файла модуля
// без расширения, относительно главного файла, разделители каталога → '_'.
TEST_F(MacroTest, PredefinedModuleName) {
    m_ctx.source().setMainModuleFile(m_ctx.source().add_source("main.src", ""));
    MacroPtr macro = std::make_shared<Macro>(m_ctx);

    // Подмодуль sub/mod.src → "sub/mod" → "sub_mod"
    ASSERT_NO_THROW(Parse("@__MODULE_NAME__", macro, "sub/mod.src"));
    ASSERT_STREQ("sub_mod", LexOut().c_str());

    // Главный файл main.src → "main"
    ASSERT_NO_THROW(Parse("@__MODULE_NAME__", macro, "main.src"));
    ASSERT_STREQ("main", LexOut().c_str());
}

// Макрос `module` (тело = @__MODULE_NAME__) раскрывается корректно.
TEST_F(MacroTest, ModuleMacroExpansion) {
    m_ctx.source().setMainModuleFile(m_ctx.source().add_source("main.src", ""));
    MacroPtr macro = std::make_shared<Macro>(m_ctx);

    ASSERT_NO_THROW(Parse("@@ module @@ @__MODULE_NAME__ @@@@;", macro));
    ASSERT_NO_THROW(Parse("@module", macro, "sub/mod.src"));
    ASSERT_STREQ("sub_mod", LexOut().c_str());
}

// Макрос `main` (тело = @__MODULE_NAME__ @## __main__): конкатенация предопределённого
// макроса должна раскрыть имя модуля до склейки → "<mod>__main__".
TEST_F(MacroTest, MainMacroConcat) {
    m_ctx.source().setMainModuleFile(m_ctx.source().add_source("main.src", ""));
    MacroPtr macro = std::make_shared<Macro>(m_ctx);

    ASSERT_NO_THROW(Parse("@@ main @@ @__MODULE_NAME__ @## __main__ @@@@;", macro));
    ASSERT_NO_THROW(Parse("@main", macro, "sub/mod.src"));
    ASSERT_STREQ("sub_mod__main__", LexOut().c_str());

    ASSERT_NO_THROW(Parse("@main", macro, "main.src"));
    ASSERT_STREQ("main__main__", LexOut().c_str());
}

// Мнемонические макросы DSL: `func` для определения функций.
// `func $... { ... }` захватывает вариативной группой `$...` ВСЮ «голову» объявления
// (имя + аргументы + необязательный `: тип`) до терминатора `{` (граница тела), а тело
// просто вставляет `:=` перед `{`. Одна группа в сигнатуре -> `@$...` в теле ссылается на неё.
// `@func myfunc ( a, b ) { ... }` → `myfunc ( a, b ) := { ... }`.
TEST_F(MacroTest, MnemonicFunc_Basic) {
    MacroPtr macro = std::make_shared<Macro>(m_ctx);
    ASSERT_NO_THROW(Parse("@@ func $... { @@ @$... := { @@@@;", macro));
    ASSERT_NO_THROW(Parse("@func myfunc ( a, b ) { body }", macro));
    ASSERT_STREQ("myfunc ( a , b ) := { body }", LexOut().c_str());
}

// Новые макросы-обёртки функций (func_const/func_pure/func_constexpr/func_sync/func_thread):
// задают предустановленный встроенный атрибут через хелпер @attr(...) (→ `@[ <attr> ]@`) и
// объявление функции по образцу @func (`@$... := {`). Раскрытие не должно зацикливаться даже
// для имён атрибутов, совпадающих с keyword-макросами (func_const/func_pure): содержимое
// `@[ ... ]@` макро-именами НЕ раскрывается (parser::m_in_attr).
TEST_F(MacroTest, MnemonicFuncWithAttr) {
    MacroPtr macro = std::make_shared<Macro>(m_ctx);
    ASSERT_NO_THROW(Parse("@@ attr(...) @@ @[ @$... ]@ @@@@;", macro));
    ASSERT_NO_THROW(Parse("@@ func_const $... { @@ @attr(func_const) @$... := { @@@@;", macro));
    ASSERT_NO_THROW(Parse("@@ func_pure $... { @@ @attr(func_pure) @$... := { @@@@;", macro));
    ASSERT_NO_THROW(Parse("@@ func_constexpr $... { @@ @attr(constexpr) @$... := { @@@@;", macro));
    ASSERT_NO_THROW(Parse("@@ func_sync $... { @@ @attr(sync) @$... := { @@@@;", macro));
    ASSERT_NO_THROW(Parse("@@ func_thread $... { @@ @attr(thread) @$... := { @@@@;", macro));

    ASSERT_NO_THROW(Parse("@func_const c ( a, b ): Int32 { body }", macro));
    ASSERT_STREQ("@[ func_const ]@ c ( a , b ) : Int32 := { body }", LexOut().c_str());

    m_ctx.diag().clear();
    ASSERT_NO_THROW(Parse("@func_pure p ( a, b ): Int32 { body }", macro));
    ASSERT_STREQ("@[ func_pure ]@ p ( a , b ) : Int32 := { body }", LexOut().c_str());

    m_ctx.diag().clear();
    ASSERT_NO_THROW(Parse("@func_constexpr k ( a, b ): Int32 { body }", macro));
    ASSERT_STREQ("@[ constexpr ]@ k ( a , b ) : Int32 := { body }", LexOut().c_str());

    m_ctx.diag().clear();
    ASSERT_NO_THROW(Parse("@func_sync s ( a, b ): Int32 { body }", macro));
    ASSERT_STREQ("@[ sync ]@ s ( a , b ) : Int32 := { body }", LexOut().c_str());

    m_ctx.diag().clear();
    ASSERT_NO_THROW(Parse("@func_thread t ( a, b ): Int32 { body }", macro));
    ASSERT_STREQ("@[ thread ]@ t ( a , b ) : Int32 := { body }", LexOut().c_str());
}

// Внутри скобок атрибута `@[ ... ]@` макросы запрещены: содержимое атрибута - имя и
// параметры-литералы, не код. Явный `@`-макрос (TermID::MACRO) внутри атрибута - ошибка.
TEST_F(MacroTest, AttributeRejectsAtMacro) {
    MacroPtr macro = std::make_shared<Macro>(m_ctx);
    ASSERT_NO_THROW(Parse("@[ @foo ]@ g(x:Int32):Int32 := { 42 };", macro));
    EXPECT_GT(m_ctx.diag().errorCount(), 0);
    EXPECT_EQ(std::string(LexOut()).find("not allowed inside an attribute"), std::string::npos) << LexOut();
}

// Самовоспроизведение макроса (тело раскрывается в само себя через bare-имя) должно давать
// мягкую Error-диагностику (общий предохранитель parser::m_macro_expansion_total / kMacroExpansionLimit),
// а не зависать: `@recurse` -> `recurse` (bare) -> `recurse` -> ...
TEST_F(MacroTest, RecursiveMacroError) {
    MacroPtr macro = std::make_shared<Macro>(m_ctx);
    ASSERT_NO_THROW(Parse("@@ recurse @@ recurse @@@@;", macro));
    m_ctx.diag().clear();
    ASSERT_NO_THROW(Parse("@recurse;", macro));
    EXPECT_GT(m_ctx.diag().errorCount(), 0);
}

// `@func имя(...): тип { ... }` — функция с типом возврата. Та же одна группа `$...`
// захватывает и `: тип` (до `{`), поэтому отдельная форма сигнатуры не нужна.
TEST_F(MacroTest, MnemonicFunc_WithReturnType) {
    MacroPtr macro = std::make_shared<Macro>(m_ctx);
    ASSERT_NO_THROW(Parse("@@ func $... { @@ @$... := { @@@@;", macro));
    ASSERT_NO_THROW(Parse("@func add ( a, b ): Int32 { body }", macro));
    ASSERT_STREQ("add ( a , b ) : Int32 := { body }", LexOut().c_str());
}

// Тип возврата может быть составным (со скобками): `Tuple(Int32, Int32)`. Группа `$...`
// захватывает тип до `{`, пропуская вложенные скобки типа.
TEST_F(MacroTest, MnemonicFunc_ReturnTypeWithBrackets) {
    MacroPtr macro = std::make_shared<Macro>(m_ctx);
    ASSERT_NO_THROW(Parse("@@ func $... { @@ @$... := { @@@@;", macro));
    ASSERT_NO_THROW(Parse("@func tup ( a, b ): Tuple(Int32, Int32) { body }", macro));
    ASSERT_STREQ("tup ( a , b ) : Tuple ( Int32 , Int32 ) := { body }", LexOut().c_str());
}

// Многоэлементные имена функций: нативное `%name` (2 токена: `%` + `name`), namespace
// `::ns::name`, global `::name`. Все они уходят в одну группу `$...` (голова до `{`).
TEST_F(MacroTest, MnemonicFunc_MultiElementNames) {
    MacroPtr macro = std::make_shared<Macro>(m_ctx);
    ASSERT_NO_THROW(Parse("@@ func $... { @@ @$... := { @@@@;", macro));

    ASSERT_NO_THROW(Parse("@func %sum_squares ( n:Int32 ): Int32 { body }", macro));
    EXPECT_NE(std::string(LexOut()).find("% sum_squares ( n : Int32 ) : Int32 := { body }"), std::string::npos) << LexOut();

    m_ctx.diag().clear();
    ASSERT_NO_THROW(Parse("@func ::ns::sum_squares ( n:Int32 ): Int32 { body }", macro));
    EXPECT_NE(std::string(LexOut()).find(":: ns :: sum_squares ( n : Int32 ) : Int32 := { body }"), std::string::npos) << LexOut();

    m_ctx.diag().clear();
    ASSERT_NO_THROW(Parse("@func ::sq ( n:Int32 ): Int32 { body }", macro));
    EXPECT_NE(std::string(LexOut()).find(":: sq ( n : Int32 ) : Int32 := { body }"), std::string::npos) << LexOut();
}

// Локальный шаблон с аргументами `$name ( ... ) {`: ParseTerm сливает `$name` и `( ... )`
// в один терм, имя фиксируется как `@$name` (один токен), а `( ... )` - группа аргументов.
// Это отдельная возможность движка (нужна, когда имя функции нужно ОТДЕЛЬНО от аргументов,
// напр. для перестановки), не связанная с shipped `func`.
TEST_F(MacroTest, LocalPatternWithArgs) {
    MacroPtr macro = std::make_shared<Macro>(m_ctx);
    ASSERT_NO_THROW(Parse("@@ named $name ( ... ) { @@ @$name ( @$... ) := { @@@@;", macro));
    ASSERT_NO_THROW(Parse("@named foo ( a, b ) { body }", macro));
    ASSERT_STREQ("foo ( a , b ) := { body }", LexOut().c_str());
}

// Групповая адресация по НОМЕРУ ЭЛЕМЕНТА: @$1.N (аргумент N группы 1, скобочной) и @$G.N
// (элемент N внескобочной группы G, напр. токен типа). @$2.1 = первый токен типа возврата.
TEST_F(MacroTest, GroupElementAddressing) {
    MacroPtr macro = std::make_shared<Macro>(m_ctx);
    // тело: имя, 1-й и 2-й аргументы (группа 1), первый токен типа (группа 2)
    ASSERT_NO_THROW(Parse("@@ func $name ( ... ): $... { @@ @$name ( @$1.1 , @$1.2 ): @$2.1 := { @@@@;", macro));

    ASSERT_NO_THROW(Parse("@func f ( a, b ): Int32 { body }", macro));
    ASSERT_STREQ("f ( a , b ) : Int32 := { body }", LexOut().c_str());
}

// Несколько `$...` в сигнатуре: хвостовой операторный `$...` (последний) + ещё один `$...` в скобках
// любого вида коллизируют как `@$...` - макрос принципиально нераскрываем. Это НЕОТКЛЮЧАЕМАЯ ошибка
// (не severity-опция, `-Wno-...` не существует). Сообщение подсказывает добавить терминатор после
// последнего `$...`. Операторный `$...` + call-группа (даже без `$...` внутри) тоже нераскрываем
// (оператор поглощает аргументы вызова).
TEST_F(MacroTest, SeveralEllipsis_NonOffableError) {
    MacroPtr macro = std::make_shared<Macro>(m_ctx);

    // Несколько `$...` -> ошибка (по умолчанию и всегда).
    ASSERT_NO_THROW(Parse("@@ foo ( $... ) $... @@ @$1... @$... @@@@;", macro));
    EXPECT_GT(m_ctx.diag().errorCount(), 0) << "several $... must be an error";

    // Легитимный операторный макрос с ОДНИМ `$...` без call-группы (extern $name $...) - без ошибки.
    m_ctx.diag().clear();
    ASSERT_NO_THROW(Parse("@@ extern $name $... @@ @$name @$... := % @$name ... @@@@;", macro));
    EXPECT_EQ(m_ctx.diag().errorCount(), 0) << "operator macro with single $... is valid";

    // Операторный `$...` + call-группа (один `$...`, оператор поглощает аргументы) - ошибка.
    m_ctx.diag().clear();
    ASSERT_NO_THROW(Parse("@@ bar ( $x ) $... @@ @$x @$... @@@@;", macro));
    EXPECT_GT(m_ctx.diag().errorCount(), 0) << "operator $... + call group must be an error";
}

// Удаление НЕсуществующего макроса - явная диагностика (не тихий stderr/fallback).
TEST_F(MacroTest, RemoveMissingMacro_ReportsError) {
    MacroPtr macro = std::make_shared<Macro>(m_ctx);
    m_ctx.diag().clear();
    ASSERT_NO_THROW(Parse("@@ nosuchmacro @@@@;", macro));
    EXPECT_GT(m_ctx.diag().errorCount(), 0) << "removing a missing macro must be an explicit error";
}

// Вызов макроса с несовпадающей сигнатурой (группа есть, но форма не подходит) ->
// ДЕТАЛЬНАЯ диагностика макропроцессора «does not match any pattern» (с причиной), а не
// общая «undefined name» анализатора. Полностью неизвестное имя - не макрос вообще.
TEST_F(MacroTest, UnknownMacroMapping_ReportsError) {
    MacroPtr macro = std::make_shared<Macro>(m_ctx);
    // группа `foo` существует, но требует аргументы в скобках
    ASSERT_NO_THROW(Parse("@@ foo ( $x ) @@ @$x @@@@;", macro));
    m_ctx.diag().clear();
    // @foo без скобок не совпадает с `foo ( $x )` -> детальная диагностика с причиной
    ASSERT_NO_THROW(Parse("@foo", macro));
    EXPECT_GT(m_ctx.diag().errorCount(), 0) << "a macro with a non-matching signature must be an error";
    const auto& diags = m_ctx.diag().diagnostics();
    ASSERT_FALSE(diags.empty());
    EXPECT_NE(std::string(diags.front().message).find("does not match any pattern"), std::string::npos) << diags.front().message;
    EXPECT_NE(std::string(diags.front().message).find("expected"), std::string::npos) << diags.front().message;
}

// Детальная диагностика + fix-it: «ближайший» кандидат указывает конкретную причину
// (expected/found), а fix-it предлагает замену, когда рассинхрон на простом литерале.
TEST_F(MacroTest, UnknownMacroMapping_DetailedMessageAndFixit) {
    MacroPtr macro = std::make_shared<Macro>(m_ctx);
    ASSERT_NO_THROW(Parse("@@ compare GREATER $x @@ @$x @@@@;", macro));
    m_ctx.diag().clear();
    ASSERT_NO_THROW(Parse("@compare MORE 1", macro));
    const auto& diags = m_ctx.diag().diagnostics();
    ASSERT_FALSE(diags.empty());
    EXPECT_NE(std::string(diags.front().message).find("expected 'GREATER' but found 'MORE'"), std::string::npos) << diags.front().message;
    // fix-it должен предлагать замену 'MORE' -> 'GREATER'
    bool hasFixit = false;
    for (const auto& d : diags) {
        for (const auto& fx : d.fixits) {
            if (fx.replacement == "GREATER") {
                hasFixit = true;
            }
        }
    }
    EXPECT_TRUE(hasFixit) << "expected a fix-it suggesting replacement with 'GREATER'";
}

// Элемент фиксированной группы 1 за границами (@$1.3 при 2 аргументах) -> «Invalid argument number».
TEST_F(MacroTest, GroupElement_OutOfRange_FixedArgs) {
    MacroPtr macro = std::make_shared<Macro>(m_ctx);
    m_ctx.diag().clear();
    ASSERT_NO_THROW(Parse("@@ foo ( $a, $b ) @@ @$1.3 @@@@;", macro));
    EXPECT_GT(m_ctx.diag().errorCount(), 0) << "@$1.3 with 2 fixed args must be invalid";

    // Валидный элемент фиксированной группы (@$1.2 = 2-й аргумент) - без ошибки.
    m_ctx.diag().clear();
    ASSERT_NO_THROW(Parse("@@ bar ( $a, $b ) @@ @$1.2 @@@@;", macro));
    EXPECT_EQ(m_ctx.diag().errorCount(), 0) << "@$1.2 with 2 fixed args must be valid";
}

// Раскрытие КОЛИЧЕСТВА элементов в нумерованных группах: @$#1 (переданные аргументы группы 1),
// @$#2 (токены внескобочной группы 2, тип) и т.д. - для перебора элементов группы.
TEST_F(MacroTest, GroupCount_ByGroupNumber) {
    MacroPtr macro = std::make_shared<Macro>(m_ctx);
    // группа 1: ( a, b ) -> 2 переданных аргумента (@$#1); группа 2: : Int32 -> 1 токен (@$#2)
    ASSERT_NO_THROW(Parse("@@ cnt ( ... ): $... { @@ @$#1 @$#2 @@@@;", macro));
    ASSERT_NO_THROW(Parse("@cnt ( a, b ): Int32 { body }", macro));
    // остаток `body }` невалиден как код, но раскрытие чисел видно в LexOut: "@$#1 @$#2" -> "2 1"
    EXPECT_NE(std::string(LexOut()).find("2 1"), std::string::npos) << LexOut();
}

// Имя макроса в сигнатуре может начинаться с '@' (имя другого макроса, напр. @@ @foo func ... @@).
// Группа хешируется без '@' (toMacroHashName), вызов - по префиксу @foo.
TEST_F(MacroTest, MacroNameWithAtPrefix) {
    MacroPtr macro = std::make_shared<Macro>(m_ctx);
    ASSERT_NO_THROW(Parse("@@ @foo $x @@ @$x @@@@;", macro));
    ASSERT_NO_THROW(Parse("@foo 42", macro));
    ASSERT_STREQ("42", LexOut().c_str());
}

// Каноническая СИММЕТРИЧНАЯ форма количества элементов группы: @$G.# (аналог @$G.N, но `#`).
// @$1.# = кол-во переданных аргументов гр.1, @$2.# = число токенов гр.2 (тип). @$#G - алиас.
TEST_F(MacroTest, GroupCount_CanonicalDotHash) {
    MacroPtr macro = std::make_shared<Macro>(m_ctx);
    ASSERT_NO_THROW(Parse("@@ cnt ( ... ): $... { @@ @$1.# @$2.# @@@@;", macro));
    ASSERT_NO_THROW(Parse("@cnt ( a, b ): Int32 { body }", macro));
    // остаток `body }` невалиден как код, но раскрытие видно: "@$1.# @$2.#" -> "2 1"
    EXPECT_NE(std::string(LexOut()).find("2 1"), std::string::npos) << LexOut();
}
