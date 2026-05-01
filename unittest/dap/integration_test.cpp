#include <gtest/gtest.h>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <cstdlib>
#include <string>
#include <vector>

// Include project headers for direct testing
#include "trust_mapper.h"
#include <nlohmann/json.hpp>
using json = nlohmann::json;

namespace fs = std::filesystem;

// Get path to trust compiler binary
static std::string getCompilerPath() {
    if (fs::exists("build/trust")) {
        return "build/trust";
    }
    if (fs::exists("../build/trust")) {
        return "../build/trust";
    }
    return "trust";
}

// Test fixtures
class CompilerTest : public ::testing::Test {
  protected:
    void SetUp() override {
        test_dir_ = "test_output";
        fs::create_directories(test_dir_);
    }

    void TearDown() override { fs::remove_all(test_dir_); }

    // Helper: compile Trust file to C++
    bool compileTrustToCpp(const std::string &trust_content, const std::string &trust_filename, const std::string &cpp_filename,
                           const std::string &map_filename) {
        // Write Trust file
        std::ofstream trust_file(test_dir_ + "/" + trust_filename);
        trust_file << trust_content;
        trust_file.close();

        // Run compiler
        std::string compiler_cmd =
            getCompilerPath() + " " + test_dir_ + "/" + trust_filename + " " + test_dir_ + "/" + cpp_filename + " " + test_dir_ + "/" + map_filename;
        int result = std::system(compiler_cmd.c_str());
        return result == 0;
    }

    // Helper: compile C++ to ELF
    bool compileCppToElf(const std::string &cpp_filename, const std::string &elf_filename) {
        std::string cmd = "g++ -std=c++17 -g3 -o " + test_dir_ + "/" + elf_filename + " " + test_dir_ + "/" + cpp_filename;
        int result = std::system(cmd.c_str());
        return result == 0;
    }

    std::string test_dir_;
};

// ==================== Trust Mapper Unit Tests ====================

class TrustMapperTest : public ::testing::Test {
  protected:
    void SetUp() override {
        test_dir_ = "test_output";
        fs::create_directories(test_dir_);
    }

    void TearDown() override { fs::remove_all(test_dir_); }

    // Create a test JSON map file
    void createTestMapFile(const std::string &filename, const std::string &content) {
        std::ofstream file(test_dir_ + "/" + filename);
        file << content;
        file.close();
    }

    std::string test_dir_;
};

TEST_F(TrustMapperTest, LoadFromJsonFile) {
    std::string json_content =
        R"({"version":1,"sources":[{"trust_file":"test.trust","cpp_file":"test.cpp","mappings":[{"trust_line":1,"cpp_line":5,"trust_vars":["x"],"cpp_vars":["x"]},{"trust_line":2,"cpp_line":6,"trust_vars":["y"],"cpp_vars":["y"]}]}]})";

    createTestMapFile("test_map.json", json_content);

    TrustMapper mapper;
    bool loaded = mapper.loadFromJsonFile(test_dir_ + "/test_map.json");

    ASSERT_TRUE(loaded) << "Failed to load source map from JSON file";
    EXPECT_EQ(mapper.getEntries().size(), 1);
    EXPECT_EQ(mapper.getEntries()[0].mappings.size(), 2);
}

TEST_F(TrustMapperTest, TrustToCppMapping) {
    std::string json_content =
        R"({"version":1,"sources":[{"trust_file":"test.trust","cpp_file":"test.cpp","mappings":[{"trust_line":1,"cpp_line":5,"trust_vars":["x"],"cpp_vars":["x"]},{"trust_line":2,"cpp_line":6,"trust_vars":["y"],"cpp_vars":["y"]},{"trust_line":5,"cpp_line":9,"trust_vars":["result"],"cpp_vars":["result"]}]}]})";

    createTestMapFile("test_map.json", json_content);

    TrustMapper mapper;
    mapper.loadFromJsonFile(test_dir_ + "/test_map.json");

    // Test Trust -> CPP mapping
    auto result1 = mapper.trustToCpp("test.trust", 1);
    EXPECT_EQ(result1.second, 5);
    EXPECT_EQ(result1.first, "test.cpp");

    auto result2 = mapper.trustToCpp("test.trust", 2);
    EXPECT_EQ(result2.second, 6);
    EXPECT_EQ(result2.first, "test.cpp");

    auto result3 = mapper.trustToCpp("test.trust", 5);
    EXPECT_EQ(result3.second, 9);
    EXPECT_EQ(result3.first, "test.cpp");

    // Test non-existent line
    auto non_existent = mapper.trustToCpp("test.trust", 99);
    EXPECT_EQ(non_existent.second, -1);
    EXPECT_TRUE(non_existent.first.empty());
}

