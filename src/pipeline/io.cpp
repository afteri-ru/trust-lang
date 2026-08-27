// src/pipeline/io.cpp
#include "pipeline/io.hpp"
#include "utils/file_io.hpp"
#include "llvm/Support/MD5.h"
#include <chrono>
#include <iomanip>
#include <sstream>
namespace trust {
// -- Helper: current timestamp string --
std::string currentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    localtime_r(&time_t_now, &tm);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S %z");
    return oss.str();
}

// -- Helper: MD5 hash for a file (uses llvm::MD5Hash, same as FileEntry::getHash) --
std::string fileHash(const std::filesystem::path& path) {
    auto content = trust::utils::FileIO::read<std::string>(path.string());
    if (!content) {
        return "error";
    }
    auto hash = llvm::MD5Hash(*content);
    std::ostringstream oss;
    oss << std::hex << std::setw(16) << std::setfill('0') << hash;
    return oss.str();
}
} // namespace trust
