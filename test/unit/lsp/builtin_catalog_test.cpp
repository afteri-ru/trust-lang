// -----------------------------------------------------------------------
// Unit tests for BuiltinCatalog — глобальный каталог встроенных имён LSP
// -----------------------------------------------------------------------

#include "lsp/builtin_catalog.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>

using trust::BuiltinCatalog;

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

// Встроенный тип несёт свои методы (методы функции — true, %-префикс у нативных).
TEST_F(BuiltinCatalogTest, StrCharHasMethods) {
    auto it = cat.types().find("StrChar");
    ASSERT_TRUE(it != cat.types().end());
    for (const char* m : {"c_str", "size", "length", "empty", "data"}) {
        EXPECT_TRUE(it->second.methods.count(m) != 0) << "missing StrChar method: " << m;
        EXPECT_TRUE(it->second.methods.at(m)) << "expected function method: " << m;
    }
}

// Алиасы нативных методов попадают в список имён для экспорта в LSP: каталог `Range`
// содержит trust-имена методов, включая алиас `length` (нативное имя — `count`), нативные
// `%size`/`%length` (в каталоге — как `size`/`length`) и обычные методы.
TEST_F(BuiltinCatalogTest, RangeHasMethodsWithAlias) {
    auto it = cat.types().find("Range");
    ASSERT_TRUE(it != cat.types().end()) << "Range must be in the builtin catalog";
    // Обычные и нативные методы + алиас `length` (синоним `count`).
    for (const char* m : {"count", "size", "length", "empty", "at", "start", "contains", "toVector", "toDict"}) {
        EXPECT_TRUE(it->second.methods.count(m) != 0) << "missing Range method: " << m;
        EXPECT_TRUE(it->second.methods.at(m)) << "expected function method: " << m;
    }
}

// Каталог — только встроенные (userDefined всегда false).
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

// Встроенные DSL-макросы загружены.
TEST_F(BuiltinCatalogTest, HasDslMacros) {
    EXPECT_FALSE(cat.dslMacros().empty());
}

} // namespace
