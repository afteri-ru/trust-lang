// module_api_save_test.cpp — stress tests for ModuleApi save/load (packToMsgpack / fromMsgpack)
//
// Tests: roundtrip serialization/deserialization, file I/O, edge cases
// Parametric test: 10 variants of TokenInfo with different names and attrs

#include "ast/attr_pool.hpp"
#include "ast/attr_builtin.hpp"
#include "ast/token_info.hpp"

#include <gtest/gtest.h>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace trust;
namespace fs = std::filesystem;

// ═══════════════════════════════════════════════════════════════
//   Test fixture
// ═══════════════════════════════════════════════════════════════

class ModuleApiSaveTest : public ::testing::Test {
  protected:
    AttrPool pool;
    fs::path tempFile;

    void SetUp() override {
        register_builtin_attrs(pool);
        for (int i = 0; i < 100; ++i) {
            auto p = fs::temp_directory_path() / ("module_api_test_" + std::to_string(i));
            if (!fs::exists(p)) {
                tempFile = p;
                break;
            }
        }
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove(tempFile, ec);
    }
};

// ═══════════════════════════════════════════════════════════════
//   Test data for parametric test
// ═══════════════════════════════════════════════════════════════

struct AttrSpec {
    std::string name;
    std::vector<std::string> params; // parameter values

    AttrId register_into(AttrPool& p) const {
        if (params.empty()) {
            return p.register_attr(name, std::vector<AttrParamType>{});
        }
        std::vector<AttrParam> param_objs;
        param_objs.reserve(params.size());
        for (const auto& pv : params) {
            param_objs.emplace_back(std::string_view(pv));
        }
        return p.register_attr(name, std::move(param_objs));
    }
};

struct TestCase {
    std::string token_name;
    std::vector<AttrSpec> attrs;

    /// Build a TokenInfo for this case using a fresh pool with builtins.
    std::pair<AttrPool, TokenInfo> build() const {
        AttrPool p;
        register_builtin_attrs(p);
        TokenInfo tok;
        tok.text = token_name;
        for (const auto& a : attrs) {
            AttrId id = a.register_into(p);
            tok.add_attr(id);
        }
        return {std::move(p), std::move(tok)};
    }

    /// Check that a reconstructed TokenInfo matches the expected attrs.
    void verify(const TokenInfo& tok, const AttrPoolView& restored_pool) const {
        EXPECT_EQ(tok.text, token_name);
        for (const auto& a : attrs) {
            EXPECT_TRUE(tok.has_attr(restored_pool, a.name)) << "Token '" << token_name << "' should have attr '" << a.name << "'";
        }
    }
};

// ═══════════════════════════════════════════════════════════════
//   10 test cases covering all combinations
// ═══════════════════════════════════════════════════════════════

static const TestCase kTestCases[] = {
    // 1. No attrs, simple name
    {"a", {}},

    // 2. Single attr (0 params), single namespace
    {"ns1::a", {{"const", {}}}},

    // 3. Two attrs (0+0 params), two-level namespace
    {"ns1::ns2::a", {{"pure", {}}, {"send", {}}}},

    // 4. Single attr with 1 param
    {"a", {{"trust", {"key"}}}},

    // 5. Single attr with 2 params
    {"ns1::a", {{"trust", {"k1", "k2"}}}},

    // 6. Single attr with 3 params, two-level namespace
    {"ns1::ns2::a", {{"trust", {"a", "b", "c"}}}},

    // 7. Three attrs (0 params each)
    {"a", {{"const", {}}, {"pure", {}}, {"send", {}}}},

    // 8. Mixed attrs: 0 params + 1 param + 2 params, two-level namespace
    {"ns1::ns2::a", {{"const", {}}, {"trust", {"k"}}, {"require", {"a", "b"}}}},

    // 9. Macro attr (depend_macro) with 3 params, deep nested namespace
    {"deep::nested::ns::a", {{"depend_macro", {"m1", "m2", "m3"}}}},

    // 10. Macro attr with 1 param, top-level namespace
    {"top::a", {{"depend_macro", {"m"}}}},
};

// ═══════════════════════════════════════════════════════════════
//   Helper: roundtrip through file
// ═══════════════════════════════════════════════════════════════

static std::unique_ptr<ModuleApi> roundtripThroughFile(const ModuleApi& api, const fs::path& path) {
    if (!api.save_to_file(path.string()))
        return nullptr;
    return ModuleApi::load_from_file(path.string());
}

// ═══════════════════════════════════════════════════════════════
//   Parametric roundtrip test: all 10 tokens → one ModuleApi → save → load → verify
// ═══════════════════════════════════════════════════════════════

