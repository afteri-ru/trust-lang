// src/toolchain/runtime/runtime_locator.cpp
#include "toolchain/runtime/runtime_locator.hpp"
#include "utils/utils.hpp"
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>
namespace trust {
namespace toolchain {

std::filesystem::path runtimeLibraryFileName(RuntimeLink link) {
    namespace fs = std::filesystem;
    return link == RuntimeLink::Static ? fs::path("trust-runtime.a") : fs::path("trust-runtime.so");
}

std::filesystem::path locateRuntimeLibrary(RuntimeLink link) {
    namespace fs = std::filesystem;
    const fs::path kLibName = runtimeLibraryFileName(link);

    // Search <base>/{kLibName, lib/kLibName, ../lib/kLibName}.
    auto searchIn = [&](const fs::path& base) -> fs::path {
        std::error_code ec;
        for (const fs::path& rel : {kLibName, fs::path("lib") / kLibName, fs::path("..") / "lib" / kLibName}) {
            fs::path cand = base / rel;
            if (fs::is_regular_file(cand, ec)) {
                return cand;
            }
        }
        return {};
    };

    // 1) Directory of the real executable (symlinks resolved via /proc/self/exe).
    {
        const fs::path exe = utils::executablePath();
        if (!exe.empty()) {
            if (fs::path p = searchIn(exe.parent_path()); !p.empty()) {
                return p;
            }
        }
    }
    // 2) Directories from LD_LIBRARY_PATH.
    if (const char* llp = std::getenv("LD_LIBRARY_PATH")) {
        std::string paths(llp);
        size_t pos = 0;
        while (pos <= paths.size()) {
            size_t sep = paths.find(':', pos);
            std::string dir = paths.substr(pos, sep == std::string::npos ? std::string::npos : sep - pos);
            if (!dir.empty()) {
                if (fs::path p = searchIn(dir); !p.empty()) {
                    return p;
                }
            }
            if (sep == std::string::npos) {
                break;
            }
            pos = sep + 1;
        }
    }
    // 3) Current working directory.
    {
        std::error_code ec;
        fs::path cwd = fs::current_path(ec);
        if (!ec) {
            if (fs::path p = searchIn(cwd); !p.empty()) {
                return p;
            }
        }
    }
    return {};
}

namespace {

/// Каталоги поиска toolchain-библиотек: exe, exe/lib, exe/../lib, LD_LIBRARY_PATH, cwd.
std::vector<std::filesystem::path> toolchainSearchDirs() {
    namespace fs = std::filesystem;
    std::vector<fs::path> dirs;
    const fs::path exe = utils::executablePath();
    if (!exe.empty()) {
        const fs::path base = exe.parent_path();
        dirs.push_back(base);
        dirs.push_back(base / "lib");
        dirs.push_back(base / ".." / "lib");
    }
    if (const char* llp = std::getenv("LD_LIBRARY_PATH")) {
        std::string paths(llp);
        std::size_t pos = 0;
        while (pos <= paths.size()) {
            const std::size_t sep = paths.find(':', pos);
            const std::string dir = paths.substr(pos, sep == std::string::npos ? std::string::npos : sep - pos);
            if (!dir.empty()) {
                dirs.emplace_back(dir);
            }
            if (sep == std::string::npos) {
                break;
            }
            pos = sep + 1;
        }
    }
    std::error_code ec;
    const fs::path cwd = fs::current_path(ec);
    if (!ec) {
        dirs.push_back(cwd);
    }
    return dirs;
}

} // namespace

std::filesystem::path locateToolchainLibrary(std::string_view name) {
    namespace fs = std::filesystem;
    if (name.empty()) {
        return {};
    }
    const std::vector<std::string> candidates = {
        "lib" + std::string(name) + ".a",
        std::string(name) + ".a",
        "lib" + std::string(name) + ".so",
        std::string(name) + ".so",
    };
    std::error_code ec;
    for (const fs::path& dir : toolchainSearchDirs()) {
        for (const std::string& file : candidates) {
            const fs::path cand = dir / file;
            if (fs::is_regular_file(cand, ec)) {
                return fs::weakly_canonical(cand, ec);
            }
        }
    }
    return {};
}

} // namespace toolchain
} // namespace trust
