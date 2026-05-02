#include "trust_source.h"

using trust::TrustSource;

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <string>
#include <unistd.h>

// ============================================================================
// Helper: строит TrustSource с тестовыми данными
// ============================================================================

static std::string GetCwd() {
    return std::filesystem::current_path().string();
}

static TrustSource MakeValidSource() {
    TrustSource ts(GetCwd(), GetCwd());

    ts.setFilePair("main.src", "main.cpp");
    // main.trust: mapping [5 → 10, vars: a→a_cpp, b→b_cpp]
    ts.addLineMapping(5, 10);
    ts.addVarMapping(5, 10, "a", "a_cpp");
    ts.addVarMapping(5, 10, "b", "b_cpp");

    // main.trust: [7 → 14, vars: c→c_cpp]
    ts.addLineMapping(7, 14);
    ts.addVarMapping(7, 14, "c", "c_cpp");

    // main.trust: [9 → 18] — без vars
    ts.addLineMapping(9, 18);

    ts.setFilePair("utils.src", "utils.cpp");
    // utils.trust: [3 → 5, vars: x→x]
    ts.addLineMapping(3, 5);
    ts.addVarMapping(3, 5, "x", "x");

    // utils.trust: [8 → 12] — без vars
    ts.addLineMapping(8, 12);

    return ts;
}

static TrustSource MakeTwoMappingsSource() {
    TrustSource ts(GetCwd(), GetCwd());

    ts.setFilePair("t.src", "t.cpp");
    ts.addLineMapping(1, 2);
    ts.addVarMapping(1, 2, "v", "v");

    ts.addLineMapping(3, 6);
    ts.addVarMapping(3, 6, "w", "w");

    return ts;
}

static TrustSource MakeSourceWithInsertedLines() {
    TrustSource ts(GetCwd(), GetCwd());
    ts.setFilePair("main.src", "main.cpp");
    // Mapping pre-preamble (как делает transpiler.h):
    //   trust 2 → cpp 1, trust 3 → cpp 2, trust 4 → cpp 3
    ts.addLineMapping(2, 1);
    ts.addVarMapping(2, 1, "x", "cpp_x");
    ts.addLineMapping(3, 2);
    ts.addVarMapping(3, 2, "y", "cpp_y");
    ts.addLineMapping(4, 3);
    // Затем добавлено 3 строки преамбулы
    ts.setCppLineInserted(3);
    return ts;
}

// Упаковывает TrustSource в msgpack (для LoadFromBinary через inline-данные)
static std::vector<unsigned char> PackSource(const TrustSource &ts) {
    return TrustSource::pack(ts);
}

// Записывает данные в файл
static bool WriteFile(const std::string &path, const std::vector<unsigned char> &data) {
    std::ofstream out(path, std::ios::binary);
    if (!out.is_open()) return false;
    out.write(reinterpret_cast<const char *>(data.data()), data.size());
    return out.good();
}

// ============================================================================
// Helper: LoadFromSource — возвращает unique_ptr<const TrustSource>
// ============================================================================

static auto LoadFromSource(const TrustSource &src) -> std::unique_ptr<const TrustSource> {
    auto data = PackSource(src);
    return TrustSource::unpack(data.data(), data.size());
}

// ============================================================================
// 1. unpack (TrustSource::unpack)
// ============================================================================

TEST(TrustSourceTest, LoadValidMap) {
    auto src = LoadFromSource(MakeValidSource());
    ASSERT_NE(src, nullptr);
    EXPECT_EQ(src->entries().size(), 2);
}

TEST(TrustSourceTest, LoadEmptySources) {
    // Создаём пустой TrustSource и пакуем его
    TrustSource empty(GetCwd(), GetCwd());
    auto data = PackSource(empty);
    auto src = TrustSource::unpack(data.data(), data.size());
    ASSERT_NE(src, nullptr);
    EXPECT_EQ(src->entries().size(), 0);
}

TEST(TrustSourceTest, LoadCorruptData) {
    std::vector<unsigned char> bad = {0x01, 0x02, 0x03, 0xFF};
    std::string err;
    auto result = TrustSource::unpack(bad.data(), bad.size(), &err);
    EXPECT_EQ(result, nullptr);
}

// ============================================================================
// 2. nearestTrustToCpp
// ============================================================================