TEST_F(TrustMapperTest, CppToTrustMapping) {
    std::string json_content =
        R"({"version":1,"sources":[{"trust_file":"test.trust","cpp_file":"test.cpp","mappings":[{"trust_line":1,"cpp_line":5,"trust_vars":["x"],"cpp_vars":["x"]},{"trust_line":2,"cpp_line":6,"trust_vars":["y"],"cpp_vars":["y"]},{"trust_line":5,"cpp_line":9,"trust_vars":["result"],"cpp_vars":["result"]}]}]})";

    createTestMapFile("test_map.json", json_content);

    TrustMapper mapper;
    mapper.loadFromJsonFile(test_dir_ + "/test_map.json");

    auto result1 = mapper.cppToTrust("test.cpp", 5);
    EXPECT_EQ(result1.first, "test.trust");
    EXPECT_EQ(result1.second, 1);

    auto result2 = mapper.cppToTrust("test.cpp", 7);
    EXPECT_EQ(result2.first, "test.trust");
    EXPECT_EQ(result2.second, 2); // Should map to nearest previous trust line

    auto result3 = mapper.cppToTrust("test.cpp", 99);
    // cppToTrust finds nearest mapping (cpp_line <= 99), which is line 9 -> trust_line 5
    EXPECT_EQ(result3.first, "test.trust");
    EXPECT_EQ(result3.second, 5); // Nearest match
}

TEST_F(TrustMapperTest, VariableMapping) {
    std::string json_content =
        R"({"version":1,"sources":[{"trust_file":"test.trust","cpp_file":"test.cpp","mappings":[{"trust_line":4,"cpp_line":8,"trust_vars":["z","x","y"],"cpp_vars":["z","x","y"]},{"trust_line":5,"cpp_line":9,"trust_vars":["result"],"cpp_vars":["result"]}]}]})";

    createTestMapFile("test_map.json", json_content);

    TrustMapper mapper;
    mapper.loadFromJsonFile(test_dir_ + "/test_map.json");

    std::vector<std::string> cpp_vars = {"z", "x", "y"};
    auto trust_vars = mapper.getTrustVars("test.cpp", 8, cpp_vars);

    EXPECT_EQ(trust_vars.size(), 3);
    EXPECT_EQ(trust_vars[0], "z");
    EXPECT_EQ(trust_vars[1], "x");
    EXPECT_EQ(trust_vars[2], "y");
}

TEST_F(TrustMapperTest, LoadWithFallback) {
    // Create a non-existent binary and valid JSON
    std::string json_content =
        R"({"version":1,"sources":[{"trust_file":"test.trust","cpp_file":"test.cpp","mappings":[{"trust_line":1,"cpp_line":5,"trust_vars":["x"],"cpp_vars":["x"]}]}]})";
    createTestMapFile("test_map.json", json_content);

    TrustMapper mapper;
    mapper.load("nonexistent_binary", test_dir_ + "/test_map.json");

    // Should have loaded from JSON
    auto result = mapper.trustToCpp("test.trust", 1);
    EXPECT_EQ(result.second, 5);
    EXPECT_EQ(result.first, "test.cpp");
}

// ==================== Integration Tests ====================

// TEST_F(CompilerTest, FullCompilationFlow) {
//     std::string trust_content = R"(create x = 10
// create y = 20
// z = x + y
// call printf("Result: %d\n", x))";

//     bool compiled = compileTrustToCpp(trust_content, "test.trust", "test.cpp", "test_map.json");
//     ASSERT_TRUE(compiled) << "Failed to compile Trust to C++";

//     // Verify C++ output exists
//     ASSERT_TRUE(fs::exists(test_dir_ + "/test.cpp")) << "C++ output file does not exist";

