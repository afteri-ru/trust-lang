// test/unit/pipeline/package_test.cpp
// Юнит-тесты манифеста пакета (trust.toml), создания (--package) и распаковки (--extract) пакета .trust.
#include "package/manifest.hpp"
#include "package/package.hpp"
#include "trust/version.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "test_data.hpp"

namespace fs = std::filesystem;
using namespace trust;

namespace {

fs::path freshDir(const std::string& name) {
    return trust::test::makeTestDataDir(name);
}

void writeFile(const fs::path& p, const std::string& content) {
    fs::create_directories(p.parent_path());
    std::ofstream ofs(p, std::ios::binary);
    ofs << content;
}

std::string readFile(const fs::path& p) {
    std::ifstream ifs(p, std::ios::binary);
    return std::string{std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>()};
}

int runShell(const std::string& cmd) {
    return std::system(cmd.c_str());
}

std::string captureShell(const std::string& cmd) {
    std::string out;
    FILE* pipe = ::popen(cmd.c_str(), "r");
    if (pipe == nullptr) {
        return out;
    }
    char buf[4096];
    std::size_t n = 0;
    while ((n = std::fread(buf, 1, sizeof(buf), pipe)) > 0) {
        out.append(buf, n);
    }
    ::pclose(pipe);
    return out;
}

/// Манифест пакета-каталога: `name` + (опционально) версия компилятора.
/// Версия проставляется упаковщиком, поэтому в тестах она совпадает с версией компилятора.
std::string manifestText(const std::string& name, bool with_version = true) {
    std::string text = "name = \"" + name + "\"\n";
    if (with_version) {
        text += "version = \"" + std::string(TRUST_VERSION_SHORT) + "\"\n";
    }
    return text;
}

/// Собрать пакет-каталог: манифест + исходники + прочие файлы + артефакты сборки.
fs::path makePackage(const fs::path& dir, const std::string& manifest) {
    writeFile(dir / "trust.toml", manifest);
    writeFile(dir / "mod_a.src", "a:Int32 := 1;\n");
    writeFile(dir / "sub" / "mod_b.src", "b:Int32 := 2;\n");
    writeFile(dir / "notes.txt", "notes\n");
    writeFile(dir / "junk.cppt", "// artifact\n");
    writeFile(dir / "junk.src_map", "map\n");
    writeFile(dir / "junk.o", "obj\n");
    writeFile(dir / "junk.so", "so\n");
    writeFile(dir / "app", "bin\n");
    writeFile(dir / ".trust" / "leftover.txt", "artifact\n");
    return dir;
}

} // namespace

// -- Манифест: валидация имени/версии --

TEST(PackageManifest, ValidNames) {
    for (const char* n : {"a", "foo", "foo_bar", "a1", "pkg0_name"}) {
        EXPECT_TRUE(isValidPackageName(n)) << n;
    }
}

TEST(PackageManifest, InvalidNames) {
    for (const char* n : {"", "_x", "x_", "Foo", "foo-bar", "foo.bar", "fo o", "foo/bar"}) {
        EXPECT_FALSE(isValidPackageName(n)) << n;
    }
}

TEST(PackageManifest, ValidVersions) {
    for (const char* v : {"0.0.0", "1.2.3", "10.20.30"}) {
        EXPECT_TRUE(isValidVersion(v)) << v;
    }
}

TEST(PackageManifest, InvalidVersions) {
    for (const char* v : {"", "1", "1.2", "1.2.3.4", "a.b.c", "1..2", "1.2.", ".1.2", "1.2.3-rc1"}) {
        EXPECT_FALSE(isValidVersion(v)) << v;
    }
}

TEST(PackageManifest, ParseValid) {
    PackageManifest m;
    std::string err;
    ASSERT_TRUE(parseManifest(manifestText("pkg") + "description = \"d\"\n", m, err)) << err;
    EXPECT_EQ(m.name(), "pkg");
    EXPECT_EQ(m.version(), TRUST_VERSION_SHORT);
    ASSERT_TRUE(m.data["description"].value<std::string>().has_value());
    EXPECT_EQ(*m.data["description"].value<std::string>(), "d");
}

TEST(PackageManifest, ParseMissingName) {
    PackageManifest m;
    std::string err;
    EXPECT_FALSE(parseManifest("version = \"" + std::string(TRUST_VERSION_SHORT) + "\"\n", m, err));
    EXPECT_NE(err.find("missing string field 'name'"), std::string::npos);
}

TEST(PackageManifest, ParseMissingVersion) {
    PackageManifest m;
    std::string err;
    // Required (манифест готового пакета): version обязательна.
    EXPECT_FALSE(parseManifest("name = \"pkg\"\n", m, err));
    EXPECT_NE(err.find("missing string field 'version'"), std::string::npos);
}

TEST(PackageManifest, ParseNonStringName) {
    PackageManifest m;
    std::string err;
    EXPECT_FALSE(parseManifest("name = 123\nversion = \"" + std::string(TRUST_VERSION_SHORT) + "\"\n", m, err));
    EXPECT_NE(err.find("missing string field 'name'"), std::string::npos);
}

TEST(PackageManifest, ParseInvalidName) {
    PackageManifest m;
    std::string err;
    EXPECT_FALSE(parseManifest(manifestText("BadName"), m, err));
    EXPECT_NE(err.find("invalid package name"), std::string::npos);
}

