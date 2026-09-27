// test/unit/packages/model_test.cpp - module path, source tree, package model and placement.
#include "packages/errors.hpp"
#include "packages/locate.hpp"
#include "packages/module_path.hpp"
#include "packages/package.hpp"
#include "packages/source_tree.hpp"

#include "test_data.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
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

const PackageEntry* findEntry(const Package& pkg, const std::string& path) {
    for (const PackageEntry& entry : pkg.entries) {
        if (entry.path == path) {
            return &entry;
        }
    }
    return nullptr;
}

std::string errorCode(const std::function<void()>& fn) {
    try {
        fn();
    } catch (const PackageException& e) {
        return e.error().code;
    }
    return {};
}

} // namespace

// ---------------------------------------------------------------------------
// Module path
// ---------------------------------------------------------------------------

TEST(PackagesModulePath, NormalizesSeparators) {
    EXPECT_EQ(normalizeModulePath("a/b"), "a/b");
    EXPECT_EQ(normalizeModulePath("a\\b"), "a/b");
    EXPECT_EQ(normalizeModulePath("a\\b\\c"), "a/b/c");
}

TEST(PackagesModulePath, RejectsInvalidPaths) {
    for (const char* bad : {"", "a//b", "/a", "a/../b", "./a", "a/", ".."}) {
        EXPECT_THROW(normalizeModulePath(bad), PackageException) << "input: " << bad;
    }
}

// ---------------------------------------------------------------------------
// DirectoryTree
// ---------------------------------------------------------------------------

TEST(PackagesSourceTree, DirectoryListingAndRead) {
    const fs::path dir = trust::test::makeTestDataDir("packages_tree");
    writeFile(dir / "a.src", "A");
    writeFile(dir / "sub" / "b.src", "B");
    writeFile(dir / "trust.pkg.json", "{}\n");
    writeFile(dir / "README.md", "docs\n");

    const DirectoryTree tree(dir);
    EXPECT_TRUE(tree.isRegularFile("a.src"));
    EXPECT_TRUE(tree.isDirectory("sub"));
    EXPECT_FALSE(tree.isDirectory("a.src"));
    EXPECT_TRUE(tree.isRegularFile("trust.pkg.json"));
    EXPECT_EQ(tree.read("sub/b.src"), "B");

    const std::vector<std::string> expected{"README.md", "a.src", "sub/b.src", "trust.pkg.json"};
    EXPECT_EQ(tree.listRecursive(), expected);
}

TEST(PackagesSourceTree, RejectsEscapingPaths) {
    const fs::path dir = trust::test::makeTestDataDir("packages_tree_safe");
    const DirectoryTree tree(dir);
    EXPECT_EQ(errorCode([&] { (void)tree.isRegularFile("../x"); }), "packages.tree.unsafe");
    EXPECT_EQ(errorCode([&] { (void)tree.read("/etc/passwd"); }), "packages.tree.unsafe");
}

// ---------------------------------------------------------------------------
// Package boundaries and discovery
// ---------------------------------------------------------------------------

TEST(PackagesModel, DetectsBoundariesAndNestedPackages) {
    const fs::path dir = trust::test::makeTestDataDir("packages_model");
    writeFile(dir / "trust.pkg.json", "{}\n");
    writeFile(dir / "a.src", "A");
    writeFile(dir / "sub" / "b.src", "B");                // plain subdir - stays in the same package
    writeFile(dir / "nested" / "trust.pkg.json", "{}\n"); // nested package
    writeFile(dir / "nested" / "c.src", "C");

    const DirectoryTree tree(dir);
    EXPECT_TRUE(isPackageRoot(tree, ""));
    EXPECT_TRUE(isPackageRoot(tree, "nested"));
    EXPECT_FALSE(isPackageRoot(tree, "sub"));

    const Package pkg = discoverPackage(tree, "");
    ASSERT_EQ(pkg.entries.size(), 3u);

    const PackageEntry* a = findEntry(pkg, "a");
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(a->kind, EntryKind::Source);
    EXPECT_EQ(a->relative, "a.src");

    const PackageEntry* b = findEntry(pkg, "sub/b");
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(b->kind, EntryKind::Source);

    const PackageEntry* nested = findEntry(pkg, "nested");
    ASSERT_NE(nested, nullptr);
    EXPECT_EQ(nested->kind, EntryKind::Package);

    const Package nestedPkg = discoverPackage(tree, "nested");
    EXPECT_EQ(nestedPkg.root, "nested");
    const PackageEntry* c = findEntry(nestedPkg, "c");
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->relative, "nested/c.src");
}

