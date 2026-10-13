// src/pipeline/paths.cpp
// Расчёт путей build-каталога и генерируемого .cppt.
#include "pipeline/paths/paths.hpp"

#include "location/module_name.hpp"
#include "utils/error.hpp"
#include "utils/utils.hpp"
#include "utils/hash.hpp"

#include <filesystem>
#include <string>

namespace trust {

std::filesystem::path computeBuildDir(const PipelineOpts& opts) {
    namespace fs = std::filesystem;
    if (!opts.temp_dir.empty()) {
        // Явный --temp-dir: каталог создаётся здесь же, иначе emit/link падает на записи
        // (литеральный путь пользователя, без подкаталога .trust). Недоступный каталог
        // диагностируется на записи выходного файла ("failed to write output file").
        std::error_code ec;
        fs::create_directories(fs::path(opts.temp_dir), ec);
        return fs::path(opts.temp_dir);
    }
    fs::path src_path(opts.input_file);
    fs::path stem = src_path.stem();
    // Каталог `.trust`: для --run - <cwd>/.trust, иначе - <каталог исходника>/.trust. Итоговый
    // build_dir = <база>/.trust/<stem> (подкаталог по имени файла, чтобы Makefile/build.conf и
    // артефакты программ с одинаковым stem из разных каталогов не перезаписывали друг друга).
    // Артефакты НЕ кладём рядом с исходником - только в скрытый `.trust`, чтобы не
    // засорять исходное дерево.
    std::string base;
    if (opts.run) {
        // При запуске шебанга cwd == каталог исходника, поэтому файлы - в локальном <src_dir>/.trust/.
        // Общий каталог <cwd>/.trust/<stem> сохраняет сверку кеша --run по относительному пути
        // (A1, см. run_cache_same_stem).
        base = ".";
    } else {
        base = src_path.parent_path().empty() ? "." : src_path.parent_path().string();
    }
    fs::path dir = utils::resolveTempDir(base, /*create=*/true) / stem;
    std::error_code ec;
    fs::create_directories(dir, ec);
    return dir;
}

std::filesystem::path computeCpptPath(const PipelineOpts& opts) {
    namespace fs = std::filesystem;
    fs::path src_path(opts.input_file);
    fs::path stem = src_path.stem();
    return computeBuildDir(opts) / (stem.string() + ".cppt");
}

std::filesystem::path moduleCppPath(const std::filesystem::path& buildDir, std::string_view modulePath) {
    namespace fs = std::filesystem;
    // Имя артефакта — хеш пути модуля: детерминировано (не зависит от порядка загрузки) и
    // различает одноимённые файлы из разных каталогов.
    const fs::path stem = fs::path(std::string(modulePath)).stem();
    const std::string hash = utils::md5Hex(modulePath).substr(0, 12);
    return buildDir / (stem.string() + "-" + hash + ".cppt");
}

std::filesystem::path moduleHeaderPath(const std::filesystem::path& buildDir, std::string_view modulePath) {
    namespace fs = std::filesystem;
    // Имя заголовка — ТО ЖЕ кодированное имя, что namespace модуля: клиент включает ровно тот
    // заголовок, чей namespace он использует в сгенерированном C++ (одна формула на всё).
    const std::string encoded = moduleCppNamespace(modulePath);
    EXPECT(!encoded.empty() && "moduleHeaderPath requires an importable module path (with '\\' prefix)");
    return buildDir / (encoded + ".hpp");
}

} // namespace trust