TEST(PackageManifest, ParseInvalidVersion) {
    PackageManifest m;
    std::string err;
    EXPECT_FALSE(parseManifest("name = \"pkg\"\nversion = \"1\"\n", m, err));
    EXPECT_NE(err.find("invalid package version"), std::string::npos);
}

// Версия пакета — semver ПАКЕТА (не версия компилятора): произвольная корректная
// версия принимается, а отсутствие допустимо в манифесте-источнике.
TEST(PackageManifest, VersionIsIndependentOfCompiler) {
    PackageManifest m;
    std::string err;
    ASSERT_TRUE(parseManifest("name = \"pkg\"\nversion = \"999.999.999\"\n", m, err)) << err;
    EXPECT_EQ(m.version(), std::string_view("999.999.999"));

    // Optional: версия может отсутствовать (её проставит упаковщик как provenance).
    EXPECT_TRUE(parseManifest("name = \"pkg\"\n", m, err, ManifestVersion::Optional)) << err;
    EXPECT_TRUE(m.version().empty());
}

TEST(PackageManifest, ParseMalformedToml) {
    PackageManifest m;
    std::string err;
    EXPECT_FALSE(parseManifest("name = \"pkg\"\nversion =\n", m, err));
    EXPECT_NE(err.find("invalid TOML"), std::string::npos);
}

TEST(PackageManifest, ParseEmpty) {
    PackageManifest m;
    std::string err;
    EXPECT_FALSE(parseManifest("", m, err));
    EXPECT_NE(err.find("missing string field 'name'"), std::string::npos);
}

// -- Манифест: [[artifact]] и [[link.headers]] --

TEST(PackageManifest, ParseArtifactsAndHeaderLinks) {
    PackageManifest m;
    std::string err;
    const std::string text = manifestText("pkg") + "[[artifact]]\n"
                                                   "provides = [\"trust-runtime\"]\n"
                                                   "kind = \"toolchain\"\n"
                                                   "libs = [\"trust-runtime\"]\n"
                                                   "[[artifact]]\n"
                                                   "provides = [\"gmp\"]\n"
                                                   "kind = \"system\"\n"
                                                   "libs = [\"gmp\"]\n"
                                                   "[[link.headers]]\n"
                                                   "header = \"trust/rational.hpp\"\n"
                                                   "libs = [\"trust-runtime\", \"gmp\"]\n";
    ASSERT_TRUE(parseManifest(text, m, err)) << err;
    const auto artifacts = m.artifacts();
    ASSERT_EQ(artifacts.size(), 2U);
    EXPECT_EQ(artifacts[0].kind, ArtifactKind::Toolchain);
    EXPECT_EQ(artifacts[0].provides, std::vector<std::string>{"trust-runtime"});
    EXPECT_EQ(artifacts[0].libs, std::vector<std::string>{"trust-runtime"});
    EXPECT_EQ(artifacts[1].kind, ArtifactKind::System);
    const auto rules = m.headerLinks();
    ASSERT_EQ(rules.size(), 1U);
    EXPECT_EQ(rules[0].header, "trust/rational.hpp");
    EXPECT_EQ(rules[0].libs, (std::vector<std::string>{"trust-runtime", "gmp"}));
}

TEST(PackageManifest, ParsesArtifactRequires) {
    PackageManifest m;
    std::string err;
    const std::string text = manifestText("pkg") + "[[artifact]]\n"
                                                   "provides = [\"trust-runtime\"]\n"
                                                   "kind = \"toolchain\"\n"
                                                   "libs = [\"trust-runtime\"]\n"
                                                   "requires = [\"gmp\"]\n";
    ASSERT_TRUE(parseManifest(text, m, err)) << err;
    const auto artifacts = m.artifacts();
    ASSERT_EQ(artifacts.size(), 1U);
    EXPECT_EQ(artifacts[0].requiresLibs, std::vector<std::string>{"gmp"});
}

TEST(PackageManifest, RejectsArtifactRequiringItself) {
    PackageManifest m;
    std::string err;
    const std::string text = manifestText("pkg") + "[[artifact]]\n"
                                                   "provides = [\"rt\"]\n"
                                                   "kind = \"toolchain\"\n"
                                                   "libs = [\"rt\"]\n"
                                                   "requires = [\"rt\"]\n";
    EXPECT_FALSE(parseManifest(text, m, err));
    EXPECT_NE(err.find("its own logical library"), std::string::npos);
}

TEST(PackageManifest, RejectsEmptyArtifactRequiresEntry) {
    PackageManifest m;
    std::string err;
    const std::string text = manifestText("pkg") + "[[artifact]]\n"
                                                   "provides = [\"rt\"]\n"
                                                   "kind = \"toolchain\"\n"
                                                   "libs = [\"rt\"]\n"
                                                   "requires = [\"\"]\n";
    EXPECT_FALSE(parseManifest(text, m, err));
    EXPECT_NE(err.find("empty 'requires'"), std::string::npos);
}

TEST(PackageManifest, RejectsUnknownArtifactKind) {
    PackageManifest m;
    std::string err;
    const std::string text = manifestText("pkg") + "[[artifact]]\nprovides = [\"x\"]\nkind = \"embedded\"\nlibs = [\"x\"]\n";
    EXPECT_FALSE(parseManifest(text, m, err));
    EXPECT_NE(err.find("unknown or missing 'kind'"), std::string::npos);
}