TEST_F(ModuleApiSaveTest, SaveLoadAllTokensRoundtrip) {
    // 1. Build all TokenInfos from test data
    std::vector<TokenInfo> token_storage;
    std::vector<AttrPool> pool_storage;
    token_storage.reserve(10);
    pool_storage.reserve(10);

    for (const auto& tc : kTestCases) {
        auto [p, tok] = tc.build();
        pool_storage.push_back(std::move(p));
        token_storage.push_back(std::move(tok));
    }

    // 2. Build pointer vector for export
    std::vector<TokenInfo*> token_ptrs;
    token_ptrs.reserve(token_storage.size());
    for (auto& tok : token_storage) {
        token_ptrs.push_back(&tok);
    }

    // 3. Export all tokens into one ModuleApi
    auto api = pool_storage[0].export_attrs(token_ptrs);
    ASSERT_NE(api, nullptr);
    EXPECT_EQ(api->token_count(), 10u);

    for (std::size_t i = 0; i < 10; ++i) {
        EXPECT_EQ(api->get_token_name(i), kTestCases[i].token_name);
    }

    // 4. Save all tokens to one file
    fs::path dataDir = fs::path(TEST_DATA_DIR);
    fs::create_directories(dataDir);
    fs::path filePath = dataDir / "ModuleApiSaveTest.bin";
    ASSERT_TRUE(api->save_to_file(filePath.string()));

    // 5. Load from file
    auto restored = ModuleApi::load_from_file(filePath.string());
    ASSERT_NE(restored, nullptr);
    EXPECT_EQ(restored->token_count(), 10u);

    // 6. Verify each restored token
    AttrPool fresh_pool;
    register_builtin_attrs(fresh_pool);

    for (std::size_t i = 0; i < 10; ++i) {
        const auto& tc = kTestCases[i];

        EXPECT_EQ(restored->get_token_name(i), tc.token_name);

        for (const auto& a : tc.attrs) {
            EXPECT_TRUE(restored->has_attr(a.name)) << "Attr '" << a.name << "' should exist in restored ModuleApi for token " << i;
        }

        auto recreated = restored->create_token(i, fresh_pool);
        ASSERT_NE(recreated, nullptr) << "create_token(" << i << ") should succeed for '" << tc.token_name << "'";

        for (const auto& a : tc.attrs) {
            EXPECT_TRUE(recreated->has_attr(fresh_pool, a.name))
                << "Recreated token[" << i << "] '" << tc.token_name << "' should have attr '" << a.name << "'";
        }
    }

    std::error_code ec;
    // fs::remove(filePath, ec);
}

TEST_F(ModuleApiSaveTest, EmptyModuleApi) {
    std::vector<TokenInfo*> empty;
    auto api = pool.export_attrs(empty);
    ASSERT_NE(api, nullptr);

    auto restored = roundtripThroughFile(*api, tempFile);
    ASSERT_NE(restored, nullptr);

    EXPECT_EQ(restored->token_count(), 0u);
    EXPECT_EQ(restored->attr_count(), 1u); // placeholder at index 0
    EXPECT_EQ(restored->set_count(), 0u);
}

TEST_F(ModuleApiSaveTest, SingleTokenSingleAttr) {
    AttrId const_id = pool.builtin_id(BuiltinAttrKind::kConst);
    TokenInfo tok;
    tok.text = "my_token";
    tok.add_attr(const_id);

    std::vector<TokenInfo*> tokens = {&tok};
    auto api = pool.export_attrs(tokens);
    ASSERT_NE(api, nullptr);
    EXPECT_EQ(api->token_count(), 1u);
    EXPECT_TRUE(api->has_attr("const"));

    auto restored = roundtripThroughFile(*api, tempFile);
    ASSERT_NE(restored, nullptr);

    EXPECT_EQ(restored->token_count(), 1u);
    EXPECT_TRUE(restored->has_attr("const"));

    auto new_const = restored->lookup("const");
    ASSERT_TRUE(new_const.has_value());
    EXPECT_EQ(restored->get_name(new_const.value()), "const");
}

TEST_F(ModuleApiSaveTest, MultipleTokensWithSets) {
    AttrId const_id = pool.builtin_id(BuiltinAttrKind::kConst);
    AttrId pure_id = pool.builtin_id(BuiltinAttrKind::kPure);
    AttrId send_id = pool.builtin_id(BuiltinAttrKind::kSend);

    TokenInfo tok1;
    tok1.text = "tok1";
    tok1.add_attr(const_id);
    tok1.add_attr(pure_id);
    tok1.add_attr(send_id);

    TokenInfo tok2;
    tok2.text = "tok2";
    tok2.add_attr(const_id);

    std::vector<TokenInfo*> tokens = {&tok1, &tok2};
    auto api = pool.export_attrs(tokens);
    ASSERT_NE(api, nullptr);
    EXPECT_EQ(api->token_count(), 2u);

    auto restored = roundtripThroughFile(*api, tempFile);
    ASSERT_NE(restored, nullptr);

    EXPECT_EQ(restored->token_count(), 2u);
    EXPECT_TRUE(restored->has_attr("const"));
    EXPECT_TRUE(restored->has_attr("pure"));
    EXPECT_TRUE(restored->has_attr("send"));

    // Verify token names
    EXPECT_EQ(restored->get_token_name(0), "tok1");
    EXPECT_EQ(restored->get_token_name(1), "tok2");

    // Verify attributes via lookup
    auto new_const = restored->lookup("const");
    auto new_pure = restored->lookup("pure");
    auto new_send = restored->lookup("send");
    ASSERT_TRUE(new_const.has_value());
    ASSERT_TRUE(new_pure.has_value());
    ASSERT_TRUE(new_send.has_value());
}

