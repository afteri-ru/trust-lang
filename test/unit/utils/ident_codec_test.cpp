// test/unit/utils/ident_codec_test.cpp
// R5: ЕДИНЫЙ обратимый кодек C++-идентификатора (utils::encodeIdent/decodeIdent).
// Для ASCII-имени — тождество; для прочего (неидентификаторные символы, начало с маркера,
// C++-ключевые слова) — `_u_`-маркер; второй канонизатор имени не вводится.
#include "utils/strings/strings_names.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace trust {
namespace {

using trust::utils::decodeIdent;
using trust::utils::encodeIdent;

TEST(IdentCodec, AsciiIdentity) {
    for (const char* n : {"abc", "a1_b", "Z"}) {
        EXPECT_EQ(encodeIdent(n), n) << n;
        EXPECT_EQ(decodeIdent(n), n) << n;
    }
    EXPECT_EQ(encodeIdent(""), "");
    EXPECT_EQ(decodeIdent(""), "");
}

TEST(IdentCodec, NonAsciiAndSpecialRoundsTrip) {
    // Неплейн-символы, начало с маркера, C++-ключевое слово — кодируются и обратимы.
    const std::vector<std::string> names = {"$x", "a-b", "имя", "_u_escape", "class", "with space", "3d"};
    for (const std::string& n : names) {
        const std::string enc = encodeIdent(n);
        EXPECT_EQ(decodeIdent(enc), n) << n << " -> " << enc;
    }
    // Неплейн-имя действительно кодируется (не остаётся как есть) и несёт маркер `_u_`.
    EXPECT_NE(encodeIdent("$x"), "$x");
    EXPECT_EQ(encodeIdent("$x").rfind("_u_", 0), 0u);
}

} // namespace
} // namespace trust
