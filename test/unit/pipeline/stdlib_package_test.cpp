// test/unit/pipeline/stdlib_package_test.cpp
// Тесты пакета ресурсов стандартной библиотеки (`share/trust/stdlib.trust`):
// поиск, чтение prelude-файлов без распаковки и полное извлечение с проверкой целостности.
#include "package/package.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "test_data.hpp"

namespace fs = std::filesystem;
using namespace trust;

namespace {

fs::path freshDir(const std::string& name) {
    return trust::test::makeTestDataDir(name);
}

} // namespace

TEST(StdlibPackage, IsLocatedInResourceDirectory) {
    const std::optional<fs::path> archive = locateResourcePackage("stdlib");
    ASSERT_TRUE(archive.has_value()) << "share/trust/stdlib.trust must be available";
    EXPECT_TRUE(fs::is_regular_file(*archive));
    EXPECT_TRUE(isTrustPackage(*archive));
}

TEST(StdlibPackage, InvalidPackageNameIsRejected) {
    EXPECT_FALSE(locateResourcePackage("../stdlib").has_value());
    EXPECT_FALSE(locateResourcePackage("").has_value());
    EXPECT_FALSE(locateResourcePackage("not a package").has_value());
}

TEST(StdlibPackage, PreludeFilesAreReadableWithoutExtraction) {
    const auto dsl = readResourcePackageEntry("stdlib", "stdlib/dsl.src");
    ASSERT_TRUE(dsl.has_value()) << "stdlib/dsl.src must be readable from the package";
    EXPECT_NE(dsl->find("assert"), std::string::npos) << "dsl.src must define the `assert` mnemonic";

    const auto iterator = readResourcePackageEntry("stdlib", "stdlib/iterator.src");
    ASSERT_TRUE(iterator.has_value());
    EXPECT_NE(iterator->find("Iterator"), std::string::npos);
    EXPECT_NE(iterator->find("@stdlib/iterator.hpp"), std::string::npos);

    const auto strings = readResourcePackageEntry("stdlib", "stdlib/builtin/string.src");
    ASSERT_TRUE(strings.has_value());
    EXPECT_NE(strings->find("StrChar = %std::string"), std::string::npos);
}

TEST(StdlibPackage, MissingEntryIsAnError) {
    const auto archive = locateResourcePackage("stdlib");
    ASSERT_TRUE(archive.has_value());
    std::string content;
    std::string error;
    EXPECT_FALSE(readPackageEntry(*archive, "stdlib/does_not_exist.src", content, error));
    EXPECT_FALSE(error.empty());
    EXPECT_FALSE(readPackageEntry(*archive, "../trust.toml", content, error));
}

TEST(StdlibPackage, ExtractAndVerifyProvidesHeaders) {
    const auto archive = locateResourcePackage("stdlib");
    ASSERT_TRUE(archive.has_value());

    const fs::path dest = freshDir("stdlib_package_extract");
    std::error_code ec;
    fs::remove_all(dest, ec);
    std::string error;
    ASSERT_TRUE(extractAndVerifyPackage(*archive, dest, error)) << error;

    EXPECT_TRUE(fs::is_regular_file(dest / "trust.toml"));
    EXPECT_TRUE(fs::is_regular_file(dest / "stdlib" / "dsl.src"));
    EXPECT_TRUE(fs::is_regular_file(dest / "include" / "trust" / "rational.hpp"));
    EXPECT_TRUE(fs::is_regular_file(dest / "include" / "trust" / "io.hpp"));
    EXPECT_TRUE(fs::is_regular_file(dest / "include" / "stdlib" / "iterator.hpp"));
    EXPECT_TRUE(fs::is_regular_file(dest / "include" / "stdlib" / "builtin" / "rational_openssl.hpp"));

    std::ifstream ifs(dest / "include" / "trust" / "rational.hpp", std::ios::binary);
    const std::string rational{std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>()};
    EXPECT_NE(rational.find("class Rational"), std::string::npos);
}