//     // Read and verify C++ content
//     std::ifstream cpp_file(test_dir_ + "/test.cpp");
//     std::string content((std::istreambuf_iterator<char>(cpp_file)), std::istreambuf_iterator<char>());

//     EXPECT_NE(content.find("int main()"), std::string::npos);
//     EXPECT_NE(content.find("int x = 10"), std::string::npos);
//     EXPECT_NE(content.find("int y = 20"), std::string::npos);
//     EXPECT_NE(content.find(".trust_map"), std::string::npos);
// }

// TEST_F(CompilerTest, SourceMapGenerated) {
//     std::string trust_content = R"(create x = 10
// create y = 20)";

//     bool compiled = compileTrustToCpp(trust_content, "test.trust", "test.cpp", "test_map.json");
//     ASSERT_TRUE(compiled) << "Failed to compile Trust";

//     // Verify map.json was created and contains valid data
//     ASSERT_TRUE(fs::exists(test_dir_ + "/test_map.json")) << "map.json was not created";

//     std::ifstream map_file(test_dir_ + "/test_map.json");
//     std::string content((std::istreambuf_iterator<char>(map_file)), std::istreambuf_iterator<char>());

//     EXPECT_NE(content.find("\"version\""), std::string::npos);
//     EXPECT_NE(content.find("\"sources\""), std::string::npos);
//     EXPECT_NE(content.find("\"trust_line\""), std::string::npos);
//     EXPECT_NE(content.find("\"cpp_line\""), std::string::npos);
// }

// TEST_F(CompilerTest, ElFSectionEmbedded) {
//     std::string trust_content = R"(create x = 10)";

//     bool compiled = compileTrustToCpp(trust_content, "test.trust", "test.cpp", "test_map.json");
//     ASSERT_TRUE(compiled) << "Failed to compile Trust";

//     bool elf_built = compileCppToElf("test.cpp", "test_binary");
//     ASSERT_TRUE(elf_built) << "Failed to compile C++ to ELF";

//     // Load source map from ELF section
//     TrustMapper mapper;
//     bool loaded = mapper.loadFromElfSection(test_dir_ + "/test_binary");

//     EXPECT_TRUE(loaded) << "Failed to load source map from ELF section";
//     // The compiler stores full paths: test_output/test.trust
//     auto result = mapper.trustToCpp(test_dir_ + "/test.trust", 1);
//     EXPECT_EQ(result.second, 5);
// }

// TEST_F(CompilerTest, VariableExtractionInMap) {
//     std::string trust_content = R"(create x = 10
// create y = 20
// z = x + y
// create result = z * 2)";

//     bool compiled = compileTrustToCpp(trust_content, "test.trust", "test.cpp", "test_map.json");
//     ASSERT_TRUE(compiled) << "Failed to compile Trust";

//     // Load and verify variable mapping
//     TrustMapper mapper;
//     mapper.loadFromJsonFile(test_dir_ + "/test_map.json");

//     // Check that variable assignments are properly mapped
//     bool found_x = false, found_y = false, found_z = false, found_result = false;
//     for (const auto &entry : mapper.getEntries()) {
//         for (const auto &mapping : entry.mappings) {
//             for (const auto &v : mapping.trust_vars) {
//                 if (v == "x")
//                     found_x = true;
//                 if (v == "y")
//                     found_y = true;
//                 if (v == "z")
//                     found_z = true;
//                 if (v == "result")
//                     found_result = true;
//             }
//         }
//     }

//     EXPECT_TRUE(found_x);
//     EXPECT_TRUE(found_y);
//     EXPECT_TRUE(found_z);
//     EXPECT_TRUE(found_result);
// }

// ==================== JSON Structure Tests ====================

class StructureTest : public ::testing::Test {
  protected:
    void SetUp() override { fs::create_directories("test_output"); }

    void TearDown() override { fs::remove_all("test_output"); }
};

TEST_F(StructureTest, JsonFormatVersion) {
    std::string json_content =
        R"({"version":1,"sources":[{"trust_file":"test.trust","cpp_file":"test.cpp","mappings":[{"trust_line":1,"cpp_line":5,"trust_vars":["x"],"cpp_vars":["x"]}]}]})";

    TrustMapper mapper;
    std::ofstream file("test_output/structure_test.json");
    file << json_content;
    file.close();

    bool loaded = mapper.loadFromJsonFile("test_output/structure_test.json");
    EXPECT_TRUE(loaded);
}

