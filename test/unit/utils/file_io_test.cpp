#include "utils/file_io.hpp"

#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

struct FileIOTest : ::testing::Test {
    fs::path tempDir;
    fs::path testFile;

    void SetUp() override {
        tempDir = fs::temp_directory_path() / "file_io_test_XXXXXX";
        for (int i = 0; i < 100; ++i) {
            auto p = fs::temp_directory_path() / ("file_io_test_" + std::to_string(i));
            if (!fs::exists(p)) {
                tempDir = p;
                break;
            }
        }
        fs::create_directories(tempDir);
        testFile = tempDir / "test.bin";
    }

    void TearDown() override { fs::remove_all(tempDir); }
};

TEST_F(FileIOTest, write_and_read_string) {
    const std::string testData = "Hello, World!";

    ASSERT_TRUE(trust::utils::FileIO::write(testFile.string(), testData));

    auto result = trust::utils::FileIO::read<std::string>(testFile.string());
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, testData);
}

TEST_F(FileIOTest, read_string_nonexistent_file) {
    auto result = trust::utils::FileIO::read<std::string>("/nonexistent/path/file.txt");
    EXPECT_FALSE(result.has_value());
}

TEST_F(FileIOTest, write_and_read_vector_char) {
    const std::vector<char> testData = {'a', 'b', 'c', 0, 'd', 'e'};

    ASSERT_TRUE(trust::utils::FileIO::write(testFile.string(), testData));

    auto result = trust::utils::FileIO::read<std::vector<char>>(testFile.string());
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, testData);
}

TEST_F(FileIOTest, read_nonexistent_file) {
    auto result = trust::utils::FileIO::read<std::vector<char>>("/nonexistent/path/file.txt");
    EXPECT_FALSE(result.has_value());
}

TEST_F(FileIOTest, write_to_invalid_path) {
    const std::string data = "test";
    EXPECT_FALSE(trust::utils::FileIO::write("/nonexistent/directory/file.txt", data));
}

TEST_F(FileIOTest, read_empty_file) {
    std::ofstream ofs(testFile.string());
    ofs.close();

    auto result = trust::utils::FileIO::read<std::vector<char>>(testFile.string());
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->empty());
}

TEST_F(FileIOTest, roundtrip_binary_data) {
    const std::vector<char> testData = {0x00, 0x01, 0x02, static_cast<char>(0xFF), static_cast<char>(0xFE), static_cast<char>(0xFD)};

    ASSERT_TRUE(trust::utils::FileIO::write(testFile.string(), testData));

    auto result = trust::utils::FileIO::read<std::vector<char>>(testFile.string());
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), testData.size());
    EXPECT_TRUE(memcmp(result->data(), testData.data(), testData.size()) == 0);
}

TEST_F(FileIOTest, overwrite_existing_file) {
    const std::string first = "first content";
    const std::string second = "second";

    ASSERT_TRUE(trust::utils::FileIO::write(testFile.string(), first));
    ASSERT_TRUE(trust::utils::FileIO::write(testFile.string(), second));

    auto result = trust::utils::FileIO::read<std::vector<char>>(testFile.string());
    ASSERT_TRUE(result.has_value());
    std::string resultStr(result->data(), result->size());
    EXPECT_EQ(resultStr, second);
}

TEST_F(FileIOTest, write_string_view) {
    const std::string_view testData = "Hello via string_view!";

    ASSERT_TRUE(trust::utils::FileIO::write(testFile.string(), testData));

    auto result = trust::utils::FileIO::read<std::vector<char>>(testFile.string());
    ASSERT_TRUE(result.has_value());
    std::string resultStr(result->data(), result->size());
    EXPECT_EQ(resultStr, testData);
}