TEST(PackageManifest, RejectsArtifactWithoutProvides) {
    PackageManifest m;
    std::string err;
    const std::string text = manifestText("pkg") + "[[artifact]]\nkind = \"toolchain\"\nlibs = [\"x\"]\n";
    EXPECT_FALSE(parseManifest(text, m, err));
    EXPECT_NE(err.find("'provides'"), std::string::npos);
}

TEST(PackageManifest, RejectsUnsafePackageLibraryPath) {
    PackageManifest m;
    std::string err;
    const std::string text = manifestText("pkg") + "[[artifact]]\nprovides = [\"x\"]\nkind = \"package\"\nlibs = [\"../libx.a\"]\n";
    EXPECT_FALSE(parseManifest(text, m, err));
    EXPECT_NE(err.find("must be relative without '..'"), std::string::npos);
}

TEST(PackageManifest, RejectsArtifactTableInsteadOfArray) {
    PackageManifest m;
    std::string err;
    const std::string text = manifestText("pkg") + "[artifact]\nprovides = [\"x\"]\nkind = \"system\"\nlibs = [\"x\"]\n";
    EXPECT_FALSE(parseManifest(text, m, err));
    EXPECT_NE(err.find("array of tables"), std::string::npos);
}

TEST(PackageManifest, RejectsHeaderRuleWithoutLibs) {
    PackageManifest m;
    std::string err;
    const std::string text = manifestText("pkg") + "[[link.headers]]\nheader = \"h.hpp\"\nlibs = []\n";
    EXPECT_FALSE(parseManifest(text, m, err));
    EXPECT_NE(err.find("'libs' must list at least one"), std::string::npos);
}

TEST(PackageManifest, ParseSourceArtifact) {
    PackageManifest m;
    std::string err;
    const std::string text = manifestText("pkg") + "[[artifact]]\n"
                                                   "provides = [\"third-party\"]\n"
                                                   "kind = \"source\"\n"
                                                   "sources = [\"lib/a.cpp\", \"lib/b.cpp\"]\n"
                                                   "link-options = [\"-pthread\"]\n";
    ASSERT_TRUE(parseManifest(text, m, err)) << err;
    const auto artifacts = m.artifacts();
    ASSERT_EQ(artifacts.size(), 1U);
    EXPECT_EQ(artifacts[0].kind, ArtifactKind::Source);
    EXPECT_EQ(artifacts[0].sources, (std::vector<std::string>{"lib/a.cpp", "lib/b.cpp"}));
    EXPECT_TRUE(artifacts[0].libs.empty());
    EXPECT_EQ(artifacts[0].linkOptions, std::vector<std::string>{"-pthread"});
}

TEST(PackageManifest, RejectsSourceWithoutSources) {
    PackageManifest m;
    std::string err;
    const std::string text = manifestText("pkg") + "[[artifact]]\nprovides = [\"x\"]\nkind = \"source\"\n";
    EXPECT_FALSE(parseManifest(text, m, err));
    EXPECT_NE(err.find("requires a non-empty 'sources'"), std::string::npos);
}

TEST(PackageManifest, RejectsSourceWithLibs) {
    PackageManifest m;
    std::string err;
    const std::string text = manifestText("pkg") + "[[artifact]]\nprovides = [\"x\"]\nkind = \"source\"\n"
                                                   "sources = [\"lib/a.cpp\"]\nlibs = [\"x\"]\n";
    EXPECT_FALSE(parseManifest(text, m, err));
    EXPECT_NE(err.find("must not declare 'libs'"), std::string::npos);
}

TEST(PackageManifest, RejectsSourceWithNonCppExtension) {
    PackageManifest m;
    std::string err;
    const std::string text = manifestText("pkg") + "[[artifact]]\nprovides = [\"x\"]\nkind = \"source\"\n"
                                                   "sources = [\"lib/a.c\"]\n";
    EXPECT_FALSE(parseManifest(text, m, err));
    EXPECT_NE(err.find("must have a .cpp extension"), std::string::npos);
}

TEST(PackageManifest, RejectsSourcesOnPrebuiltArtifact) {
    PackageManifest m;
    std::string err;
    const std::string text = manifestText("pkg") + "[[artifact]]\nprovides = [\"x\"]\nkind = \"toolchain\"\n"
                                                   "sources = [\"lib/a.cpp\"]\nlibs = [\"x\"]\n";
    EXPECT_FALSE(parseManifest(text, m, err));
    EXPECT_NE(err.find("only valid for kind=\"source\""), std::string::npos);
}

// -- Манифест: файловый round-trip --

TEST(PackageManifest, FileRoundTrip) {
    fs::path dir = freshDir("pkg_manifest_file");
    PackageManifest m;
    std::string err;
    ASSERT_TRUE(parseManifest(manifestText("pkg"), m, err)) << err;
    ASSERT_TRUE(writeManifestFile(dir / "trust.toml", m, err)) << err;
    PackageManifest loaded;
    ASSERT_TRUE(parseManifestFile(dir / "trust.toml", loaded, err)) << err;
    EXPECT_EQ(loaded.name(), "pkg");
    EXPECT_EQ(loaded.version(), TRUST_VERSION_SHORT);
}

