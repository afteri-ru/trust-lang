// test/unit/packages/manifest_test.cpp - package manifest document (structure is the model's
// concern; version/range semantics belong to the consumer).
#include "packages/errors.hpp"
#include "packages/manifest.hpp"

#include <gtest/gtest.h>

#include <string>

using namespace trust::packages;

TEST(PackagesManifest, ParsesPackageModelFields) {
    const nlohmann::json json = nlohmann::json::parse(R"({"schema":1,"name":"acme/foo","version":"1.0.0","dependencies":{"acme/bar":"^2.0"}})");
    const Manifest manifest = parseManifest(json);
    EXPECT_EQ(manifest.schema, 1);
    EXPECT_EQ(manifest.name, "acme/foo");
    EXPECT_EQ(manifest.version, "1.0.0");
    ASSERT_EQ(manifest.dependencies.size(), 1u);
    EXPECT_EQ(manifest.dependencies[0].name, "acme/bar");
    EXPECT_EQ(manifest.dependencies[0].range, "^2.0");
}

TEST(PackagesManifest, PreservesUnknownFieldsAsData) {
    // Fields the package model does not interpret (signature/url/custom) must not be lost.
    const nlohmann::json json = nlohmann::json::parse(R"({"schema":1,"name":"a","version":"1.0.0","signature":"abc","url":"https://x","custom":{"k":1}})");
    const Manifest manifest = parseManifest(json);
    EXPECT_EQ(manifestToJson(manifest).at("signature").get<std::string>(), "abc");
    EXPECT_EQ(manifestToJson(manifest).at("url").get<std::string>(), "https://x");
    EXPECT_EQ(manifestToJson(manifest).at("custom").at("k").get<int>(), 1);
}

TEST(PackagesManifest, VersionIsDataNotValidated) {
    // `1.0` is not strict semver, but the package model carries the version as an opaque string.
    const Manifest manifest = parseManifest(nlohmann::json::parse(R"({"schema":1,"name":"a","version":"1.0"})"));
    EXPECT_EQ(manifest.version, "1.0");
}

TEST(PackagesManifest, RejectsInvalidStructure) {
    EXPECT_THROW(parseManifest(nlohmann::json::parse(R"({"name":"a","version":"1.0.0"})")), PackageException);             // no schema
    EXPECT_THROW(parseManifest(nlohmann::json::parse(R"({"schema":99,"name":"a","version":"1.0.0"})")), PackageException); // bad schema
    EXPECT_THROW(parseManifest(nlohmann::json::parse(R"({"schema":1,"version":"1.0.0"})")), PackageException);             // no name
    EXPECT_THROW(parseManifest(nlohmann::json::parse(R"({"schema":1,"name":"a"})")), PackageException);                    // no version
    EXPECT_THROW(parseManifest(nlohmann::json::parse(R"({"schema":1,"name":"a","version":"1.0.0","dependencies":{"x":1}})")),
                 PackageException);                                                     // range not a string
    EXPECT_THROW(parseManifest(nlohmann::json::parse(R"([1,2,3])")), PackageException); // not an object
}