// ---------------------------------------------------------------------------
// Placement
// ---------------------------------------------------------------------------

TEST(PackagesLocate, ResolvesModules) {
    const fs::path dir = trust::test::makeTestDataDir("packages_locate");
    writeFile(dir / "trust.pkg.json", "{}\n");
    writeFile(dir / "a.src", "A");
    writeFile(dir / "sub" / "b.src", "B");
    writeFile(dir / "nested" / "trust.pkg.json", "{}\n");
    writeFile(dir / "nested" / "c.src", "C");
    writeFile(dir / "pkg.trust", "stub");
    writeFile(dir / "comp.pcp", "stub");

    const DirectoryTree tree(dir);
    EXPECT_EQ(locate(tree, "", "a").relative, "a.src");
    EXPECT_EQ(locate(tree, "", "sub/b").relative, "sub/b.src");
    EXPECT_EQ(locate(tree, "", "sub\\b").relative, "sub/b.src"); // source-level separator
    EXPECT_EQ(locate(tree, "", "nested/c").relative, "nested/c.src");
    EXPECT_EQ(locate(tree, "", "pkg").kind, EntryKind::Package);
    EXPECT_EQ(locate(tree, "", "nested").kind, EntryKind::Package);
    // Precompiled package (`.pcp`) is a package archive, like `.trust`.
    EXPECT_EQ(locate(tree, "", "comp").relative, "comp.pcp");
    EXPECT_EQ(locate(tree, "", "comp").kind, EntryKind::Package);
}

TEST(PackagesModel, DiscoversPrecompiledPackageArchive) {
    const fs::path dir = trust::test::makeTestDataDir("packages_pcp_model");
    writeFile(dir / "trust.pkg.json", "{}\n");
    writeFile(dir / "a.src", "A");
    writeFile(dir / "a.cppt", "C++");
    writeFile(dir / "a.hppt", "hdr");
    writeFile(dir / "a.o", "obj");
    writeFile(dir / "dep.pcp", "stub");

    const DirectoryTree tree(dir);
    const Package pkg = discoverPackage(tree, "");
    // The `.pcp` is a nested package entry; generated artifacts are not package entries.
    ASSERT_EQ(pkg.entries.size(), 2u);
    const PackageEntry* dep = findEntry(pkg, "dep");
    ASSERT_NE(dep, nullptr);
    EXPECT_EQ(dep->kind, EntryKind::Package);
    EXPECT_EQ(dep->relative, "dep.pcp");
    EXPECT_EQ(findEntry(pkg, "a")->kind, EntryKind::Source);
}

TEST(PackagesLocate, AmbiguousSourceAndPcpCandidatesFail) {
    const fs::path dir = trust::test::makeTestDataDir("packages_locate_src_pcp");
    writeFile(dir / "z.src", "src");
    writeFile(dir / "z.pcp", "archive");
    const DirectoryTree tree(dir);
    EXPECT_EQ(errorCode([&] { (void)locate(tree, "", "z"); }), "packages.locate.ambiguous");
}

TEST(PackagesLocate, MissingModuleFails) {
    const fs::path dir = trust::test::makeTestDataDir("packages_locate_missing");
    const DirectoryTree tree(dir);
    EXPECT_EQ(errorCode([&] { (void)locate(tree, "", "nope"); }), "packages.locate.missing");
}

TEST(PackagesLocate, AmbiguousCandidatesFail) {
    const fs::path dir = trust::test::makeTestDataDir("packages_locate_ambiguous");
    writeFile(dir / "x.src", "src");
    writeFile(dir / "x.trust", "archive");
    const DirectoryTree tree(dir);
    EXPECT_EQ(errorCode([&] { (void)locate(tree, "", "x"); }), "packages.locate.ambiguous");

    const fs::path dir2 = trust::test::makeTestDataDir("packages_locate_ambiguous_dir");
    writeFile(dir2 / "y.src", "src");
    writeFile(dir2 / "y" / "z.src", "z");
    const DirectoryTree tree2(dir2);
    EXPECT_EQ(errorCode([&] { (void)locate(tree2, "", "y"); }), "packages.locate.ambiguous");
}
