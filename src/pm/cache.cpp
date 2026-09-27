// src/pm/cache.cpp
#include "pm/cache.hpp"

#include "pm/protocol.hpp"

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <system_error>

namespace trust::pm {
namespace {

namespace fs = std::filesystem;

} // namespace

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
