#ifndef TRANSPILE_LSP_TEST_UTIL_HPP
#define TRANSPILE_LSP_TEST_UTIL_HPP
// Shared helpers for transpiler pipeline + source map tests
// (transpile_lsp_test.cpp / transpile_lsp_mapping_test.cpp).
#include "diag/context.hpp"
#include "types/registry.hpp"
#include "ast/term_to_ast.hpp"
#include "semantic/pass_runner.hpp"
#include "syntax/parser.h"
#include "syntax/term.h"
#include "transpiler/transpiler.hpp"
#include "pipeline/pipeline.hpp"
#include "diag/mapper.hpp"
#include "syntax/macro.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

using namespace trust;

// Helper: run full pipeline and return source map reader
// Note: Parser::ParseText registers the text under the given source name
// (see Parser::ParseText: m_ctx.source().add_source(sourceName, text)).
inline void runPipeline(Context& ctx, std::string_view trustCode, MapperFile* outCppIdx = nullptr) {
    // 1. Parse with legacy parser (registers "input" source file)
    trust::Parser parser(ctx);
    trust::TermPtr term = parser.ParseText(trustCode);
    ASSERT_NE(term, nullptr);

    // 2. Convert Term tree to AST nodes
    auto ast_nodes = TermToAstConverter::termToAst(term, ctx);
    ASSERT_FALSE(ast_nodes.empty());

    // 3. Semantic analysis
    SemanticPassRunner runner(ctx);
    runner.run(ast_nodes);

    // 4. Generate C++
    MapperFile cppIdx = ctx.source().add_output("test.cpp", true);
    ASSERT_FALSE(cppIdx.isInvalid());
    CppTranspiler transpiler(ctx);
    transpiler.generateToFile(ast_nodes, cppIdx);

    if (outCppIdx) {
        *outCppIdx = cppIdx;
    }
}

// -- Хелперы для проверки записи .cppt/.src_map на диск --

// Сохраняет транспилированный C++ + source map в _build/test_data/<testName>/
// (путь по имени юнит-теста, временные имена не используются).
// Проверяет создание .cppt и .src_map на диске и что .src_map читается
// через SourceMapReader::fromMsgpack и содержит range-маппинги.
inline void saveAndCheckDisk(Context& ctx, MapperFile cppIdx, const std::string& testName) {
    namespace fs = std::filesystem;
    fs::path dir = fs::path(TEST_DATA_DIR) / testName;
    fs::remove_all(dir);
    ASSERT_TRUE(fs::create_directories(dir));

    fs::path cpptPath = dir / (testName + ".cppt");
    ASSERT_TRUE(trust::saveCppAndEmbedSourceMap(ctx, cppIdx, cpptPath, /*verbose=*/false));

    ASSERT_TRUE(fs::exists(cpptPath)) << "missing .cppt: " << cpptPath;
    fs::path mapPath = cpptPath;
    mapPath.replace_extension(".src_map");
    ASSERT_TRUE(fs::exists(mapPath)) << "missing .src_map: " << mapPath;

    // .cppt содержит сгенерированный код и #embed-секцию source map
    std::ifstream cppIfs(cpptPath);
    std::string cpp((std::istreambuf_iterator<char>(cppIfs)), std::istreambuf_iterator<char>());
    EXPECT_FALSE(cpp.empty());
    EXPECT_NE(cpp.find("#embed"), std::string::npos) << cpp;
    EXPECT_NE(cpp.find("__debug_trust_source_map"), std::string::npos) << cpp;

    // .src_map валиден и содержит range-маппинги (mapStart/mapStop).
    // Макро-раскрытие может регистрировать несколько одноимённых источников
    // "input", поэтому считаем маппинги по всем входным файлам.
    std::ifstream mapIfs(mapPath, std::ios::binary);
    std::vector<unsigned char> data((std::istreambuf_iterator<char>(mapIfs)), std::istreambuf_iterator<char>());
    ASSERT_FALSE(data.empty());
    auto reader = SourceMapReader::fromMsgpack(data.data(), data.size());
    ASSERT_NE(reader, nullptr);
    size_t mappingCount = 0;
    for (uint32_t i = 0; i < reader->input_count(); ++i) {
        mappingCount += reader->getTrustFileMappings(ReaderFile::make_input(i)).size();
    }
    EXPECT_GE(mappingCount, 1u);
}

// 0-based индекс в строке → 1-based offset (используется makeLoc/getText)
inline uint32_t off1(size_t idx0) {
    return static_cast<uint32_t>(idx0 + 1);
}

// Ищет input-файл с заданным содержимым. Parser::ParseText при раскрытии макросов
// регистрирует множество одноимённых источников "input", поэтому пользовательский
// код находится по содержимому, а не по индексу.
inline bool findInputBySource(const SourceMapReader* reader, std::string_view src, uint32_t& outIdx) {
    for (uint32_t i = 0; i < reader->input_count(); ++i) {
        if (reader->source(ReaderFile::make_input(i)) == src) {
            outIdx = i;
            return true;
        }
    }
    return false;
}

#endif // TRANSPILE_LSP_TEST_UTIL_HPP