TEST(TrustSourceTest, NearestTrustToCpp_ExactMatch) {
    auto src = LoadFromSource(MakeValidSource());
    ASSERT_NE(src, nullptr);
    auto result = src->nearestTrustToCpp("main.src", 5);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->first, "main.cpp");
    EXPECT_EQ(result->second, 10);
}

TEST(TrustSourceTest, NearestTrustToCpp_NoMatch) {
    auto src = LoadFromSource(MakeValidSource());
    ASSERT_NE(src, nullptr);
    auto result = src->nearestTrustToCpp("nonexistent.src", 99);
    EXPECT_FALSE(result.has_value());
}

TEST(TrustSourceTest, NearestTrustToCpp_WrongLine) {
    auto src = LoadFromSource(MakeValidSource());
    ASSERT_NE(src, nullptr);
    auto result = src->nearestTrustToCpp("main.src", 6);
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result->first, "main.cpp");
    EXPECT_EQ(result->second, 10); // nearest ≤ 6 is 5 → 10
}

// ============================================================================
// 3. nearestCppToTrust
// ============================================================================

TEST(TrustSourceTest, NearestCppToTrust_ExactMatch) {
    auto src = LoadFromSource(MakeValidSource());
    ASSERT_NE(src, nullptr);
    auto result = src->nearestCppToTrust("main.cpp", 10);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->first, "main.src");
    EXPECT_EQ(result->second, 5);
}

TEST(TrustSourceTest, NearestCppToTrust_NearestLine) {
    auto src = LoadFromSource(MakeTwoMappingsSource());
    ASSERT_NE(src, nullptr);
    auto result = src->nearestCppToTrust("t.cpp", 4);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->first, "t.src");
    EXPECT_EQ(result->second, 1);
}

TEST(TrustSourceTest, NearestCppToTrust_NoMatch) {
    auto src = LoadFromSource(MakeValidSource());
    ASSERT_NE(src, nullptr);
    auto result = src->nearestCppToTrust("unknown.cpp", 10);
    EXPECT_FALSE(result.has_value());
}

// ============================================================================
// 4. getCppVar / getTrustVar
// ============================================================================

TEST(TrustSourceTest, GetCppVar_FullMatch) {
    auto src = LoadFromSource(MakeValidSource());
    ASSERT_NE(src, nullptr);
    // trust_file="main.src", trust_line=5 → a (trust) → a_cpp (cpp)
    auto var = src->getCppVar("main.src", 5, "a");
    ASSERT_TRUE(var.has_value());
    EXPECT_EQ(var->vars.second, "a_cpp");
    EXPECT_EQ(var->vars.first, "a");
    EXPECT_EQ(var->lines.first, 5);
    EXPECT_EQ(var->lines.second, 10);
    // trust_file="main.src", trust_line=5 → b (trust) → b_cpp (cpp)
    var = src->getCppVar("main.src", 5, "b");
    ASSERT_TRUE(var.has_value());
    EXPECT_EQ(var->vars.second, "b_cpp");
    EXPECT_EQ(var->vars.first, "b");
    EXPECT_EQ(var->lines.first, 5);
    EXPECT_EQ(var->lines.second, 10);
}

TEST(TrustSourceTest, GetCppVar_NoMatch_ReturnsNullopt) {
    auto src = LoadFromSource(MakeValidSource());
    ASSERT_NE(src, nullptr);
    // z не существует в vars, должен вернуть nullopt
    auto var = src->getCppVar("main.src", 5, "z");
    EXPECT_FALSE(var.has_value());
}

TEST(TrustSourceTest, GetCppVar_WrongLine_ReturnsNullopt) {
    auto src = LoadFromSource(MakeValidSource());
    ASSERT_NE(src, nullptr);
    // trust_line=99 не существует → nullopt
    auto var = src->getCppVar("main.src", 99, "a");
    EXPECT_FALSE(var.has_value());
}

TEST(TrustSourceTest, GetTrustVar_FullMatch) {
    auto src = LoadFromSource(MakeValidSource());
    ASSERT_NE(src, nullptr);
    // a_cpp (cpp) → a (trust)
    auto trustVar = src->getTrustVar("main.cpp", 10, "a_cpp");
    ASSERT_TRUE(trustVar.has_value());
    EXPECT_EQ(trustVar->vars.first, "a");
    EXPECT_EQ(trustVar->vars.second, "a_cpp");
    // b_cpp (cpp) → b (trust)
    trustVar = src->getTrustVar("main.cpp", 10, "b_cpp");
    ASSERT_TRUE(trustVar.has_value());
    EXPECT_EQ(trustVar->vars.first, "b");
    EXPECT_EQ(trustVar->vars.second, "b_cpp");
}

