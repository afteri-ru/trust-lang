// Кодирование логического пути файла в идентификатор (@__MODULE_NAME__).
// Инвариант: кодирование инъективно — разные логические пути дают разные имена, иначе производные
// C++-идентификаторы (entry-функция и любые имена, построенные из макроса) могли бы совпасть.

#include "syntax/util/warning_push.h"
#include <gtest/gtest.h>
#include "syntax/util/warning_pop.h"

#include "syntax/module_header.hpp"

namespace {

using trust::encodeModuleNameForIdent;

TEST(ModuleNameEncoding, EncodesScopeKind) {
    // Однокомпонентное имя (stem/\file) — как есть; тип scope входит в имя составных путей.
    EXPECT_EQ("hello", encodeModuleNameForIdent("\\hello"));
    EXPECT_EQ("m_util_string", encodeModuleNameForIdent("\\\\util\\string"));
    EXPECT_EQ("p_tensor_math_matrix", encodeModuleNameForIdent("\\\\\\tensor\\math\\matrix"));
    // Проектный и пакетный пути с одинаковым остатком не совпадают.
    EXPECT_NE(encodeModuleNameForIdent("\\\\a\\b"), encodeModuleNameForIdent("\\\\\\a\\b"));
}

TEST(ModuleNameEncoding, EscapesUnderscore) {
    EXPECT_EQ("m_util_open__file", encodeModuleNameForIdent("\\\\util\\open_file"));
}

TEST(ModuleNameEncoding, InjectiveForCollisionPronePaths) {
    // До введения экранирования оба пути давали одно и то же имя `a_b_c`.
    EXPECT_NE(encodeModuleNameForIdent("\\\\a\\b_c"), encodeModuleNameForIdent("\\\\a_b\\c"));
    EXPECT_EQ("m_a_b__c", encodeModuleNameForIdent("\\\\a\\b_c"));
    EXPECT_EQ("m_a__b_c", encodeModuleNameForIdent("\\\\a_b\\c"));
}

TEST(ModuleNameEncoding, DecodingIsUnambiguous) {
    // Обратное преобразование после снятия ТЕГА SCOPE: `__` -> '_', `_` -> '\\'.
    for (const char* path : {"\\hello", "\\\\a\\b_c", "\\\\a_b\\c", "\\\\\\pkg\\m\\f_g", "\\\\x\\a_b_c"}) {
        const std::string encoded = encodeModuleNameForIdent(path);
        std::string_view expected(path);
        expected.remove_prefix(expected.find_first_not_of('\\'));
        std::string_view body = encoded;
        if (body.starts_with("p_") || body.starts_with("m_")) {
            body.remove_prefix(2);
        }
        std::string decoded;
        for (std::size_t i = 0; i < body.size(); ++i) {
            if (body[i] == '_' && i + 1 < body.size() && body[i + 1] == '_') {
                decoded.push_back('_');
                ++i;
            } else if (body[i] == '_') {
                decoded.push_back('\\');
            } else {
                decoded.push_back(body[i]);
            }
        }
        EXPECT_EQ(expected, decoded) << "path=" << path;
    }
}

} // namespace