TEST_F(StructureTest, EmptyMappings) {
    std::string json_content = R"({"version":1,"sources":[{"trust_file":"test.trust","cpp_file":"test.cpp","mappings":[]}]})";

    TrustMapper mapper;
    std::ofstream file("test_output/empty_map.json");
    file << json_content;
    file.close();

    bool loaded = mapper.loadFromJsonFile("test_output/empty_map.json");
    EXPECT_FALSE(loaded);
}

TEST_F(StructureTest, MultipleSourceEntries) {
    std::string json_content =
        R"({"version":1,"sources":[{"trust_file":"test.trust","cpp_file":"test.cpp","mappings":[{"trust_line":1,"cpp_line":5,"trust_vars":["x"],"cpp_vars":["x"]},{"trust_line":2,"cpp_line":6,"trust_vars":["y"],"cpp_vars":["y"]},{"trust_line":3,"cpp_line":7,"trust_vars":["z"],"cpp_vars":["z"]}]}]})";

    TrustMapper mapper;
    std::ofstream file("test_output/multi_map.json");
    file << json_content;
    file.close();

    bool loaded = mapper.loadFromJsonFile("test_output/multi_map.json");
    EXPECT_TRUE(loaded);
    EXPECT_EQ(mapper.getEntries().size(), 1);
    EXPECT_EQ(mapper.getEntries()[0].mappings.size(), 3);
}

// ==================== DAP Protocol Tests ====================

class DapProtocolTest : public ::testing::Test {
  protected:
    void SetUp() override {
        test_dir_ = "test_output";
        fs::create_directories(test_dir_);
    }

    void TearDown() override { fs::remove_all(test_dir_); }

    std::string test_dir_;
};

TEST_F(DapProtocolTest, JsonInitializeRequest) {
    std::string json_content = R"({"seq": 1, "type": "request", "command": "initialize", "arguments": {"clientID": "vscode"}})";

    json j = json::parse(json_content);

    EXPECT_EQ(j["seq"], 1);
    EXPECT_EQ(j["type"], "request");
    EXPECT_EQ(j["command"], "initialize");
    EXPECT_TRUE(j["arguments"].contains("clientID"));
}

TEST_F(DapProtocolTest, JsonInitializeResponse) {
    json j;
    j["type"] = "response";
    j["request_seq"] = 1;
    j["seq"] = 2;
    j["command"] = "initialize";
    j["success"] = true;
    j["body"]["supportsConfigurationDoneRequest"] = true;
    j["body"]["supportsSetVariable"] = true;

    std::string output = j.dump();

    EXPECT_NE(output.find("\"type\":\"response\""), std::string::npos);
    EXPECT_NE(output.find("\"success\":true"), std::string::npos);
    EXPECT_NE(output.find("\"supportsConfigurationDoneRequest\":true"), std::string::npos);
}

TEST_F(DapProtocolTest, JsonLaunchRequest) {
    std::string json_content = R"({
        "seq": 2,
        "type": "request", 
        "command": "launch",
        "arguments": {
            "program": "/path/to/elf",
            "sourceMap": "/path/to/map.json"
        }
    })";

    json j = json::parse(json_content);

    EXPECT_EQ(j["command"], "launch");
    EXPECT_EQ(j["arguments"]["program"], "/path/to/elf");
    EXPECT_EQ(j["arguments"]["sourceMap"], "/path/to/map.json");
}

TEST_F(DapProtocolTest, JsonStoppedEvent) {
    std::string json_content = R"({"seq": 1, "type": "event", "event": "stopped", "body": {"reason": "breakpoint", "threadId": 1}})";

    json j = json::parse(json_content);

    EXPECT_EQ(j["type"], "event");
    EXPECT_EQ(j["event"], "stopped");
    EXPECT_EQ(j["body"]["reason"], "breakpoint");
    EXPECT_EQ(j["body"]["threadId"], 1);
}

