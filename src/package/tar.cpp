// src/package/tar.cpp
// tar-примитивы пакета: запуск системного tar, листинг/чтение/распаковка, gzip-подпись,
// упаковка каталога в tar.gz. Не знает о манифесте и раскладке слоёв.
#include "package/archive/archive_io.hpp"
#include "package/package.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#ifndef _WIN32
#include <errno.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace trust {
namespace {

namespace fs = std::filesystem;

#ifndef _WIN32

/// argv[0] = "tar", далее args; null-терминирован.
std::vector<char*> makeArgv(const std::vector<std::string>& args) {
    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    for (const std::string& a : args) {
        argv.push_back(const_cast<char*>(a.c_str()));
    }
    argv.push_back(nullptr);
    return argv;
}

/// Запуск `tar <args...>`. captured != nullptr -> stdout захватывается; иначе stdout -> /dev/null.
/// stderr всегда -> /dev/null. false при fork/pipe/ненулевом exit.
bool runTar(const std::vector<std::string>& args, std::string* captured, std::string& out_error) {
    int pipefd[2] = {-1, -1};
    if (captured != nullptr && ::pipe(pipefd) != 0) {
        out_error = "pipe failed";
        return false;
    }
    const pid_t pid = ::fork();
    if (pid < 0) {
        if (captured != nullptr) {
            ::close(pipefd[0]);
            ::close(pipefd[1]);
        }
        out_error = "fork failed";
        return false;
    }
    if (pid == 0) {
        if (captured != nullptr) {
            ::close(pipefd[0]);
            ::dup2(pipefd[1], STDOUT_FILENO);
            ::close(pipefd[1]);
        } else {
            const int devnull = ::open("/dev/null", O_WRONLY);
            if (devnull >= 0) {
                ::dup2(devnull, STDOUT_FILENO);
                ::close(devnull);
            }
        }
        const int devnull_err = ::open("/dev/null", O_WRONLY);
        if (devnull_err >= 0) {
            ::dup2(devnull_err, STDERR_FILENO);
            ::close(devnull_err);
        }
        std::vector<char*> argv = makeArgv(args);
        ::execvp("tar", argv.data());
        ::_exit(127);
    }
    if (captured != nullptr) {
        ::close(pipefd[1]);
        captured->clear();
        char buf[4096];
        ssize_t n = 0;
        while ((n = ::read(pipefd[0], buf, sizeof(buf))) > 0) {
            captured->append(buf, static_cast<std::size_t>(n));
        }
        ::close(pipefd[0]);
    }
    int status = 0;
    while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        out_error = "tar failed";
        return false;
    }
    return true;
}

#endif // _WIN32

} // namespace

namespace package::detail {

bool isSafeEntry(std::string_view entry) {
    if (entry.empty() || entry.front() == '/') {
        return false;
    }
    std::size_t start = 0;
    while (start <= entry.size()) {
        const std::size_t slash = entry.find('/', start);
        const std::string_view comp = (slash == std::string_view::npos) ? entry.substr(start) : entry.substr(start, slash - start);
        if (comp == "..") {
            return false;
        }
        if (slash == std::string_view::npos) {
            break;
        }
        start = slash + 1;
    }
    return true;
}

std::string_view stripDotSlash(std::string_view entry) {
    if (entry.starts_with("./")) {
        return entry.substr(2);
    }
    return entry;
}

#ifndef _WIN32

bool listEntries(const fs::path& archive, std::vector<std::string>& out_entries, std::string& out_error) {
    std::string listing;
    if (!runTar({"tar", "tzf", archive.string()}, &listing, out_error)) {
        return false;
    }
    out_entries.clear();
    std::istringstream iss(listing);
    std::string entry;
    while (std::getline(iss, entry)) {
        if (!entry.empty()) {
            out_entries.push_back(entry);
        }
    }
    return true;
}

bool readEntry(const fs::path& archive, std::string_view entry, std::string& out_content, std::string& out_error) {
    return runTar({"tar", "xzf", archive.string(), "-O", std::string(entry)}, &out_content, out_error);
}

bool extractArchive(const fs::path& archive, const fs::path& dest, std::string& out_error) {
    std::vector<std::string> entries;
    if (!listEntries(archive, entries, out_error)) {
        return false;
    }
    for (const std::string& entry : entries) {
        if (!isSafeEntry(entry)) {
            out_error = "unsafe archive entry '" + entry + "'";
            return false;
        }
    }
    std::error_code ec;
    fs::create_directories(dest, ec);
    if (ec) {
        out_error = "cannot create destination '" + dest.string() + "'";
        return false;
    }
    return runTar({"tar", "xzf", archive.string(), "-C", dest.string()}, nullptr, out_error);
}

#else // _WIN32

bool listEntries(const fs::path&, std::vector<std::string>&, std::string& out_error) {
    out_error = "tar not supported on this platform";
    return false;
}

bool readEntry(const fs::path&, std::string_view, std::string&, std::string& out_error) {
    out_error = "tar not supported on this platform";
    return false;
}

bool extractArchive(const fs::path&, const fs::path&, std::string& out_error) {
    out_error = "tar not supported on this platform";
    return false;
}

#endif // _WIN32

} // namespace package::detail

bool isTrustPackage(const fs::path& path) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) {
        return false;
    }
    unsigned char magic[2] = {0, 0};
    ifs.read(reinterpret_cast<char*>(magic), 2);
    return ifs.gcount() == 2 && magic[0] == 0x1f && magic[1] == 0x8b;
}

bool createTarGz(const fs::path& dir, const fs::path& archive, std::string& out_error) {
#ifndef _WIN32
    // Воспроизводимый архив (порядок, mtime, owner): иначе md5 stdlib.trust менялся бы
    // при каждом репаке и ломал ccache повторных прогонов (dep-хэш stdlib в .cppt).
    return runTar({"tar", "czf", archive.string(), "--sort=name", "--numeric-owner", "--owner=0", "--group=0", "--mtime=@0", "--exclude=*.o", "--exclude=*.so",
                   "--exclude=app", "-C", dir.string(), "."},
                  nullptr, out_error);
#else
    (void)dir;
    (void)archive;
    out_error = "tar not supported on this platform";
    return false;
#endif
}

} // namespace trust