TEST_F(ModuleApiSaveTest, TokenNamesWithPrefixes) {
    AttrId const_id = pool.builtin_id(BuiltinAttrKind::kConst);

    TokenInfo tok1;
    tok1.text = "mymodule::TokenA";
    tok1.add_attr(const_id);
    TokenInfo tok2;
    tok2.text = "mymodule::TokenB";
    tok2.add_attr(const_id);

    std::vector<TokenInfo*> tokens = {&tok1, &tok2};
    auto api = pool.export_attrs(tokens);
    ASSERT_NE(api, nullptr);
    EXPECT_EQ(api->token_count(), 2u);

    auto restored = roundtripThroughFile(*api, tempFile);
    ASSERT_NE(restored, nullptr);

    EXPECT_EQ(restored->token_count(), 2u);
    EXPECT_EQ(restored->get_token_name(0), "mymodule::TokenA");
    EXPECT_EQ(restored->get_token_name(1), "mymodule::TokenB");
}

TEST_F(ModuleApiSaveTest, InvalidDataReturnsNull) {
    // nullptr / 0 → nullptr
    auto result = ModuleApi::fromMsgpack(nullptr, 0);
    EXPECT_EQ(result, nullptr);

    // Truncated data (< 13 bytes) → nullptr
    std::vector<uint8_t> short_data = {0, 1, 2, 3, 4, 5};
    result = ModuleApi::fromMsgpack(short_data.data(), short_data.size());
    EXPECT_EQ(result, nullptr);
}

TEST_F(ModuleApiSaveTest, CorruptedChecksumReturnsNull) {
    AttrId const_id = pool.builtin_id(BuiltinAttrKind::kConst);
    TokenInfo tok;
    tok.text = "corrupt_test";
    tok.add_attr(const_id);

    std::vector<TokenInfo*> tokens = {&tok};
    auto api = pool.export_attrs(tokens);
    ASSERT_NE(api, nullptr);

    // Сохраняем, загружаем, портим файл, загружаем снова
    ASSERT_TRUE(api->save_to_file(tempFile.string()));

    // Читаем файл, портим, записываем обратно
    std::fstream f(tempFile.string(), std::ios::in | std::ios::out | std::ios::binary);
    ASSERT_TRUE(f.is_open());
    f.seekg(0, std::ios::end);
    auto sz = f.tellg();
    ASSERT_GT(sz, 13);
    f.seekg(-1, std::ios::end);
    char last;
    f.read(&last, 1);
    last ^= 0xFF;
    f.seekp(-1, std::ios::end);
    f.write(&last, 1);
    f.close();

    auto restored = ModuleApi::load_from_file(tempFile.string());
    EXPECT_EQ(restored, nullptr);
}

TEST_F(ModuleApiSaveTest, AttrWithParamsRoundtrip) {
    AttrId trust_id = pool.builtin_id(BuiltinAttrKind::kTrust);
    TokenInfo tok;
    tok.text = "trust_token";
    tok.add_attr(trust_id);

    std::vector<TokenInfo*> tokens = {&tok};
    auto api = pool.export_attrs(tokens);
    ASSERT_NE(api, nullptr);
    ASSERT_EQ(api->token_count(), 1u);

    auto restored = roundtripThroughFile(*api, tempFile);
    ASSERT_NE(restored, nullptr);
    ASSERT_EQ(restored->token_count(), 1u);
    EXPECT_EQ(restored->get_token_name(0), "trust_token");
    EXPECT_TRUE(restored->has_attr("trust"));

    // Re-create the token in a fresh pool (by index)
    AttrPool fresh_pool;
    register_builtin_attrs(fresh_pool);
    auto recreated = restored->create_token(0, fresh_pool);
    ASSERT_NE(recreated, nullptr);
    auto re_trust = fresh_pool.lookup("trust");
    ASSERT_TRUE(re_trust.has_value());
    EXPECT_TRUE(recreated->has_attr(fresh_pool, re_trust.value()));
}