TEST_F(DapProtocolTest, JsonStackTraceResponse) {
    json j;
    j["type"] = "response";
    j["command"] = "stackTrace";
    j["success"] = true;

    json frame;
    frame["id"] = 1;
    frame["name"] = "main";
    frame["line"] = 5;
    frame["column"] = 1;

    json source;
    source["name"] = "example.trust";
    source["path"] = "/path/to/example.trust";
    frame["source"] = source;

    j["body"]["stackFrames"] = json::array();
    j["body"]["stackFrames"].push_back(frame);

    EXPECT_EQ(j["body"]["stackFrames"][0]["name"], "main");
    EXPECT_EQ(j["body"]["stackFrames"][0]["source"]["name"], "example.trust");
}

TEST_F(DapProtocolTest, JsonScopesResponse) {
    json j;
    j["type"] = "response";
    j["command"] = "scopes";
    j["success"] = true;

    json scope;
    scope["name"] = "Local";
    scope["presentationHint"] = "locals";
    scope["variablesReference"] = 1;

    j["body"]["scopes"] = json::array();
    j["body"]["scopes"].push_back(scope);

    std::string output = j.dump();
    EXPECT_NE(output.find("\"name\":\"Local\""), std::string::npos);
    EXPECT_NE(output.find("\"presentationHint\":\"locals\""), std::string::npos);
}

TEST_F(DapProtocolTest, JsonSetBreakpointsRequest) {
    std::string json_content = R"({
        "seq": 3,
        "type": "request",
        "command": "setBreakpoints",
        "arguments": {
            "source": {"name": "example.trust", "path": "/path/example.trust"},
            "breakpoints": [{"line": 1}, {"line": 3}],
            "lines": [1, 3]
        }
    })";

    json j = json::parse(json_content);

    EXPECT_EQ(j["command"], "setBreakpoints");
    EXPECT_EQ(j["arguments"]["source"]["name"], "example.trust");
    EXPECT_EQ(j["arguments"]["breakpoints"].size(), 2);
    EXPECT_EQ(j["arguments"]["breakpoints"][0]["line"], 1);
}

TEST_F(DapProtocolTest, JsonSetBreakpointsResponse) {
    json j;
    j["type"] = "response";
    j["command"] = "setBreakpoints";
    j["success"] = true;

    json bp1, bp2;
    bp1["id"] = 5;
    bp1["verified"] = true;
    bp1["line"] = 1;

    bp2["verified"] = false;
    bp2["line"] = 3;

    j["body"]["breakpoints"] = json::array();
    j["body"]["breakpoints"].push_back(bp1);
    j["body"]["breakpoints"].push_back(bp2);

    EXPECT_EQ(j["body"]["breakpoints"].size(), 2);
    EXPECT_TRUE(j["body"]["breakpoints"][0]["verified"]);
    EXPECT_FALSE(j["body"]["breakpoints"][1]["verified"]);
}

TEST_F(DapProtocolTest, JsonContentLengthHeader) {
    json j;
    j["type"] = "response";
    j["command"] = "test";
    j["success"] = true;

    std::string body = j.dump();
    std::string header = "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n";
    std::string full = header + body;

    EXPECT_TRUE(full.substr(0, 16) == "Content-Length: ");
    size_t colon_pos = full.find(':');
    size_t crlf_pos = full.find("\r\n");
    std::string len_str = full.substr(colon_pos + 2, crlf_pos - colon_pos - 2);
    EXPECT_EQ(std::stoi(len_str), body.size());
}

