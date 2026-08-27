// src/pipeline/runtime_locator.cpp
#include "pipeline/runtime_locator.hpp"
#include "utils/io.hpp"
#include "utils/elf.hpp"
#include <cstdlib>
#include <filesystem>
#include <fstream>
namespace trust {

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
        std::error_code ec;
        fs::path exe = fs::canonical("/proc/self/exe", ec);
        if (!ec && !exe.empty()) {
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

bool extractRuntimeHeader(const std::string& headerPath, const std::filesystem::path& buildDir, RuntimeLink link) {
    namespace fs = std::filesystem;
    const fs::path lib_path = locateRuntimeLibrary(link);
    if (lib_path.empty()) {
        trust::errs() << "error: cannot locate trust-runtime library (" << runtimeLibraryFileName(link).string()
                      << "; set LD_LIBRARY_PATH or run from the build directory)\n";
        return false;
    }
    auto section = trust::utils::readSectionFromLibrary(lib_path.string(), headerPath);
    if (!section) {
        trust::errs() << "error: runtime header '" << headerPath << "' not found in " << lib_path.string() << "\n";
        return false;
    }
    // Отбросить хвостовой '\0', добавленный при #embed.
    while (!section->empty() && section->back() == '\0') {
        section->pop_back();
    }

    fs::path outPath = buildDir / headerPath;
    std::error_code ec;
    fs::create_directories(outPath.parent_path(), ec);
    std::ofstream ofs(outPath, std::ios::binary);
    if (!ofs) {
        trust::errs() << "error: cannot write runtime header '" << outPath << "'\n";
        return false;
    }
    ofs.write(reinterpret_cast<const char*>(section->data()), static_cast<std::streamsize>(section->size()));
    return static_cast<bool>(ofs);
}
} // namespace trust
