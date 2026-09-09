// test/unit/pipeline/pipeline_test.cpp - unit-тесты конвейера (emitBuildDirArchive и пр.).
#include "pipeline/pipeline.hpp"
#include "driver/cli.hpp"
#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unistd.h>

#include "test_data.hpp"

using namespace trust;

namespace {

// Читает текстовый файл целиком (для проверки содержимого извлечённого build-каталога).
static std::string read_text_file(const std::filesystem::path& p) {
    std::ifstream ifs(p);
    std::ostringstream ss;
    ss << ifs.rdbuf();
    return ss.str();
}

} // anonymous namespace

// pipeline: Makefile/build.conf/.cppt/trust/) и удаляет временные файлы. Проверяем, что
// архив существует/непуст и временный build-каталог удалён (RAII-очистка).
TEST(PipelineTest, EmitBuildDirArchiveCleansTempFiles) {
    namespace fs = std::filesystem;
    const fs::path emit_dir = trust::test::makeTestDataDir("pipeline_emit_archive");

    std::string err;
    const fs::path archive = trust::emitBuildDirArchive("print(\"hello\");\n", emit_dir, err);
    EXPECT_TRUE(!archive.empty()) << "err: " << err;
    if (!archive.empty()) {
        EXPECT_TRUE(fs::is_regular_file(archive));
        EXPECT_GT(fs::file_size(archive), 0u);
        // Временный build-каталог (work) должен быть удалён после архивации.
        EXPECT_FALSE(fs::exists(emit_dir / "work"));
    }
}

// Регрессия: скачиваемый архив песочницы (--emit-build-dir) генерируется в ОДНОФАЙЛОВОМ
// режиме (-fsingle-file, как --run). «Модуль-скрипт» (top-level операторы без @main) обязан
// давать компилируемый build-каталог: top-level оборачивается в синтезируемую entry
// `program__main__` + обёртку `int main()` В ТОМ ЖЕ .cppt; отдельный `_main.cppt` / `SRC_MAIN`
// не создаются. Раньше (многофайловый путь) такой код давал архив с НЕкомпилируемым C++.
TEST(PipelineTest, EmitBuildDirArchiveModuleScriptIsSingleFile) {
    namespace fs = std::filesystem;
    const fs::path emit_dir = trust::test::makeTestDataDir("pipeline_emit_script");

    // top-level «модуль-скрипт» без явной entry (@main отсутствует).
    const std::string script = "@func triple(x:Int32):Int32 { return x * 3; };\n"
                               "v := triple(4);\n"
                               "@print('v={}\\n', v);\n";

    std::string err;
    const fs::path archive = trust::emitBuildDirArchive(script, emit_dir, err);
    ASSERT_FALSE(archive.empty()) << "err: " << err;
    ASSERT_TRUE(fs::is_regular_file(archive));

    // Распаковываем архив и проверяем состав build-каталога.
    const fs::path extract = emit_dir / "extract";
    fs::create_directories(extract);
    const std::string cmd = "tar -xzf '" + archive.string() + "' -C '" + extract.string() + "'";
    ASSERT_EQ(std::system(cmd.c_str()), 0);

    // Отдельный entry-файл `_main.cppt` не создаётся.
    bool foundMainCppt = false;
    for (const auto& e : fs::directory_iterator(extract)) {
        const std::string name = e.path().filename().string();
        if (name.size() > 5 && name.rfind("_main.cppt") == name.size() - 10 /* length("_main.cppt") */) {
            foundMainCppt = true;
        }
    }
    EXPECT_FALSE(foundMainCppt) << "unexpected separate _main.cppt in single-file archive";

    const std::string conf = read_text_file(extract / "build.conf");
    EXPECT_NE(conf.find("SRC       := program.cppt"), std::string::npos) << conf;
    EXPECT_EQ(conf.find("SRC_MAIN"), std::string::npos) << "single-file archive must not set SRC_MAIN:\n" << conf;

    // Синтезированная entry + обёртка main встроены в основной .cppt.
    const std::string cppt = read_text_file(extract / "program.cppt");
    EXPECT_NE(cppt.find("program__main__"), std::string::npos) << "synthesized module-script entry missing";
    EXPECT_NE(cppt.find("int main()"), std::string::npos) << "main wrapper must be inlined into program.cppt";
}