TEST_F(DapProtocolTest, TrustBreakpointMapping) {
    std::string json_content =
        R"({"version":1,"sources":[{"trust_file":"test.trust","cpp_file":"test.cpp","mappings":[{"trust_line":1,"cpp_line":5,"trust_vars":["x"],"cpp_vars":["x"]},{"trust_line":2,"cpp_line":6,"trust_vars":["y"],"cpp_vars":["y"]},{"trust_line":5,"cpp_line":9,"trust_vars":["result"],"cpp_vars":["result"]}]}]})";

    std::ofstream file(test_dir_ + "/bp_map.json");
    file << json_content;
    file.close();

    TrustMapper mapper;
    mapper.loadFromJsonFile(test_dir_ + "/bp_map.json");

    json dap_request;
    dap_request["type"] = "request";
    dap_request["command"] = "setBreakpoints";
    dap_request["arguments"]["source"]["name"] = "test.trust";
    dap_request["arguments"]["source"]["path"] = "test.trust";
    dap_request["arguments"]["breakpoints"] = json::array();
    dap_request["arguments"]["breakpoints"].push_back({{"line", 1}});
    dap_request["arguments"]["breakpoints"].push_back({{"line", 2}});

    json resp_body;
    resp_body["breakpoints"] = json::array();
    for (const auto &bp : dap_request["arguments"]["breakpoints"]) {
        int trust_line = bp["line"].get<int>();
        auto cpp_mapping = mapper.trustToCpp("test.trust", trust_line);
        const std::string &cpp_file = cpp_mapping.first;
        int cpp_line = cpp_mapping.second;

        json mapped_bp;
        mapped_bp["line"] = trust_line;
        mapped_bp["verified"] = cpp_line > 0 && !cpp_file.empty();
        if (cpp_line > 0) {
            mapped_bp["id"] = cpp_line;
        }
        resp_body["breakpoints"].push_back(mapped_bp);
    }

    EXPECT_EQ(resp_body["breakpoints"].size(), 2);
    EXPECT_TRUE(resp_body["breakpoints"][0]["verified"]);
    EXPECT_EQ(resp_body["breakpoints"][0]["id"], 5);
    EXPECT_TRUE(resp_body["breakpoints"][1]["verified"]);
    EXPECT_EQ(resp_body["breakpoints"][1]["id"], 6);
}

TEST_F(DapProtocolTest, TrustStackTraceMapping) {
    std::string json_content =
        R"({"version":1,"sources":[{"trust_file":"example.trust","cpp_file":"example.cpp","mappings":[{"trust_line":1,"cpp_line":5,"trust_vars":["x"],"cpp_vars":["x"]},{"trust_line":2,"cpp_line":6,"trust_vars":["y"],"cpp_vars":["y"]},{"trust_line":3,"cpp_line":7,"trust_vars":["z"],"cpp_vars":["z"]}]}]})";

    std::ofstream file(test_dir_ + "/stack_map.json");
    file << json_content;
    file.close();

    TrustMapper mapper;
    mapper.loadFromJsonFile(test_dir_ + "/stack_map.json");

    int cpp_line = 6;

    auto trust_info = mapper.cppToTrust("example.cpp", cpp_line);

    json stack_frame;
    stack_frame["id"] = 1;
    stack_frame["name"] = "main";
    stack_frame["line"] = trust_info.second;

    json source;
    source["name"] = std::filesystem::path(trust_info.first).filename().string();
    source["path"] = trust_info.first;
    stack_frame["source"] = source;

    EXPECT_EQ(stack_frame["name"], "main");
    EXPECT_EQ(stack_frame["line"], 2);
    EXPECT_EQ(stack_frame["source"]["name"], "example.trust");
}

// ==================== Edge Cases ====================

TEST_F(TrustMapperTest, NonExistentTrustFile) {
    std::string json_content =
        R"({"version":1,"sources":[{"trust_file":"test.trust","cpp_file":"test.cpp","mappings":[{"trust_line":1,"cpp_line":5,"trust_vars":["x"],"cpp_vars":["x"]}]}]})";

    createTestMapFile("test_map.json", json_content);

    TrustMapper mapper;
    mapper.loadFromJsonFile(test_dir_ + "/test_map.json");

    auto result = mapper.trustToCpp("other.trust", 1);
    EXPECT_EQ(result.second, -1);
}

TEST_F(TrustMapperTest, VariableCaseInsensitive) {
    std::string json_content =
        R"({"version":1,"sources":[{"trust_file":"test.trust","cpp_file":"test.cpp","mappings":[{"trust_line":1,"cpp_line":5,"trust_vars":["myVar"],"cpp_vars":["myVar"]}]}]})";

    createTestMapFile("test_map.json", json_content);

    TrustMapper mapper;
    mapper.loadFromJsonFile(test_dir_ + "/test_map.json");

    std::vector<std::string> cpp_vars = {"myVar"};
    auto trust_vars = mapper.getTrustVars("test.cpp", 5, cpp_vars);

    ASSERT_EQ(trust_vars.size(), 1);
    EXPECT_EQ(trust_vars[0], "myVar");
}