TEST(PackageManifest, RejectsInvalidLayout) {
    PackageManifest m;
    std::string err;
    EXPECT_FALSE(parseManifest(manifestText("pkg") + "layout = \"bad\"\n", m, err));
    EXPECT_NE(err.find("'layout' must be a table"), std::string::npos) << err;

    EXPECT_FALSE(parseManifest(manifestText("pkg") + "[layout]\nlayers = \"bad\"\n", m, err));
    EXPECT_NE(err.find("'layout.layers' must be an array of tables"), std::string::npos) << err;

    EXPECT_FALSE(parseManifest(manifestText("pkg") + "[layout]\nlayers = []\n", m, err));
    EXPECT_NE(err.find("'layout.layers' must not be empty"), std::string::npos) << err;

    EXPECT_FALSE(parseManifest(manifestText("pkg") + "[[layout.layers]]\nsource-dir = 1\npatterns = [\"*.src\"]\n", m, err));
    EXPECT_NE(err.find("'source-dir' must be a non-empty string"), std::string::npos) << err;

    EXPECT_FALSE(parseManifest(manifestText("pkg") + "[[layout.layers]]\npatterns = \"*.src\"\n", m, err));
    EXPECT_NE(err.find("'patterns' must be a non-empty array of strings"), std::string::npos) << err;

    EXPECT_FALSE(parseManifest(manifestText("pkg") + "[[layout.layers]]\npatterns = [\"*.src\", 1]\n", m, err));
    EXPECT_NE(err.find("patterns[1] must be a non-empty string"), std::string::npos) << err;
}

TEST(PackageManifest, ParseFileMissing) {
    fs::path dir = freshDir("pkg_manifest_missing");
    PackageManifest m;
    std::string err;
    EXPECT_FALSE(parseManifestFile(dir / "nope.toml", m, err));
    EXPECT_NE(err.find("cannot read manifest file"), std::string::npos);
}

// -- Создание пакета (--package) --

TEST(PackageCreate, ProducesArchiveWithBuildBlock) {
    fs::path dir = makePackage(freshDir("pkg_create_ok"), manifestText("demo") + "description = \"D\"\n");
    fs::path out = dir.parent_path() / "demo_out.trust";
    fs::remove(out);
    ASSERT_EQ(createTrustPackage(dir, out), 0);
    ASSERT_TRUE(fs::is_regular_file(out));

    const std::string list = captureShell("tar tzf '" + out.string() + "'");
    EXPECT_NE(list.find("./trust.toml"), std::string::npos);
    EXPECT_NE(list.find("./mod_a.src"), std::string::npos);
    EXPECT_NE(list.find("./sub/mod_b.src"), std::string::npos);
    EXPECT_NE(list.find("./notes.txt"), std::string::npos);
    EXPECT_EQ(list.find("junk.cppt"), std::string::npos);
    EXPECT_EQ(list.find("junk.src_map"), std::string::npos);
    EXPECT_EQ(list.find("junk.o"), std::string::npos);
    EXPECT_EQ(list.find("junk.so"), std::string::npos);
    EXPECT_EQ(list.find("app"), std::string::npos);
    EXPECT_EQ(list.find("./.trust/"), std::string::npos);

    const std::string mf = captureShell("tar xzf '" + out.string() + "' -O ./trust.toml");
    EXPECT_NE(mf.find("[build]"), std::string::npos);
    EXPECT_NE(mf.find("formatVersion"), std::string::npos);
    EXPECT_NE(mf.find("toolchain"), std::string::npos);
    EXPECT_NE(mf.find("created"), std::string::npos);
    EXPECT_NE(mf.find("demo"), std::string::npos);
    EXPECT_NE(mf.find(TRUST_VERSION_SHORT), std::string::npos);

    // Исходный trust.toml НЕ изменяется (build в него не дописывается).
    EXPECT_EQ(readFile(dir / "trust.toml").find("[build]"), std::string::npos);
}

TEST(PackageCreate, MissingManifest) {
    fs::path dir = freshDir("pkg_create_nomf");
    writeFile(dir / "readme.txt", "x\n");
    EXPECT_EQ(createTrustPackage(dir, dir.parent_path() / "n.trust"), 1);
}

TEST(PackageCreate, NotADirectory) {
    fs::path dir = freshDir("pkg_create_nodir");
    EXPECT_EQ(createTrustPackage(dir / "nope", dir.parent_path() / "n.trust"), 1);
}

TEST(PackageCreate, InvalidManifestToml) {
    fs::path dir = makePackage(freshDir("pkg_create_badtoml"), "name = \"x\"\nversion =\n");
    EXPECT_EQ(createTrustPackage(dir, dir.parent_path() / "n.trust"), 1);
}

TEST(PackageCreate, InvalidName) {
    fs::path dir = makePackage(freshDir("pkg_create_badname"), manifestText("Bad-Name"));
    EXPECT_EQ(createTrustPackage(dir, dir.parent_path() / "n.trust"), 1);
}

TEST(PackageCreate, InvalidVersion) {
    fs::path dir = makePackage(freshDir("pkg_create_badver"), manifestText("ok", false) + "version = \"1\"\n");
    EXPECT_EQ(createTrustPackage(dir, dir.parent_path() / "n.trust"), 1);
}

