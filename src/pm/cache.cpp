// src/pm/cache.cpp
#include "pm/cache.hpp"

#include "pm/protocol.hpp"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/SHA256.h"

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <system_error>

namespace trust::pm {
namespace {

namespace fs = std::filesystem;

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

std::string sha256File(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        throw PmException(PmError{"pm.cache.read", "cannot read file for hashing: " + file.string(), nlohmann::json{{"file", file.string()}}});
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

FileLock::FileLock(const fs::path& path) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    m_fd = ::open(path.c_str(), O_CREAT | O_RDWR, 0644);
    if (m_fd < 0) {
        throw PmException(
            PmError{"pm.cache.lock", "cannot open lock file: " + path.string(), nlohmann::json{{"lock", path.string()}, {"errno", std::strerror(errno)}}});
    }
    if (::flock(m_fd, LOCK_EX) != 0) {
        ::close(m_fd);
        m_fd = -1;
        throw PmException(
            PmError{"pm.cache.lock", "cannot acquire lock: " + path.string(), nlohmann::json{{"lock", path.string()}, {"errno", std::strerror(errno)}}});
    }
}

FileLock::~FileLock() {
    if (m_fd >= 0) {
        ::flock(m_fd, LOCK_UN);
        ::close(m_fd);
    }
}

Cache::Cache(fs::path root)
: m_root(std::move(root)) {
}

const fs::path& Cache::root() const {
    return m_root;
}

fs::path Cache::artifactPath(const std::string& sha256) const {
    return m_root / "artifacts" / sha256;
}

bool Cache::has(const std::string& sha256) const {
    std::error_code ec;
    return fs::is_regular_file(artifactPath(sha256), ec);
}

fs::path Cache::store(const std::string& sha256, const fs::path& source) {
    std::error_code ec;
    const fs::path target = artifactPath(sha256);
    if (fs::is_regular_file(target, ec)) {
        return target;
    }
    fs::create_directories(target.parent_path(), ec);
    const fs::path tmp_dir = m_root / "tmp";
    fs::create_directories(tmp_dir, ec);
    const fs::path tmp = tmp_dir / (sha256 + ".part");
    fs::copy_file(source, tmp, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        throw PmException(
            PmError{"pm.cache.store", "cannot stage artifact: " + source.string(), nlohmann::json{{"source", source.string()}, {"error", ec.message()}}});
    }
    fs::rename(tmp, target, ec);
    if (ec) {
        fs::remove(tmp, ec);
        throw PmException(
            PmError{"pm.cache.store", "cannot commit artifact: " + target.string(), nlohmann::json{{"target", target.string()}, {"error", ec.message()}}});
    }
    return target;
}

FileLock Cache::lock() const {
    return FileLock(m_root / "lock");
}

} // namespace trust::pm