TEST(TrustSourceTest, GetTrustVar_NoMatch_ReturnsNullopt) {
    auto src = LoadFromSource(MakeValidSource());
    ASSERT_NE(src, nullptr);
    auto trustVar = src->getTrustVar("main.cpp", 10, "unknown_cpp");
    EXPECT_FALSE(trustVar.has_value());
}

TEST(TrustSourceTest, GetTrustVar_LineWithNoVars) {
    auto src = LoadFromSource(MakeValidSource());
    ASSERT_NE(src, nullptr);
    // line 18 — нет vars, "a" не должна транслироваться
    auto trustVar = src->getTrustVar("main.cpp", 18, "a");
    EXPECT_FALSE(trustVar.has_value());
}

TEST(TrustSourceTest, GetTrustVar_NearestLineFallback) {
    auto src = LoadFromSource(MakeTwoMappingsSource());
    ASSERT_NE(src, nullptr);
    // cpp_line=4 → nearest (≤) = cpp_line=2, trust_line=1, vars={"v":"v"}
    auto trustVar = src->getTrustVar("t.cpp", 4, "v");
    ASSERT_TRUE(trustVar.has_value());
    EXPECT_EQ(trustVar->vars.first, "v");
    EXPECT_EQ(trustVar->lines.first, 1);
}

// ============================================================================
// 4b. getCppVar / getTrustVar with non-zero cpp_line_inserted
// ============================================================================

TEST(TrustSourceTest, GetCppVar_WithInsertedLines) {
    auto src = LoadFromSource(MakeSourceWithInsertedLines());
    ASSERT_NE(src, nullptr);
    // trust-строки без offset — должны находиться
    auto var = src->getCppVar("main.src", 2, "x");
    ASSERT_TRUE(var.has_value());
    EXPECT_EQ(var->vars.second, "cpp_x");
    EXPECT_EQ(var->vars.first, "x");

    var = src->getCppVar("main.src", 3, "y");
    ASSERT_TRUE(var.has_value());
    EXPECT_EQ(var->vars.second, "cpp_y");
}

TEST(TrustSourceTest, GetTrustVar_WithInsertedLines_ExactMatch) {
    auto src = LoadFromSource(MakeSourceWithInsertedLines());
    ASSERT_NE(src, nullptr);
    // LLDB сообщает C++ строки ПОСЛЕ вставки преамбулы (строка 4 = cpp_x = x)
    // Сейчас getTrustVar ищет lines.second == cppLine (1==4) → не находит → BUG
    auto var = src->getTrustVar("main.cpp", 4, "cpp_x");
    ASSERT_TRUE(var.has_value()) << "Should find cpp_x at C++ line 4 (after preamble)";
    EXPECT_EQ(var->vars.first, "x");
    EXPECT_EQ(var->vars.second, "cpp_x");
}

TEST(TrustSourceTest, GetTrustVar_WithInsertedLines_NearestFallback) {
    auto src = LoadFromSource(MakeSourceWithInsertedLines());
    ASSERT_NE(src, nullptr);
    // C++ line 5 — нет var на этой строке, должен найти nearest (≤)
    // nearest: C++ line 2 (pre-preamble) → 5 (post-preamble) → trust 3 → var y
    auto var = src->getTrustVar("main.cpp", 5, "cpp_y");
    ASSERT_TRUE(var.has_value()) << "Should find cpp_y via nearest fallback at C++ line 5";
    EXPECT_EQ(var->vars.first, "y");
}

// ============================================================================
// 5. LoadFromBinary — из .map файла (auto-fallback на файл)
// ============================================================================

static std::string TempPath(const char *name) {
    return (std::filesystem::temp_directory_path() / name).string();
}

TEST(TrustSourceTest, LoadFromBinary_MapFile) {
    auto src = MakeValidSource();
    auto packed = PackSource(src);
    std::string mapPath = TempPath("_test_mapfile.map");
    ASSERT_TRUE(WriteFile(mapPath, packed));

    auto loaded = TrustSource::LoadFromBinary("", mapPath);
    ASSERT_NE(loaded, nullptr);
    EXPECT_EQ(loaded->entries().size(), 2);

    // Проверяем, что работает трансляция
    auto result = loaded->nearestTrustToCpp("main.src", 5);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->first, "main.cpp");
    EXPECT_EQ(result->second, 10);

    std::remove(mapPath.c_str());
}

