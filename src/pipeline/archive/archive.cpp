// src/pipeline/archive.cpp
// Сборка tar.gz-архива build-каталога (--emit-build-dir для trust-lsp).
// Модуль ArchiveBuilder (декомпозиция pipeline.cpp).
#include "pipeline/archive/archive.hpp"
#include "pipeline/pipeline.hpp"
#include "session/context.hpp"
#include "package/package.hpp"
#include "trust/version.h"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
namespace trust {

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
    // Скачиваемый архив песочницы генерирует entry так же, как `--run`: однофайловый режим
    // `-fsingle-file`. В нём «модуль-скрипт» (top-level операторы без явного `__main__`,
    // напр. `hello.src`) корректно оборачивается транспилятором в синтезируемую entry
    // `<модуль>__main__` + встроенный `int main()` (тот же реальный pipeline, что и `--run`).
    // Без него (многофайловый путь) такой код давал НЕкомпилируемый архив (top-level операторы
    // оставались на глобальном C++-уровне, а `<модуль>__main__` не эмитилась).
    opts.single_file = true;

    trust::Context ctx;
    trust::Pipeline pipeline(ctx, opts);
    try {
        const trust::MapperFile inputFile = ctx.source().load_file(opts.input_file);
        if (pipeline.compileProgram(inputFile) != 0) {
            out_error = "build-dir emission failed (trust pipeline error)";
            return {};
        }
    } catch (const std::exception& e) {
        out_error = std::string("build-dir emission failed: ") + e.what();
        return {};
    }

    // Архив: tar.gz ВНЕ work (иначе tar видит изменение каталога при чтении).
    const fs::path archive = emit_dir / ("trust-lang-" + std::string(TRUST_VERSION) + "-generated.tar.gz");
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