// Версия пакета — semver САМОГО ПАКЕТА и не связана с версией компилятора:
// упаковщик сохраняет авторскую версию, а версия toolchain живёт в [build].toolchain.
TEST(PackageCreate, KeepsAuthorVersion) {
    fs::path dir = makePackage(freshDir("pkg_create_ver_author"), manifestText("authored", false) + "version = \"1.2.3\"\n");
    fs::path out = dir.parent_path() / "authored.trust";
    fs::remove(out);
    ASSERT_EQ(createTrustPackage(dir, out), 0);
    const std::string mf = captureShell("tar xzf '" + out.string() + "' -O ./trust.toml");
    EXPECT_NE(mf.find("version = '1.2.3'"), std::string::npos) << mf;
    // Provenance toolchain фиксируется отдельно от версии пакета.
    EXPECT_NE(mf.find(std::string("toolchain = '") + TRUST_VERSION + "'"), std::string::npos) << mf;
}

// Манифест без version: упаковщик проставляет версию компилятора.
TEST(PackageCreate, VersionIsStampedByPackager) {
    fs::path dir = makePackage(freshDir("pkg_create_ver_stamp"), manifestText("stamped", false));
    fs::path out = dir.parent_path() / "stamped.trust";
    fs::remove(out);
    ASSERT_EQ(createTrustPackage(dir, out), 0);
    const std::string mf = captureShell("tar xzf '" + out.string() + "' -O ./trust.toml");
    EXPECT_NE(mf.find(std::string("version = '") + TRUST_VERSION_SHORT + "'"), std::string::npos) << mf;
    // Исходный манифест версию не получил.
    EXPECT_EQ(readFile(dir / "trust.toml").find("version"), std::string::npos);
}

TEST(PackageCreate, NameCollision) {
    fs::path dir = freshDir("pkg_create_collide");
    writeFile(dir / "trust.toml", manifestText("collide"));
    writeFile(dir / "dup.src", "a:Int32 := 1;\n");
    writeFile(dir / "dup.trust", "nested\n");
    EXPECT_EQ(createTrustPackage(dir, dir.parent_path() / "n.trust"), 1);
}

// -- Распаковка пакета (--extract) --

namespace {

/// RAII-восстановление текущего каталога (для теста дефолтного назначения).
struct CwdGuard {
    fs::path saved;
    CwdGuard()
    : saved(fs::current_path()) {}
    ~CwdGuard() {
        std::error_code ec;
        fs::current_path(saved, ec);
    }
};

} // namespace

TEST(PackageExtract, RoundTrip) {
    fs::path dir = makePackage(freshDir("pkg_extract_rt"), manifestText("rt"));
    fs::path out = dir.parent_path() / "rt.trust";
    fs::remove(out);
    ASSERT_EQ(createTrustPackage(dir, out), 0);

    fs::path dest = dir.parent_path() / "rt_out";
    fs::remove_all(dest);
    ASSERT_EQ(extractTrustPackage(out, dest), 0);
    EXPECT_TRUE(fs::is_regular_file(dest / "trust.toml"));
    EXPECT_TRUE(fs::is_regular_file(dest / "mod_a.src"));
    EXPECT_TRUE(fs::is_regular_file(dest / "sub" / "mod_b.src"));
    EXPECT_TRUE(fs::is_regular_file(dest / "notes.txt"));

    PackageManifest m;
    std::string err;
    ASSERT_TRUE(parseManifestFile(dest / "trust.toml", m, err)) << err;
    EXPECT_EQ(m.name(), "rt");
    EXPECT_TRUE(m.data["build"].is_table());
}

TEST(PackageExtract, DefaultDestinationIsPackageName) {
    fs::path dir = makePackage(freshDir("pkg_extract_def_src"), manifestText("defname"));
    fs::path out = dir.parent_path() / "defname.trust";
    fs::remove(out);
    ASSERT_EQ(createTrustPackage(dir, out), 0);

    fs::path work = freshDir("pkg_extract_def_work");
    CwdGuard guard;
    fs::current_path(work);
    const int rc = extractTrustPackage(out, {});
    ASSERT_EQ(rc, 0);
    EXPECT_TRUE(fs::is_regular_file(work / "defname" / "trust.toml"));
}

TEST(PackageExtract, NotAFile) {
    fs::path dir = freshDir("pkg_extract_notfile");
    EXPECT_EQ(extractTrustPackage(dir / "nope.trust", dir / "out"), 1);
}

TEST(PackageExtract, NoManifestInArchive) {
    fs::path src = freshDir("pkg_extract_nomf_src");
    writeFile(src / "a.src", "a:Int32 := 1;\n");
    fs::path arc = src.parent_path() / "nomf.trust";
    fs::remove(arc);
    ASSERT_EQ(runShell("tar czf '" + arc.string() + "' -C '" + src.string() + "' ."), 0);
    fs::path dest = src.parent_path() / "nomf_out";
    fs::remove_all(dest);
    EXPECT_EQ(extractTrustPackage(arc, dest), 1);
}

TEST(PackageExtract, MalformedManifestInArchive) {
    fs::path src = freshDir("pkg_extract_badm_src");
    writeFile(src / "trust.toml", "name = \"x\"\nversion =\n");
    fs::path arc = src.parent_path() / "badm.trust";
    fs::remove(arc);
    ASSERT_EQ(runShell("tar czf '" + arc.string() + "' -C '" + src.string() + "' ."), 0);
    fs::path dest = src.parent_path() / "badm_out";
    fs::remove_all(dest);
    EXPECT_EQ(extractTrustPackage(arc, dest), 1);
}

