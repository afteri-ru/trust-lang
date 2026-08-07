#include "diag/context.hpp"
#include "location/location.hpp"
#include "diag/mapper.hpp"

#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace trust;
namespace fs = std::filesystem;

// ═══════════════════════════════════════════════════════════════
//  save_output + readFilesFromDisk: roundtrip test
// ═══════════════════════════════════════════════════════════════

TEST(SaveAndReadTest, SaveOutputThenReadFromDisk) {
    // Используем TEST_DATA_DIR (_build/test_data/) для временных файлов
    fs::path dataDir = fs::path(TEST_DATA_DIR) / "save_read_test";
    fs::create_directories(dataDir);
    std::string outputPath = (dataDir / "output.cpp").generic_string();
    std::string inputPath = (dataDir / "test.src").generic_string();

    // Создаём входной файл на диске — он понадобится readFilesFromDisk после десериализации
    {
        std::ofstream ofs(inputPath, std::ios::out | std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(ofs);
        ofs << "create x = 42;";
    }

    Context ctx(dataDir.generic_string());

    // Загружаем входной файл с диска — в msgpack попадёт абсолютный путь
    MapperFile inputIdx = ctx.source().load_file(inputPath);
    EXPECT_FALSE(inputIdx.isInvalid());
    EXPECT_EQ(ctx.source().source(inputIdx), "create x = 42;");

    // Добавляем выходной файл и пишем в него
    // Передаём полный путь — normalizePath приведёт его к относительному от baseDir (dataDir)
    MapperFile outIdx = ctx.source().add_output(outputPath, true); // полный путь → нормализуется в "output.cpp"
    ctx.source().output_append(outIdx, "int x = 42;\n");

    // Добавляем ещё один выходной файл в подкаталоге
    std::string subdirOutputPath = (dataDir / "lib" / "helpers.cpp").generic_string();
    MapperFile subdirIdx = ctx.source().add_output(subdirOutputPath, true); // → нормализуется в "lib/helpers.cpp"
    ctx.source().output_append(subdirIdx, "int helper() { return 0; }\n");

    // Сохраняем все выходные файлы на диск
    ASSERT_TRUE(ctx.source().save_output(dataDir.generic_string()));

    // Читаем сохранённые файлы и проверяем содержимое
    {
        std::ifstream ifs(outputPath, std::ios::in | std::ios::binary);
        ASSERT_TRUE(ifs);
        std::ostringstream ss;
        ss << ifs.rdbuf();
        EXPECT_EQ(ss.str(), "int x = 42;\n");
    }
    {
        std::ifstream ifs(subdirOutputPath, std::ios::in | std::ios::binary);
        ASSERT_TRUE(ifs) << "subdir output file not found: " << subdirOutputPath;
        std::ostringstream ss;
        ss << ifs.rdbuf();
        EXPECT_EQ(ss.str(), "int helper() { return 0; }\n");
    }

    // ── Получаем SourceMapReader (финализирует выходные файлы) ──
    const SourceMapReader* reader = ctx.source().toReader();
    ASSERT_NE(reader, nullptr);
    ASSERT_EQ(reader->output_count(), 2);

    // ── Сериализуем → десериализуем (симуляция IPC) ──
    std::vector<unsigned char> packed = reader->packToMsgpack();
    ASSERT_FALSE(packed.empty());

    auto restored = SourceMapReader::fromMsgpack(packed.data(), packed.size());
    ASSERT_NE(restored, nullptr);

    // Проверяем: после десериализации содержимое файлов пустое (только имена и хеши)
    EXPECT_TRUE(restored->source(ReaderFile::make_input(0)).empty());
    EXPECT_TRUE(restored->source(ReaderFile::make_output(0)).empty());

    // ── Читаем файлы с диска ──
    // После десериализации filename для output — это абсолютный путь (т.к. в msgpack записан он)
    // Убедимся, что readFilesFromDisk находит файлы:
    // Передаём dataDir как baseDir для резолвинга относительных путей
    bool readOk = restored->readFilesFromDisk(dataDir.generic_string());
    if (!readOk) {
        // Если не удалось — это может быть из-за input-файла (но он есть на диске).
        // Выводим диагностику
        ADD_FAILURE() << "readFilesFromDisk вернул false";
    }

    // Проверяем содержимое после чтения с диска
    // После десериализации имя выходного файла — абсолютный путь (сохранён в msgpack)
    EXPECT_FALSE(restored->filename(ReaderFile::make_input(0)).empty());
    EXPECT_EQ(restored->source(ReaderFile::make_input(0)), "create x = 42;");

    // Проверяем первый выходной файл
    EXPECT_FALSE(restored->filename(ReaderFile::make_output(0)).empty());
    EXPECT_EQ(restored->source(ReaderFile::make_output(0)), "int x = 42;\n");

    // Проверяем второй выходной файл (в подкаталоге lib/)
    ASSERT_GE(restored->output_count(), 2);
    EXPECT_FALSE(restored->filename(ReaderFile::make_output(1)).empty());
    EXPECT_EQ(restored->source(ReaderFile::make_output(1)), "int helper() { return 0; }\n");

    // Файлы остаются в dataDir для инспекции
}

TEST(SaveAndReadTest, ChecksumMismatchDetected) {
    // Тест: при несовпадении контрольной суммы verifyHash
    // возвращает false, но readFilesFromDisk при этом успешно читает данные.

    fs::path dataDir = fs::path(TEST_DATA_DIR) / "checksum_test";
    fs::create_directories(dataDir);
    std::string outputPath = (dataDir / "output.cpp").generic_string();
    std::string inputPath = (dataDir / "test.src").generic_string();

    // Создаём входной файл на диске для readFilesFromDisk
    {
        std::ofstream ofs(inputPath, std::ios::out | std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(ofs);
        ofs << "create x = 42;";
    }

    Context ctx(dataDir.generic_string());
    MapperFile inputIdx = ctx.source().load_file(inputPath);
    EXPECT_FALSE(inputIdx.isInvalid());

    // Добавляем выходной файл и пишем в него
    MapperFile outIdx = ctx.source().add_output(outputPath, true); // полный путь → нормализуется в "output.cpp"
    ctx.source().output_append(outIdx, "int x = 42;\n");
    ASSERT_TRUE(ctx.source().save_output(dataDir.generic_string()));

    // ── Сериализуем через packToMsgpack ──
    const SourceMapReader* reader = ctx.source().toReader();
    ASSERT_NE(reader, nullptr);
    std::vector<unsigned char> packed = reader->packToMsgpack();

    // ── Меняем содержимое файла на диске (чтобы сломать контрольную сумму) ──
    {
        std::ofstream ofs(outputPath, std::ios::out | std::ios::binary | std::ios::trunc);
        ofs << "int y = 99;\n";
    }

    // ── Десериализуем ──
    auto restored = SourceMapReader::fromMsgpack(packed.data(), packed.size());
    ASSERT_NE(restored, nullptr);

    // ── readFilesFromDisk читает файлы с диска, не проверяя хеши ──
    bool readOk = restored->readFilesFromDisk(dataDir.generic_string());
    EXPECT_TRUE(readOk) << "readFilesFromDisk должен вернуть true (файлы прочитаны)";

    // ── Проверяем, что содержимое прочитано ──
    EXPECT_EQ(restored->source(ReaderFile::make_output(0)), "int y = 99;\n");

    // ── verifyHash индивидуально проверяет хеш каждого файла ──
    EXPECT_FALSE(restored->verifyHash(ReaderFile::make_output(0))) << "verifyHash должен вернуть false для изменённого файла";
    EXPECT_TRUE(restored->verifyHash(ReaderFile::make_input(0))) << "verifyHash должен вернуть true для неизменённого входного файла";

    // Файлы остаются в dataDir для инспекции
}

TEST(SaveAndReadTest, DiskReadFailure) {
    // Тест: если файла нет на диске, readFilesFromDisk возвращает false.

    fs::path dataDir = fs::path(TEST_DATA_DIR) / "disk_read_fail_test";
    fs::create_directories(dataDir);
    std::string outputPath = (dataDir / "output.cpp").generic_string();
    std::string inputPath = (dataDir / "test.src").generic_string();

    // Создаём входной файл на диске
    {
        std::ofstream ofs(inputPath, std::ios::out | std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(ofs);
        ofs << "create x = 42;";
    }

    Context ctx(dataDir.generic_string());
    MapperFile inputIdx = ctx.source().load_file(inputPath);
    EXPECT_FALSE(inputIdx.isInvalid());
    MapperFile outIdx = ctx.source().add_output(outputPath, true);
    ctx.source().output_append(outIdx, "int x = 42;\n");
    ASSERT_TRUE(ctx.source().save_output(dataDir.generic_string()));

    // ── Сериализуем → десериализуем ──
    const SourceMapReader* reader = ctx.source().toReader();
    ASSERT_NE(reader, nullptr);
    std::vector<unsigned char> packed = reader->packToMsgpack();

    auto restored = SourceMapReader::fromMsgpack(packed.data(), packed.size());
    ASSERT_NE(restored, nullptr);

    // ── Удаляем выходной файл с диска ──
    ASSERT_TRUE(fs::remove(outputPath));

    // ── readFilesFromDisk должен вернуть false (файл не найден) ──
    bool readOk = restored->readFilesFromDisk(dataDir.generic_string());
    EXPECT_FALSE(readOk) << "readFilesFromDisk должен вернуть false, т.к. output.cpp удалён";

    // ── Входной файл всё ещё на диске и должен быть прочитан ──
    EXPECT_EQ(restored->source(ReaderFile::make_input(0)), "create x = 42;");

    // ── Выходной файл не прочитан — источник пуст ──
    EXPECT_TRUE(restored->source(ReaderFile::make_output(0)).empty());
}