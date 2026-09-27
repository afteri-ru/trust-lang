// test/pm/integration.cpp - end-to-end test of the trust-pm executable.
// Usage: pm_integration <path-to-trust-pm> <tmp-dir>
#include <sys/wait.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

namespace {

struct RunResult {
    int code = -1;
    std::string output;
};

RunResult run(const std::string& command) {
    RunResult result;
    FILE* pipe = popen((command + " 2>&1").c_str(), "r");
    if (pipe == nullptr) {
        return result;
    }
    char buffer[4096];
    while (std::fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result.output += buffer;
    }
    const int status = pclose(pipe);
    result.code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return result;
}

void writeFile(const fs::path& path, const std::string& data) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << data;
}

std::string readFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

bool check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
    }
    return condition;
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "usage: pm_integration <trust-pm> <work-dir>\n";
        return 2;
    }
    const std::string pm = argv[1];
    // Work in a dedicated subdirectory: the parent is the build directory that
    // holds this test binary and must not be wiped.
    const fs::path tmp = fs::path(argv[2]) / "work";
    std::error_code ec;
    fs::remove_all(tmp, ec);
    fs::create_directories(tmp, ec);

    const fs::path root = tmp / "root";
    const fs::path source = tmp / "src";
    const fs::path cache = tmp / "cache";
    writeFile(root / "trust.pkg.json", "{\"schema\":1,\"name\":\"acme/app\",\"version\":\"0.1.0\",\"dependencies\":{\"acme/bar\":\"^1.0\"}}\n");

    const fs::path bar110 = tmp / "bar110";
    writeFile(bar110 / "trust.pkg.json", "{\"schema\":1,\"name\":\"acme/bar\",\"version\":\"1.1.0\",\"dependencies\":{}}\n");
    writeFile(bar110 / "bar.src", "# bar 1.1.0\n");

    bool ok = true;
    const fs::path a = tmp / "a.trust";
    const fs::path b = tmp / "b.trust";
    ok &= check(run(pm + " pack " + bar110.string() + " -o " + a.string() + " --json").code == 0, "pack a");
    ok &= check(run(pm + " pack " + bar110.string() + " -o " + b.string() + " --json").code == 0, "pack b");
    ok &= check(readFile(a) == readFile(b), "deterministic packing (same bytes)");

    fs::create_directories(source, ec);
    ok &= check(run(pm + " pack " + bar110.string() + " -o " + (source / "bar-1.1.0.trust").string()).code == 0, "pack into source");

    const RunResult resolve = run(pm + " resolve --manifest " + (root / "trust.pkg.json").string() + " --source " + source.string() + " --json");
    ok &= check(resolve.code == 0, "resolve exit code");
    ok &= check(resolve.output.find("\"ok\": true") != std::string::npos, "resolve ok envelope");
    ok &= check(resolve.output.find("1.1.0") != std::string::npos, "resolve picked 1.1.0");

    ok &=
        check(run(pm + " install --manifest " + (root / "trust.pkg.json").string() + " --source " + source.string() + " --cache " + cache.string() + " --json")
                      .code == 0,
              "install");

    fs::remove_all(source, ec);
    const RunResult offline = run(pm + " install --manifest " + (root / "trust.pkg.json").string() + " --source " + source.string() + " --cache " +
                                  cache.string() + " --frozen --offline --json");
    ok &= check(offline.code == 0, "frozen+offline install from cache");
    ok &= check(offline.output.find("\"ok\": true") != std::string::npos, "offline ok envelope");

    if (ok) {
        std::cout << "pm_integration: OK\n";
        return 0;
    }
    return 1;
}
