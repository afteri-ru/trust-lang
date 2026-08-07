// Test file: extracting embedded runtime headers from the trust-runtime library
// (both the static .a archive and the shared .so). Verifies readSectionFromLibrary
// and the buffer-based ELF parser in utils/elf.cpp.
#include "utils/elf.hpp"
#include <gtest/gtest.h>

#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

std::string readBinaryFile(const std::string& path) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) {
        return {};
    }
    return std::string((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
}

// Strip the trailing NUL(s) that #embed appends to the header bytes.
void stripTrailingNuls(std::vector<unsigned char>& data) {
    while (!data.empty() && data.back() == 0) {
        data.pop_back();
    }
}

// readSectionFromLibrary must find the embedded header inside the static archive,
// scanning the trust_headers.cpp.o member (long name stored via the // table).
TEST(ElfArchiveTest, StaticArchiveHeaderExtraction) {
    auto section = trust::utils::readSectionFromLibrary(TRUST_RUNTIME_STATIC_PATH, "trust/rational.hpp");
    ASSERT_TRUE(section.has_value());
    stripTrailingNuls(*section);
    std::string extracted(section->begin(), section->end());
    EXPECT_EQ(extracted, readBinaryFile(TRUST_RATIONAL_HPP_PATH));
}

// readSectionFromLibrary on the shared object exercises the single-ELF fast path.
TEST(ElfArchiveTest, SharedObjectHeaderExtraction) {
    auto section = trust::utils::readSectionFromLibrary(TRUST_RUNTIME_SHARED_PATH, "trust/rational.hpp");
    ASSERT_TRUE(section.has_value());
    stripTrailingNuls(*section);
    std::string extracted(section->begin(), section->end());
    EXPECT_EQ(extracted, readBinaryFile(TRUST_RATIONAL_HPP_PATH));
}

// Regression: parsing an ELF from an in-memory buffer matches parsing by path.
TEST(ElfArchiveTest, BufferExtractionMatchesFileExtraction) {
    std::ifstream ifs(TRUST_RUNTIME_SHARED_PATH, std::ios::binary);
    ASSERT_TRUE(ifs);
    std::vector<unsigned char> data((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());

    auto fromBuffer = trust::utils::readElfSectionFromBuffer(data, "trust/rational.hpp");
    auto fromFile = trust::utils::readElfSection(TRUST_RUNTIME_SHARED_PATH, "trust/rational.hpp");
    ASSERT_TRUE(fromBuffer.has_value());
    ASSERT_TRUE(fromFile.has_value());
    EXPECT_EQ(*fromBuffer, *fromFile);
}

// Missing sections (and, implicitly, the archive/ELF distinction) return nullopt.
TEST(ElfArchiveTest, MissingSectionReturnsNullopt) {
    EXPECT_FALSE(trust::utils::readSectionFromLibrary(TRUST_RUNTIME_STATIC_PATH, "no/such/section").has_value());
    EXPECT_FALSE(trust::utils::readSectionFromLibrary(TRUST_RUNTIME_SHARED_PATH, "no/such/section").has_value());
}

} // namespace
