// test/unit/packages/archive_test.cpp - `.trust` pack/unpack and archive-backed trees.
#include "packages/archive.hpp"
#include "packages/errors.hpp"
#include "packages/locate.hpp"
#include "packages/package.hpp"
#include "packages/source_tree.hpp"

#include "test_data.hpp"
#include "utils/hash.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace trust::packages;

namespace {

void writeFile(const fs::path& path, const std::string& data) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << data;
}

/// Builds a small package tree with a nested package and returns its root dir.
fs::path makeTree(const char* name) {
    const fs::path dir = trust::test::makeTestDataDir(name);
    writeFile(dir / "trust.pkg.json", "{\"schema\":1,\"name\":\"acme/foo\",\"version\":\"1.0.0\"}\n");
    writeFile(dir / "README.md", "package\n");
    writeFile(dir / "main.src", "main\n");
    writeFile(dir / "src" / "util.src", "util\n");
    writeFile(dir / "nested" / "trust.pkg.json", "{}\n");
    writeFile(dir / "nested" / "n.src", "nested\n");
    return dir;
}

} // namespace

TEST(PackagesArchive, PackIsDeterministicAndListsEntries) {
    const fs::path dir = makeTree("packages_arch");
    const fs::path out = trust::test::makeTestDataDir("packages_arch_out");
    const fs::path a = out / "a.trust";
    const fs::path b = out / "b.trust";

    const std::string sha_a = packArchive(dir, a);
    const std::string sha_b = packArchive(dir, b);
    EXPECT_EQ(sha_a, sha_b);
    EXPECT_EQ(sha_a, trust::utils::sha256File(a));

    std::vector<std::string> entries = listEntries(a);
    std::vector<std::string> sorted = entries;
    std::sort(sorted.begin(), sorted.end());
    EXPECT_EQ(entries, sorted); // archive order is the sorted entry order

    const std::vector<std::string> expected{"README.md", "main.src", "nested/n.src", "nested/trust.pkg.json", "src/util.src", "trust.pkg.json"};
    EXPECT_EQ(entries, expected);

    EXPECT_EQ(readArchiveEntry(a, "src/util.src"), "util\n");
}

TEST(PackagesArchive, RoundTripsThroughUnpack) {
    const fs::path dir = makeTree("packages_arch_roundtrip");
    const fs::path out = trust::test::makeTestDataDir("packages_arch_roundtrip_out");
    const fs::path archive = out / "pkg.trust";
    packArchive(dir, archive);

    const fs::path dest = out / "extracted";
    unpackArchive(archive, dest);
    EXPECT_TRUE(fs::is_regular_file(dest / "trust.pkg.json"));
    EXPECT_TRUE(fs::is_regular_file(dest / "main.src"));
    EXPECT_TRUE(fs::is_regular_file(dest / "src" / "util.src"));
    EXPECT_TRUE(fs::is_regular_file(dest / "nested" / "trust.pkg.json"));
    EXPECT_EQ(readArchiveEntry(archive, "main.src"), "main\n");
}

TEST(PackagesArchive, ArchiveTreeMatchesDirectoryView) {
    const fs::path dir = makeTree("packages_arch_tree");
    const fs::path out = trust::test::makeTestDataDir("packages_arch_tree_out");
    const fs::path archive = out / "pkg.trust";
    packArchive(dir, archive);

    const DirectoryTree directory(dir);
    const ArchiveTree packed(archive);

    EXPECT_EQ(directory.listRecursive(), packed.listRecursive());
    EXPECT_TRUE(packed.isRegularFile("main.src"));
    EXPECT_TRUE(packed.isDirectory("src"));
    EXPECT_FALSE(packed.isDirectory("main.src"));
    EXPECT_EQ(packed.read("src/util.src"), "util\n");
    EXPECT_TRUE(isPackageRoot(packed, ""));
    EXPECT_TRUE(isPackageRoot(packed, "nested"));

    EXPECT_EQ(locate(packed, "", "main").relative, "main.src");
    EXPECT_EQ(locate(packed, "", "src/util").relative, "src/util.src");
    EXPECT_EQ(locate(packed, "nested", "n").relative, "nested/n.src");

    const Package pkg = discoverPackage(packed, "");
    ASSERT_EQ(pkg.entries.size(), 3u); // main, src/util, nested
    const Package nested = discoverPackage(packed, "nested");
    EXPECT_EQ(nested.root, "nested");
    EXPECT_EQ(nested.entries.size(), 1u); // nested/n.src
}

TEST(PackagesArchive, MissingEntryAndModuleFail) {
    const fs::path dir = makeTree("packages_arch_missing");
    const fs::path out = trust::test::makeTestDataDir("packages_arch_missing_out");
    const fs::path archive = out / "pkg.trust";
    packArchive(dir, archive);

    EXPECT_THROW(readArchiveEntry(archive, "does/not/exist"), PackageException);
    const ArchiveTree tree(archive);
    EXPECT_THROW(locate(tree, "", "does/not/exist"), PackageException);
}

TEST(PackagesArchive, MissingArchiveFails) {
    const fs::path out = trust::test::makeTestDataDir("packages_arch_absent");
    EXPECT_THROW(ArchiveTree(out / "absent.trust"), PackageException);
    EXPECT_THROW(listEntries(out / "absent.trust"), PackageException);
}

TEST(PackagesArchive, DoesNotEmbedItsOwnOutput) {
    const fs::path dir = makeTree("packages_arch_self");
    const fs::path archive = dir / "self.trust"; // the output lives INSIDE the packed tree
    packArchive(dir, archive);

    const std::vector<std::string> entries = listEntries(archive);
    EXPECT_EQ(std::find(entries.begin(), entries.end(), std::string("self.trust")), entries.end());
    EXPECT_EQ(readArchiveEntry(archive, "main.src"), "main\n");
}
