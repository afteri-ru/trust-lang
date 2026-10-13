// src/toolchain/cache/hash.cpp
#include "toolchain/hash.hpp"
#include "utils/hash.hpp"
namespace trust {
namespace toolchain {
// -- Helper: MD5 hash for a file (единый utils-хелпер) --
std::string fileHash(const std::filesystem::path& path) {
    auto hash = utils::md5HexFile(path);
    return hash ? *hash : std::string("error");
}
} // namespace toolchain
} // namespace trust
