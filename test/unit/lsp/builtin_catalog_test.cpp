// -----------------------------------------------------------------------
// Unit tests for BuiltinCatalog - глобальный каталог встроенных имён LSP
// -----------------------------------------------------------------------

#include "lsp/builtin_catalog.h"

#include "syntax/macro.h"
#include "syntax/parser.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <string>

using trust::BuiltinCatalog;
using trust::Context;
using trust::Macro;
using trust::Parser;

namespace {

class BuiltinCatalogTest : public ::testing::Test {
  protected:
    const BuiltinCatalog& cat = BuiltinCatalog::instance();
};

// Встроенные типы присутствуют в каталоге (общее иммутабельное ядро).
TEST_F(BuiltinCatalogTest, ContainsBuiltinTypes) {
    ASSERT_FALSE(cat.types().empty());
    for (const char* name : {"Int32", "StrChar", "Bool", "Dict", "Rational"}) {
        EXPECT_TRUE(cat.types().count(name) != 0) << "missing builtin type: " << name;
    }
}

// Встроенный тип несёт свои методы (методы функции - true, %-префикс у нативных).
TEST_F(BuiltinCatalogTest, StrCharHasMethods) {
    auto it = cat.types().find("StrChar");
    ASSERT_TRUE(it != cat.types().end());
    for (const char* m : {"c_str", "size", "length", "empty", "data"}) {
        EXPECT_TRUE(it->second.methods.count(m) != 0) << "missing StrChar method: " << m;
        EXPECT_TRUE(it->second.methods.at(m)) << "expected function method: " << m;
    }
}

// Алиасы нативных методов попадают в список имён для экспорта в LSP: каталог `Range`
// содержит trust-имена методов, включая алиас `length` (нативное имя - `count`), нативные
// `%size`/`%length` (в каталоге - как `size`/`length`) и обычные методы.
TEST_F(BuiltinCatalogTest, RangeHasMethodsWithAlias) {
    auto it = cat.types().find("Range");
    ASSERT_TRUE(it != cat.types().end()) << "Range must be in the builtin catalog";
    // Обычные и нативные методы + алиас `length` (синоним `count`).
    for (const char* m : {"count", "size", "length", "empty", "at", "start", "contains", "toVector", "toDict"}) {
        EXPECT_TRUE(it->second.methods.count(m) != 0) << "missing Range method: " << m;
        EXPECT_TRUE(it->second.methods.at(m)) << "expected function method: " << m;
    }
}

// Каталог - только встроенные (userDefined всегда false).
TEST_F(BuiltinCatalogTest, OnlyBuiltin) {
    for (const auto& [name, info] : cat.types()) {
        (void)name;
        EXPECT_FALSE(info.userDefined) << "catalog must contain only builtin types";
    }
}

// Предопределённые макросы парсера доступны из каталога.
TEST_F(BuiltinCatalogTest, HasPredefMacros) {
    ASSERT_FALSE(cat.predefMacros().empty());
    EXPECT_NE(std::find(cat.predefMacros().begin(), cat.predefMacros().end(), "@__FILE__"), cat.predefMacros().end());
}

// Документирующие комментарии предопределённых @__...__ макросов доступны для hover/доков.
// Единое хранилище macroDocs(): ключ = первый терм без '@' (__FILE__, __LINE__, ...).
TEST_F(BuiltinCatalogTest, HasPredefMacroDocs) {
    const auto& docs = cat.macroDocs();
    ASSERT_FALSE(docs.empty());
    for (const char* name : {"__FILE__", "__LINE__", "__COUNTER__", "__TRUST_VERSION_MAJOR__"}) {
        auto it = docs.find(name);
        EXPECT_TRUE(it != docs.end() && !it->second.empty()) << "missing predef macro doc: " << name;
    }
}

// Прагма-макросы (@__OPTION_*, @__HYGIENIC__, @__PRAGMA_*) НЕ входят в реестр предопределённых
// макросов (PredefMacroResolver::pragmaMacroNames), но должны давать hover-справку (доки в
// macroDocs()) и появляться в автодополнении (predefMacros()).
// Доки прагма-макросов сидируются в ветках PragmaEvaluator::pragmaEval (Context::addMacroDoc) -
// при ИСПОЛЬЗОВАНИИ прагмы (addMacroDoc стоит в начале каждой ветки, до проверки аргументов).
// Поэтому прогоняем каждую ветку отдельным Parser (try/catch - часть прагм даёт
// диагностики/исключения по аргументам).
TEST_F(BuiltinCatalogTest, HasPragmaMacroDocs) {
    const char* kPragmas[] = {
        "@__OPTION_PUSH__;",
        "@__OPTION_POP__;",
        "@__OPTION__(\"Wno-foo\",\"ignore\");",
        "@__OPTION_TRUE__(\"Wno-foo\" 1);", // старая форма без запятой
        "@__OPTION_FALSE__(\"Wno-foo\");",
        "@__OPTION_IIF__(\"Wno-foo\" 1 2);",
        "@__HYGIENIC__(x);",
        "@__PRAGMA_MESSAGE__(\"a\");",
        "@__PRAGMA_WARNING__(\"a\");",
        "@__PRAGMA_ERROR__(\"a\");",
        "@__PRAGMA_EXPECTED__(\"foo\");",
        "@__PRAGMA_DOC__(\"@__LINE__\",\"d\");",
    };
    for (const char* src : kPragmas) {
        trust::Context ctx(".");
        auto macro = std::make_shared<Macro>(ctx);
        ctx.setMacro(macro);
        Parser p(ctx);
        try {
            p.ParseText(src, "@t");
        } catch (const trust::ParserError&) {
            // Диагностика по аргументам прагмы не должна мешать сидированию дока.
        }
    }
    const auto& docs = Context::macroDocs();
    for (const char* name : {"__OPTION__", "__OPTION_PUSH__", "__OPTION_POP__", "__OPTION_TRUE__", "__OPTION_FALSE__", "__OPTION_IIF__", "__HYGIENIC__",
                             "__PRAGMA_MESSAGE__", "__PRAGMA_WARNING__", "__PRAGMA_ERROR__", "__PRAGMA_EXPECTED__", "__PRAGMA_DOC__"}) {
        auto it = docs.find(name);
        EXPECT_TRUE(it != docs.end() && !it->second.empty()) << "missing pragma macro doc: " << name;
        EXPECT_NE(std::find(cat.predefMacros().begin(), cat.predefMacros().end(), std::string("@").append(name)), cat.predefMacros().end())
            << "pragma macro missing from completion names: @__" << name << "__";
    }
}

// Встроенные DSL-макросы загружены.
TEST_F(BuiltinCatalogTest, HasDslMacros) {
    EXPECT_FALSE(cat.dslMacros().empty());
    // Мнемоническая команда `macro` присутствует.
    EXPECT_NE(cat.dslMacros().find("macro"), cat.dslMacros().end());
}

// Документирующие комментарии DSL-макросов доступны для подсказки в IDE: `macro`/`func`
// несут непустой док (из единого хранилища macroDocs()).
TEST_F(BuiltinCatalogTest, HasDslMacroDocs) {
    const auto& docs = cat.macroDocs();
    ASSERT_FALSE(docs.empty());
    for (const char* name : {"macro", "func"}) {
        auto it = docs.find(name);
        EXPECT_TRUE(it != docs.end() && !it->second.empty()) << "missing DSL macro doc: " << name;
    }
}

// Прагма @__PRAGMA_DOC__ переопределяет док СУЩЕСТВУЮЩЕГО макроса в едином хранилище
// (ключ нормализуется - с '@' и без). Неизвестный макрос -> error, не записывается.
TEST_F(BuiltinCatalogTest, PragmaDocOverridesExistingAndErrorsUnknown) {
    trust::Context ctx(".");
    auto macro = std::make_shared<Macro>(ctx);
    ctx.setMacro(macro);
    Parser p(ctx);
    // Существующий предdef-дефолт переопределяется (с '@' и без - ключ один).
    // Неизвестный макрос -> error. Оба в одном ParseText (см. заметку про повторный ParseText).
    ASSERT_NO_THROW(p.ParseText("@__PRAGMA_DOC__(\"@__LINE__\", \"Номер строки в текущем файле\"); @__PRAGMA_DOC__(\"no_such_macro\", \"текст\");", "@t"));
    const std::string* line = Context::macroDoc("__LINE__");   // без '@'
    const std::string* line2 = Context::macroDoc("@__LINE__"); // с '@' - тот же ключ
    ASSERT_TRUE(line != nullptr);
    ASSERT_TRUE(line2 != nullptr);
    EXPECT_EQ(line, line2);
    EXPECT_EQ("Номер строки в текущем файле", *line);
    // Неизвестный макрос: дока нет, диагностика error выдана.
    EXPECT_EQ(nullptr, Context::macroDoc("no_such_macro"));
    EXPECT_GT(ctx.diag().errorCount(), 0);
}

} // namespace