TEST(PackageExtract, PathTraversalRejected) {
    fs::path src = freshDir("pkg_extract_trav_src");
    writeFile(src / "evil.txt", "evil\n");
    fs::path arc = src.parent_path() / "trav.trust";
    fs::remove(arc);
    const std::string cmd = "tar -czf '" + arc.string() + "' --transform='s|^evil.txt$|../trust.toml|' -C '" + src.string() + "' evil.txt";
    ASSERT_EQ(runShell(cmd), 0);
    fs::path dest = src.parent_path() / "trav_out";
    fs::remove_all(dest);
    EXPECT_EQ(extractTrustPackage(arc, dest), 1);
    EXPECT_FALSE(fs::exists(src.parent_path() / "trust.toml"));
}

TEST(PackageExtract, DestinationIsFile) {
    fs::path dir = makePackage(freshDir("pkg_extract_destfile_src"), manifestText("df"));
    fs::path out = dir.parent_path() / "df.trust";
    fs::remove(out);
    ASSERT_EQ(createTrustPackage(dir, out), 0);
    fs::path dest = dir.parent_path() / "df_out";
    fs::remove_all(dest);
    writeFile(dest, "occupied\n");
    EXPECT_EQ(extractTrustPackage(out, dest), 1);
}

// -- Контроль целостности ([files] в манифесте; MD5) --

TEST(PackageIntegrity, ManifestCarriesFileHashes) {
    fs::path dir = makePackage(freshDir("pkg_integrity"), manifestText("intp"));
    fs::path out = dir.parent_path() / "intp.trust";
    fs::remove(out);
    ASSERT_EQ(createTrustPackage(dir, out), 0);

    fs::path dest = dir.parent_path() / "intp_out";
    fs::remove_all(dest);
    ASSERT_EQ(extractTrustPackage(out, dest), 0);

    PackageManifest m;
    std::string err;
    ASSERT_TRUE(parseManifestFile(dest / "trust.toml", m, err)) << err;
    ASSERT_TRUE(m.data["files"].is_table());
    EXPECT_TRUE(m.data["files"]["mod_a.src"].is_string());
    EXPECT_TRUE(m.data["files"]["sub/mod_b.src"].is_string());
}

TEST(PackageIntegrity, VerifyDetectsTamperedFile) {
    fs::path dir = makePackage(freshDir("pkg_tamper"), manifestText("tamp"));
    fs::path out = dir.parent_path() / "tamp.trust";
    fs::remove(out);
    ASSERT_EQ(createTrustPackage(dir, out), 0);

    fs::path dest = dir.parent_path() / "tamp_out";
    fs::remove_all(dest);
    ASSERT_EQ(extractTrustPackage(out, dest), 0);

    std::string err;
    ASSERT_TRUE(trust::verifyPackage(dest, err)) << err;

    writeFile(dest / "mod_a.src", "tampered\n");
    EXPECT_FALSE(trust::verifyPackage(dest, err));
}

TEST(PackageIntegrity, VerifyDetectsUnlistedFile) {
    fs::path dir = makePackage(freshDir("pkg_unlisted"), manifestText("unl"));
    fs::path out = dir.parent_path() / "unl.trust";
    fs::remove(out);
    ASSERT_EQ(createTrustPackage(dir, out), 0);

    fs::path dest = dir.parent_path() / "unl_out";
    fs::remove_all(dest);
    ASSERT_EQ(extractTrustPackage(out, dest), 0);

    std::string err;
    ASSERT_TRUE(trust::verifyPackage(dest, err)) << err;
    writeFile(dest / "extra.txt", "extra\n");
    EXPECT_FALSE(trust::verifyPackage(dest, err));
}

TEST(PackageIntegrity, VerifyRequiresManifest) {
    fs::path dir = freshDir("pkg_verify_nomanifest");
    std::string err;
    EXPECT_FALSE(trust::verifyPackage(dir, err));
    EXPECT_NE(err.find("trust.toml"), std::string::npos);
}

TEST(PackageIntegrity, VerifyRequiresFilesTable) {
    fs::path dir = freshDir("pkg_verify_nofiles");
    writeFile(dir / "trust.toml", manifestText("nofiles"));
    std::string err;
    EXPECT_FALSE(trust::verifyPackage(dir, err));
    EXPECT_NE(err.find("[files]"), std::string::npos);
}

TEST(PackageExtract, MissingFilesTableRejected) {
    fs::path src = freshDir("pkg_extract_nofiles_src");
    writeFile(src / "trust.toml", manifestText("nofiles"));
    writeFile(src / "a.src", "a:Int32 := 1;\n");
    fs::path arc = src.parent_path() / "nofiles.trust";
    fs::remove(arc);
    ASSERT_EQ(runShell("tar czf '" + arc.string() + "' -C '" + src.string() + "' ."), 0);
    fs::path dest = src.parent_path() / "nofiles_out";
    fs::remove_all(dest);
    EXPECT_EQ(extractTrustPackage(arc, dest), 1);
}

