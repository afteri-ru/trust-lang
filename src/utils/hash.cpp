// src/utils/hash.cpp
#include "utils/hash.hpp"

#include "utils/error.hpp"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/SHA256.h"

#include <array>
#include <fstream>

namespace trust::utils {
namespace {

std::string toHex(llvm::ArrayRef<uint8_t> bytes) {
    static const char* kHex = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (const uint8_t byte : bytes) {
        out.push_back(kHex[byte >> 4]);
        out.push_back(kHex[byte & 0x0F]);
    }
    return out;
}

} // namespace

std::string sha256Hex(std::string_view data) {
    llvm::SHA256 hasher;
    hasher.update(llvm::ArrayRef<uint8_t>(reinterpret_cast<const uint8_t*>(data.data()), data.size()));
    const std::array<uint8_t, 32> hash = hasher.final();
    return toHex(hash);
}

std::string sha256File(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        FAULT("cannot read file for hashing: {}", file.string());
    }
    llvm::SHA256 hasher;
    std::array<char, 65536> buffer{};
    while (in) {
        in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize got = in.gcount();
        if (got > 0) {
            hasher.update(llvm::ArrayRef<uint8_t>(reinterpret_cast<const uint8_t*>(buffer.data()), static_cast<std::size_t>(got)));
        }
    }
    const std::array<uint8_t, 32> hash = hasher.final();
    return toHex(hash);
}

} // namespace trust::utils
