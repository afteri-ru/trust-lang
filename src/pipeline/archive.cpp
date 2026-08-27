// src/pipeline/archive.cpp
// Сборка tar.gz-архива build-каталога (--emit-build-dir для trust-lsp).
// Модуль ArchiveBuilder (декомпозиция pipeline.cpp).
#include "pipeline/archive.hpp"
#include "pipeline/pipeline.hpp"
#include "trust/version.h"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#ifndef _WIN32
#include <errno.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
#include <string>
namespace trust {

// -- createTarGz: упаковка каталога dir в tar.gz (через системный tar, без шелла -
//    безопасно от shell-инъекций по путям). Архив должен лежать ВНЕ dir. --
static bool createTarGz(const std::filesystem::path& dir, const std::filesystem::path& archive, std::string& out_error) {
#ifndef _WIN32
    const pid_t pid = ::fork();
    if (pid < 0) {
        out_error = "fork failed";
        return false;
    }
    if (pid == 0) {
        // Тишина в дочернем процессе: stdout/stderr → /dev/null (tar пишет ошибки туда).
        const int devnull = ::open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            ::dup2(devnull, STDOUT_FILENO);
            ::dup2(devnull, STDERR_FILENO);
            ::close(devnull);
        }
        ::execlp("tar", "tar", "czf", archive.string().c_str(), "--exclude=*.o", "--exclude=*.so", "--exclude=app", "-C", dir.string().c_str(), ".",
                 static_cast<char*>(nullptr));
        ::_exit(127);
    }
    int status = 0;
    while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        out_error = "tar failed";
        return false;
    }
    return true;
#else
    (void)dir;
    (void)archive;
    out_error = "tar not supported on this platform";
    return false;
#endif
}

// -- emitBuildDirArchive: транспилирует trust_code и собирает tar.gz-архив build-каталога
//    (<emit_dir>/trust-lang-<версия>-generated.tar.gz) БЕЗ компиляции. Временные файлы
//    build-каталога удаляются (RAII). Возвращает путь к архиву; пусто при ошибке. --
std::filesystem::path emitBuildDirArchive(const std::string& trust_code, const std::filesystem::path& emit_dir, std::string& out_error) {
    namespace fs = std::filesystem;
    out_error.clear();
    std::error_code ec;

    fs::create_directories(emit_dir, ec);
    // build-каталог (временный; удаляется после архивации).
    const fs::path work = emit_dir / "work";
    fs::create_directories(work, ec);

    // Исходник - во временный .src для пайплайна.
    const fs::path src = work / "program.src";
    {
        std::ofstream ofs(src, std::ios::binary);
        if (!ofs) {
            out_error = "cannot create source file in " + emit_dir.string();
            return {};
        }
        ofs << trust_code;
    }

    // RAII: удалить каталог сборки при выходе (в т.ч. по ошибке) - чистка временных файлов.
    struct WorkDirGuard {
        fs::path p;
        ~WorkDirGuard() {
            std::error_code ec_;
            fs::remove_all(p, ec_);
        }
    } work_guard{work};

    trust::PipelineOpts opts;
    opts.input_file = src.string();
    opts.temp_dir = work.string();
    opts.compile_mode = CompileMode::Executable;
    opts.emit_build_dir_only = true;

    trust::Context ctx;
    trust::Pipeline pipeline(ctx, opts);
    try {
        if (pipeline.execute() != 0) {
            out_error = "build-dir emission failed (trust pipeline error)";
            return {};
        }
    } catch (const std::exception& e) {
        out_error = std::string("build-dir emission failed: ") + e.what();
        return {};
    }

    // Архив: tar.gz ВНЕ work (иначе tar видит изменение каталога при чтении).
    const fs::path archive = emit_dir / ("trust-lang-" + std::string(TRUST_VERSION_FULL) + "-generated.tar.gz");
    fs::remove(archive, ec);
    if (!createTarGz(work, archive, out_error)) {
        return {};
    }
    if (!fs::is_regular_file(archive, ec) || ec) {
        out_error = "archive not produced";
        return {};
    }
    return archive;
}
} // namespace trust