// C++-заголовки пакета размещаются в include/ и попадают в архив вместе с [files].
TEST(PackageCreate, HeaderUnderIncludeIsPackaged) {
    fs::path dir = freshDir("pkg_create_header");
    writeFile(dir / "trust.toml", manifestText("withheader"));
    writeFile(dir / "mod.src", "x:Int32 := 1;\n");
    writeFile(dir / "include" / "pkg" / "header.hpp", "#pragma once\ninline int value() { return 42; }\n");

    fs::path out = dir.parent_path() / "withheader.trust";
    fs::remove(out);
    ASSERT_EQ(createTrustPackage(dir, out), 0);

    const std::string list = captureShell("tar tzf '" + out.string() + "'");
    EXPECT_NE(list.find("./include/pkg/header.hpp"), std::string::npos) << list;

    const std::string manifest = captureShell("tar xzf '" + out.string() + "' -O ./trust.toml");
    EXPECT_NE(manifest.find("'include/pkg/header.hpp'"), std::string::npos) << manifest;

    std::string content;
    std::string error;
    ASSERT_TRUE(readPackageEntry(out, "include/pkg/header.hpp", content, error)) << error;
    EXPECT_NE(content.find("return 42"), std::string::npos);
}

TEST(PackageReadEntry, RejectsMissingAndTraversalPaths) {
    fs::path dir = makePackage(freshDir("pkg_read_entry"), manifestText("readentry"));
    fs::path out = dir.parent_path() / "readentry.trust";
    fs::remove(out);
    ASSERT_EQ(createTrustPackage(dir, out), 0);

    std::string content;
    std::string error;
    EXPECT_TRUE(readPackageEntry(out, "./mod_a.src", content, error)) << error;
    EXPECT_NE(content.find("a:Int32"), std::string::npos);

    EXPECT_FALSE(readPackageEntry(out, "missing.src", content, error));
    EXPECT_FALSE(readPackageEntry(out, "../trust.toml", content, error));
    EXPECT_FALSE(readPackageEntry(out, "", content, error));
}

// -- Раскладка пакета слоями ([[layout.layers]]) --

namespace {

/// Пакет с раскладкой по слоям: исходники и заголовки в разных подкаталогах.
fs::path makeLayeredPackage(const fs::path& dir) {
    writeFile(dir / "trust.toml", "name = \"layered\"\n"
                                  "[[layout.layers]]\n"
                                  "archive-dir = \"src\"\n"
                                  "patterns = [\"*.src\"]\n"
                                  "[[layout.layers]]\n"
                                  "source-dir = \"inc\"\n"
                                  "archive-dir = \"include/pkg\"\n"
                                  "patterns = [\"*.hpp\"]\n");
    writeFile(dir / "a.src", "a:Int32 := 1;\n");
    writeFile(dir / "nested" / "b.src", "b:Int32 := 2;\n");
    writeFile(dir / "inc" / "header.hpp", "#pragma once\n");
    writeFile(dir / "inc" / "deep" / "inner.hpp", "#pragma once\n");
    writeFile(dir / "notes.txt", "not packed\n");
    writeFile(dir / "CMakeLists.txt", "# not packed\n");
    return dir;
}

} // namespace

// Слои раскладывают файлы по target; подкаталоги входят рекурсивно; прочие расширения
// (notes.txt, CMakeLists.txt) в пакет не попадают.
TEST(PackageLayout, LayersPlaceFilesRecursively) {
    fs::path dir = makeLayeredPackage(freshDir("pkg_layout_ok"));
    fs::path out = dir.parent_path() / "layered.trust";
    fs::remove(out);
    ASSERT_EQ(createTrustPackage(dir, out), 0);

    const std::string list = captureShell("tar tzf '" + out.string() + "'");
    EXPECT_NE(list.find("./src/a.src"), std::string::npos) << list;
    EXPECT_NE(list.find("./src/nested/b.src"), std::string::npos) << list;
    EXPECT_NE(list.find("./include/pkg/header.hpp"), std::string::npos) << list;
    EXPECT_NE(list.find("./include/pkg/deep/inner.hpp"), std::string::npos) << list;
    EXPECT_EQ(list.find("notes.txt"), std::string::npos) << list;
    EXPECT_EQ(list.find("CMakeLists.txt"), std::string::npos) << list;

    // [files] покрывает все упакованные файлы.
    const std::string manifest = captureShell("tar xzf '" + out.string() + "' -O ./trust.toml");
    EXPECT_NE(manifest.find("'src/a.src'"), std::string::npos) << manifest;
    EXPECT_NE(manifest.find("'include/pkg/deep/inner.hpp'"), std::string::npos) << manifest;
}

// source и target по умолчанию ".": слой из одного include кладёт файлы в корень архива
// из каталога манифеста.
TEST(PackageLayout, DefaultLayerDirs) {
    fs::path dir = freshDir("pkg_layout_defaults");
    writeFile(dir / "trust.toml", "name = \"defs\"\n"
                                  "[[layout.layers]]\n"
                                  "patterns = [\"*.src\"]\n");
    writeFile(dir / "a.src", "a:Int32 := 1;\n");
    fs::path out = dir.parent_path() / "defs.trust";
    fs::remove(out);
    ASSERT_EQ(createTrustPackage(dir, out), 0);
    const std::string list = captureShell("tar tzf '" + out.string() + "'");
    EXPECT_NE(list.find("./a.src"), std::string::npos) << list;
}