TEST(TrustSourceTest, LoadFromBinary_MapFileWithDirectories) {
    auto src = MakeValidSource();
    auto packed = PackSource(src);
    std::string mapPath = TempPath("_test_mapfile_dir.map");
    ASSERT_TRUE(WriteFile(mapPath, packed));

    auto loaded = TrustSource::LoadFromBinary("", mapPath);
    ASSERT_NE(loaded, nullptr);
    EXPECT_EQ(loaded->entries().size(), 2);

    std::remove(mapPath.c_str());
}

TEST(TrustSourceTest, LoadFromBinary_NonexistentMap) {
    auto loaded = TrustSource::LoadFromBinary("/", "/nonexistent.map");
    EXPECT_EQ(loaded, nullptr);
}

// ============================================================================
// 6. LoadFromBinary из не-ELF бинарника (должен вернуть nullptr)
// ============================================================================

TEST(TrustSourceTest, LoadFromBinary_NonElfBinary) {
    std::string path = TempPath("_test_notelf.bin");
    {
        std::ofstream out(path, std::ios::binary);
        ASSERT_TRUE(out.is_open());
        out << "not an ELF file";
    }
    auto loaded = TrustSource::LoadFromBinary(path);
    EXPECT_EQ(loaded, nullptr);
    std::remove(path.c_str());
}

TEST(TrustSourceTest, LoadFromBinary_NoSuchFile) {
    auto loaded = TrustSource::LoadFromBinary("/nonexistent_file.elf");
    EXPECT_EQ(loaded, nullptr);
}

// ============================================================================
// 7. roundtrip: pack → unpack — проверка идентичности
// ============================================================================

TEST(TrustSourceTest, RoundtripPackUnpack) {
    auto src = MakeValidSource();
    auto packed = TrustSource::pack(src);
    ASSERT_FALSE(packed.empty());

    auto restored = TrustSource::unpack(packed.data(), packed.size());
    ASSERT_NE(restored, nullptr);

    // Проверяем, что упакованные и распакованные данные идентичны
    auto repacked = TrustSource::pack(*restored);
    EXPECT_EQ(packed, repacked);
}

// ============================================================================
// 8. Нормализация через pack/unpack — проверяем roundtrip относительных путей
// ============================================================================

TEST(TrustSourceTest, NormalizePathsRoundtrip) {
    TrustSource ts(GetCwd(), GetCwd());
    ts.setFilePair(".trust/subdir/main.src", "subdir/main.cpp");
    ts.addLineMapping(1, 1);

    auto packed = PackSource(ts);
    auto restored = TrustSource::unpack(packed.data(), packed.size());
    ASSERT_NE(restored, nullptr);

    auto result = restored->nearestTrustToCpp(".trust/subdir/main.src", 1);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->first, "subdir/main.cpp");
}

// ============================================================================
// 9. Нормализация cpp-пути по умолчанию (без явного cppPath)
// ============================================================================

TEST(TrustSourceTest, NormalizeDefaultCppDir) {
    TrustSource ts(GetCwd());

    // Полный абсолютный путь — нормализуется (срезается префикс cpp_directory_)
    {
        std::string fullCppPath = GetCwd() + "/.trust/main.cpp";
        ts.setFilePair("main.src", fullCppPath);
        ts.addLineMapping(1, 1);

        auto packed = PackSource(ts);
        auto restored = TrustSource::unpack(packed.data(), packed.size());
        ASSERT_NE(restored, nullptr);

        auto result = restored->nearestTrustToCpp("main.src", 1);
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(result->first, "main.cpp");
    }

    // Относительный путь — остаётся как есть (не нормализуется)
    {
        ts.setFilePair("utils.src", ".trust/utils.cpp");
        ts.addLineMapping(2, 2);

        auto packed = PackSource(ts);
        auto restored = TrustSource::unpack(packed.data(), packed.size());
        ASSERT_NE(restored, nullptr);

        auto result = restored->nearestTrustToCpp("utils.src", 2);
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(result->first, ".trust/utils.cpp");
    }
}
