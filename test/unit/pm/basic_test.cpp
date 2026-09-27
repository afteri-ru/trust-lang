// test/unit/pm/basic_test.cpp - unit tests for the package manager component.
#include "pm/cache.hpp"
#include "pm/manifest.hpp"
#include "pm/protocol.hpp"
#include "pm/resolver.hpp"
#include "pm/semver.hpp"

#include "utils/hash.hpp"

#include "test_data.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace trust::pm;
using trust::utils::sha256File;

namespace {

void writeFile(const fs::path& path, const std::string& data) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << data;
}

} // namespace

// --- semver ---

TEST(PmSemver, ParseAndFormat) {
    const Semver v = parseSemver("1.2.3");
    EXPECT_EQ(v.major, 1);
    EXPECT_EQ(v.minor, 2);
    EXPECT_EQ(v.patch, 3);
    EXPECT_EQ(formatSemver(v), "1.2.3");
    EXPECT_THROW(parseSemver("1.2"), PmException);
    EXPECT_THROW(parseSemver("x.y.z"), PmException);
}

TEST(PmSemver, SatisfiesRanges) {
    const Semver v = parseSemver("1.4.2");
    EXPECT_TRUE(satisfiesRange(v, "*"));
    EXPECT_TRUE(satisfiesRange(v, "^1.2"));
    EXPECT_FALSE(satisfiesRange(v, "^2.0"));
    EXPECT_TRUE(satisfiesRange(v, "~1.4"));
    EXPECT_FALSE(satisfiesRange(v, "~1.3"));
    EXPECT_TRUE(satisfiesRange(v, ">=1.0"));
    EXPECT_FALSE(satisfiesRange(v, ">=1.5"));
    EXPECT_TRUE(satisfiesRange(v, "^1.2,<1.5"));
    EXPECT_FALSE(satisfiesRange(v, "^1.2,<1.4"));
    EXPECT_TRUE(satisfiesRange(v, "1.4.2"));
}

// --- manifest ---

TEST(PmManifest, ParsesValidManifest) {
    const nlohmann::json json = nlohmann::json::parse(R"({"schema":1,"name":"acme/foo","version":"1.0.0","dependencies":{"acme/bar":"^2.0"}})");
    const Manifest manifest = parseManifest(json);
    EXPECT_EQ(manifest.name, "acme/foo");
    EXPECT_EQ(manifest.version, "1.0.0");
    ASSERT_EQ(manifest.dependencies.size(), 1u);
    EXPECT_EQ(manifest.dependencies[0].name, "acme/bar");
    EXPECT_EQ(manifest.dependencies[0].range, "^2.0");
}

TEST(PmManifest, RejectsInvalidManifest) {
    EXPECT_THROW(parseManifest(nlohmann::json::parse(R"({"name":"a","version":"1.0.0"})")), PmException);
    EXPECT_THROW(parseManifest(nlohmann::json::parse(R"({"schema":99,"name":"a","version":"1.0.0"})")), PmException);
    EXPECT_THROW(parseManifest(nlohmann::json::parse(R"({"schema":1,"name":"a","version":"1.0"})")), PmException);
    EXPECT_THROW(parseManifest(nlohmann::json::parse(R"({"schema":1,"name":"a","version":"1.0.0","dependencies":{"x":1}})")), PmException);
}

// --- resolver ---

TEST(PmResolver, SelectsHighestSatisfyingVersion) {
    const Manifest root = parseManifest(nlohmann::json::parse(R"({"schema":1,"name":"acme/app","version":"0.1.0","dependencies":{"acme/bar":"^2.0"}})"));
    std::vector<PackageRecord> candidates;
    candidates.push_back(PackageRecord{"acme/bar", "2.0.0", {}, {}, {}, {}, "local"});
    candidates.push_back(PackageRecord{"acme/bar", "2.1.0", {}, {}, {}, {}, "local"});
    candidates.push_back(PackageRecord{"acme/bar", "3.0.0", {}, {}, {}, {}, "local"});
    const std::vector<Resolved> resolved = resolveDependencies(root, candidates);
    ASSERT_EQ(resolved.size(), 1u);
    EXPECT_EQ(resolved[0].name, "acme/bar");
    EXPECT_EQ(resolved[0].version, "2.1.0");
}

TEST(PmResolver, ResolvesTransitiveDependencies) {
    const Manifest root = parseManifest(nlohmann::json::parse(R"({"schema":1,"name":"acme/app","version":"0.1.0","dependencies":{"acme/bar":"^1.0"}})"));
    std::vector<PackageRecord> candidates;
    candidates.push_back(PackageRecord{"acme/bar", "1.0.0", {Dependency{"acme/baz", "^1.0"}}, {}, {}, {}, "local"});
    candidates.push_back(PackageRecord{"acme/baz", "1.2.0", {}, {}, {}, {}, "local"});
    const std::vector<Resolved> resolved = resolveDependencies(root, candidates);
    ASSERT_EQ(resolved.size(), 2u);
    EXPECT_EQ(resolved[0].name, "acme/bar");
    EXPECT_EQ(resolved[1].name, "acme/baz");
}

TEST(PmResolver, ConflictsWithoutCandidates) {
    const Manifest root = parseManifest(nlohmann::json::parse(R"({"schema":1,"name":"acme/app","version":"0.1.0","dependencies":{"acme/bar":"^2.0"}})"));
    std::vector<PackageRecord> candidates;
    candidates.push_back(PackageRecord{"acme/bar", "1.0.0", {}, {}, {}, {}, "local"});
    EXPECT_THROW(resolveDependencies(root, candidates), PmException);
}

// --- cache / hashing ---

TEST(PmCache, Sha256AndStore) {
    const fs::path dir = trust::test::makeTestDataDir("pm_cache");
    const fs::path source = dir / "artifact.bin";
    writeFile(source, "abc");
    // Known SHA256 of "abc".
    EXPECT_EQ(sha256File(source), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    Cache cache(dir / "cache");
    const std::string hash = sha256File(source);
    EXPECT_FALSE(cache.has(hash));
    const fs::path stored = cache.store(hash, source);
    EXPECT_TRUE(cache.has(hash));
    EXPECT_EQ(sha256File(stored), hash);
}

// --- protocol ---

TEST(PmProtocol, Envelopes) {
    const nlohmann::json ok = okEnvelope(nlohmann::json{{"value", 1}});
    EXPECT_EQ(ok.at("protocol").get<int>(), kProtocolVersion);
    EXPECT_TRUE(ok.at("ok").get<bool>());
    EXPECT_TRUE(ok.at("errors").empty());
    const nlohmann::json bad = errorEnvelope(PmError{"pm.test", "boom", nlohmann::json::object()});
    EXPECT_FALSE(bad.at("ok").get<bool>());
    ASSERT_EQ(bad.at("errors").size(), 1u);
    EXPECT_EQ(bad.at("errors")[0].at("code").get<std::string>(), "pm.test");
}