// Маски применяются к ИМЕНИ файла: `*.hpp`/`LICENSE*`/`?` работают рекурсивно и не
// зависят от подкаталога.
TEST(PackageLayout, PatternMasksMatchFileNames) {
    fs::path dir = freshDir("pkg_layout_patterns");
    writeFile(dir / "trust.toml", "name = \"masks\"\n"
                                  "[[layout.layers]]\n"
                                  "patterns = [\"*.hpp\", \"LICENSE*\", \"a?.txt\"]\n");
    writeFile(dir / "a.hpp", "// a\n");
    writeFile(dir / "nested" / "b.hpp", "// b\n");
    writeFile(dir / "c.cpp", "// c\n");
    writeFile(dir / "LICENSE", "license\n");
    writeFile(dir / "ab.txt", "ab\n");
    writeFile(dir / "abc.txt", "abc\n");
    fs::path out = dir.parent_path() / "masks.trust";
    fs::remove(out);
    ASSERT_EQ(createTrustPackage(dir, out), 0);
    const std::string list = captureShell("tar tzf '" + out.string() + "'");
    EXPECT_NE(list.find("./a.hpp"), std::string::npos) << list;
    EXPECT_NE(list.find("./nested/b.hpp"), std::string::npos) << list;
    EXPECT_NE(list.find("./LICENSE"), std::string::npos) << list;
    EXPECT_NE(list.find("./ab.txt"), std::string::npos) << list;
    EXPECT_EQ(list.find("./c.cpp"), std::string::npos) << list;
    EXPECT_EQ(list.find("./abc.txt"), std::string::npos) << list;
}

// Без patterns слой невалиден (явная ошибка, без fallback на «все файлы»).
TEST(PackageLayout, MissingPatternsIsError) {
    fs::path dir = freshDir("pkg_layout_nopatterns");
    writeFile(dir / "trust.toml", "name = \"nop\"\n"
                                  "[[layout.layers]]\n"
                                  "archive-dir = \"src\"\n");
    writeFile(dir / "a.src", "a:Int32 := 1;\n");
    EXPECT_EQ(createTrustPackage(dir, dir.parent_path() / "n.trust"), 1);
}

// Отсутствующий каталог слоя — ошибка (без fallback).
TEST(PackageLayout, MissingSourceIsError) {
    fs::path dir = freshDir("pkg_layout_nosource");
    writeFile(dir / "trust.toml", "name = \"nosrc\"\n"
                                  "[[layout.layers]]\n"
                                  "source-dir = \"absent\"\n"
                                  "archive-dir = \"src\"\n"
                                  "patterns = [\"*.src\"]\n");
    EXPECT_EQ(createTrustPackage(dir, dir.parent_path() / "n.trust"), 1);
}

// target с '..' — ошибка (путь не должен выходить за пределы архива).
TEST(PackageLayout, ArchiveDirTraversalIsError) {
    fs::path dir = freshDir("pkg_layout_traversal");
    writeFile(dir / "trust.toml", "name = \"trav\"\n"
                                  "[[layout.layers]]\n"
                                  "source-dir = \".\"\n"
                                  "archive-dir = \"../escape\"\n"
                                  "patterns = [\"*.src\"]\n");
    writeFile(dir / "a.src", "a:Int32 := 1;\n");
    EXPECT_EQ(createTrustPackage(dir, dir.parent_path() / "n.trust"), 1);
}

// Слой без единого подходящего файла — ошибка (пакет не должен молча быть неполным).
TEST(PackageLayout, EmptyLayerIsError) {
    fs::path dir = freshDir("pkg_layout_empty");
    writeFile(dir / "trust.toml", "name = \"empty\"\n"
                                  "[[layout.layers]]\n"
                                  "source-dir = \".\"\n"
                                  "archive-dir = \"src\"\n"
                                  "patterns = [\"*.nope\"]\n");
    writeFile(dir / "a.src", "a:Int32 := 1;\n");
    EXPECT_EQ(createTrustPackage(dir, dir.parent_path() / "n.trust"), 1);
}

// Два слоя дают один и тот же путь в архиве — ошибка (коллизия).
TEST(PackageLayout, DuplicateArchiveDirIsError) {
    fs::path dir = freshDir("pkg_layout_dup");
    writeFile(dir / "trust.toml", "name = \"dup\"\n"
                                  "[[layout.layers]]\n"
                                  "source-dir = \".\"\n"
                                  "archive-dir = \"src\"\n"
                                  "patterns = [\"*.src\"]\n"
                                  "[[layout.layers]]\n"
                                  "source-dir = \".\"\n"
                                  "archive-dir = \"src\"\n"
                                  "patterns = [\"*.src\"]\n");
    writeFile(dir / "a.src", "a:Int32 := 1;\n");
    EXPECT_EQ(createTrustPackage(dir, dir.parent_path() / "n.trust"), 1);
}

// Пакет без [[layout.layers]] — прежнее поведение (всё дерево).
TEST(PackageLayout, AbsentLayoutPacksWholeTree) {
    fs::path dir = makePackage(freshDir("pkg_layout_absent"), manifestText("nole"));
    fs::path out = dir.parent_path() / "nole.trust";
    fs::remove(out);
    ASSERT_EQ(createTrustPackage(dir, out), 0);
    const std::string list = captureShell("tar tzf '" + out.string() + "'");
    EXPECT_NE(list.find("./mod_a.src"), std::string::npos) << list;
    EXPECT_NE(list.find("./sub/mod_b.src"), std::string::npos) << list;
    EXPECT_NE(list.find("./notes.txt"), std::string::npos) << list;
}
