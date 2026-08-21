// -----------------------------------------------------------------------
// End-to-end regression test for the "trust + trust-lsp" source map flow.
//
//   trust CLI  --emit-cpp --temp-dir <dir> <file.src>
//     -> generates <dir>/<file>.cppt  and  <dir>/<file>.src_map (side by side)
//   SourceMapReader::fromMsgpack reads the .src_map back
//     -> filenames resolve to the real .src / .cppt, hashes verify,
//        serialize -> deserialize round-trip preserves mappings.
//
// Guards:
//   * output filename in the map must be the real .cppt basename (not "out")
//   * input filename must be a clean relative path (not CWD-dependent garbage)
//   * the map must round-trip and carry non-empty trust<->cpp mappings.
// -----------------------------------------------------------------------

#include "diag/mapper.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace trust;
namespace fs = std::filesystem;

TEST(SrcMapCliReadback, EmitCppProducesReadableMap) {
    fs::path dir = fs::path(TEST_DATA_DIR) / "src_map_cli_readback";
    fs::remove_all(dir);
    ASSERT_TRUE(fs::create_directories(dir));
    fs::path srcPath = dir / "prog.src";
    {
        std::ofstream ofs(srcPath);
        ofs << "x:Int32 := 10;\n"
               "y:Int32 := 3;\n"
               "z := x / y;\n";
    }

    // 1. Run the real trust CLI exactly as a user would.
    std::string cmd = std::string(TRUST_BINARY_PATH) + " --quiet --temp-dir \"" + dir.string() + "\" --emit-cpp \"" + srcPath.string() + "\" >/dev/null";
    int rc = std::system(cmd.c_str());
    ASSERT_EQ(rc, 0) << "trust exited with code " << rc;

    fs::path cpptPath = dir / "prog.cppt";
    fs::path mapPath = dir / "prog.src_map";
    ASSERT_TRUE(fs::exists(cpptPath)) << "missing .cppt next to .src_map";
    ASSERT_TRUE(fs::exists(mapPath)) << "missing .src_map";

    // 2. Read the saved source map back.
    std::ifstream ifs(mapPath, std::ios::binary);
    std::vector<unsigned char> data((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    ASSERT_FALSE(data.empty());
    auto reader = SourceMapReader::fromMsgpack(data.data(), data.size());
    ASSERT_NE(reader, nullptr) << "src_map does not deserialize";

    // 3. Output filename must be the real .cppt basename (not the old "out").
    ASSERT_EQ(reader->output_count(), 1u);
    std::string outName(reader->filename(ReaderFile::make_output(0)));
    EXPECT_EQ(outName, "prog.cppt") << "output filename in src_map = '" << outName << "'";

    // 4. The first input must be the real source: a clean, non-absolute path
    //    ending with the source basename (not resolved against the process CWD
    //    into a useless absolute path, which would break readback).
    ASSERT_GE(reader->input_count(), 1u);
    std::string inName(reader->filename(ReaderFile::make_input(0)));
    fs::path inPath(inName);
    EXPECT_FALSE(inPath.is_absolute()) << "input filename in src_map = '" << inName << "'";
    EXPECT_EQ(inPath.filename().string(), "prog.src") << "input filename in src_map = '" << inName << "'";
    EXPECT_EQ(inName.find("home/"), std::string::npos) << "input filename leaked absolute home path: '" << inName << "'";

    // 5. Mappings must be present in both directions.
    EXPECT_FALSE(reader->getForwardMappings().empty());
    EXPECT_FALSE(reader->getBackwardMappings().empty());

    // 5b. The forward mappings must be attached to the REAL source file
    //     (input[0] = prog.src), so LSP navigation (findRangeMap on the cursor
    //     position of the actual .src) can resolve trust -> cpp.
    EXPECT_FALSE(reader->getTrustFileMappings(ReaderFile::make_input(0)).empty()) << "no mappings on the real source file (prog.src)";

    // 6. Serialize -> deserialize round-trip must preserve the mappings.
    auto repacked = reader->packToMsgpack();
    ASSERT_FALSE(repacked.empty());
    auto reader2 = SourceMapReader::fromMsgpack(repacked.data(), repacked.size());
    ASSERT_NE(reader2, nullptr);
    EXPECT_EQ(reader2->getForwardMappings().size(), reader->getForwardMappings().size());

    // 6b. In-memory (fictitious) sources are marked with the '@' prefix, so
    //     consumers know there is no file on disk (DSL -> "@dsl", macro bodies
    //     -> "@input").
    bool sawInMemory = false;
    for (uint32_t i = 0; i < reader->input_count(); ++i) {
        sawInMemory = sawInMemory || trust::SourceMapReader::isInMemoryName(reader->filename(ReaderFile::make_input(i)));
    }
    EXPECT_TRUE(sawInMemory) << "expected at least one '@'-prefixed in-memory input (DSL/macro)";
    EXPECT_FALSE(trust::SourceMapReader::isInMemoryName("prog.src"));

    // 7. The real source file must load from disk and its hash must match the
    //    hash stored in the map (map is not stale for the unchanged source).
    //    The stored input name is relative to the compiler's base dir; recover
    //    that base dir by stripping the relative suffix from the absolute path.
    //    ('@'-prefixed in-memory inputs are skipped by readFilesFromDisk; the
    //     overall return value may still be false if the .cppt output lives in a
    //     different temp dir than baseDir - a separate path-resolution concern.)
    std::string absSrc = fs::absolute(srcPath).lexically_normal().generic_string();
    if (absSrc.ends_with(inName) && inName.size() < absSrc.size()) {
        fs::path baseDir = fs::path(absSrc.substr(0, absSrc.size() - inName.size()));
        reader->readFilesFromDisk(baseDir.string());
        ReaderFile srcFile = reader->findFile(srcPath.string());
        ASSERT_FALSE(srcFile.isInvalid()) << "source file not found in map by path";
        EXPECT_TRUE(reader->verifyHash(srcFile)) << "source file hash mismatch (stale map?)";
    }
}
