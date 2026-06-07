#include "utils/io.hpp"
/**
 * gen_test_map.cpp — Generates binary .map files for LSP integration tests.
 *
 * Build & run:
 *   cmake --build _build --target gen_test_map
 *   ./gen_test_map <trust_file> <cpp_file> <output_map>
 */

#include "diag/context.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

int main(int argc, char* argv[]) {
    if (argc != 4) {
        trust::errs() << "Usage: " << argv[0] << " <trust_file> <cpp_file> <output_map>\n";
        return 1;
    }

    std::string trustFile = argv[1];
    std::string cppFile = argv[2];
    std::string mapPath = argv[3];

    // Создаём Context и заполняем маппинги
    trust::Context ctx;

    // Читаем trust-файл, чтобы получить source content для loc_from_line
    std::ifstream trustIn(trustFile);
    std::string trustContent((std::istreambuf_iterator<char>(trustIn)), std::istreambuf_iterator<char>());
    trustIn.close();

    auto trustIdx = ctx.source().add_source(trustFile, trustContent, true);
    auto cppIdx = ctx.source().add_output(cppFile, true);

    // Маппинг trust lines → cpp lines
    // trust:  fn main() -> int              → cpp:  #include <iostream>
    // trust:    let x: int = 42             → cpp:  int main() {
    // trust:    let y: int = x + 1          → cpp:      int x = 42;
    // trust:    return y                    → cpp:      int y = x + 1;
    auto addMapping = [&](int trustLine, int cppLine) {
        auto tLoc = ctx.source().loc_from_line(trustIdx, trustLine);
        auto tLocEnd = ctx.source().loc_from_line(trustIdx, trustLine + 1);
        auto tRange = ctx.source().makeRange(tLoc, tLocEnd);

        auto cLoc = ctx.source().makeLoc(cppIdx, cppLine);
        auto cLocEnd = ctx.source().makeLoc(cppIdx, cppLine + 1);
        auto cRange = ctx.source().makeRange(cLoc, cLocEnd);

        ctx.source().addRangeMapping(tRange, cRange);
    };

    addMapping(1, 2); // trust:1 → cpp:2
    addMapping(2, 3); // trust:2 → cpp:3
    addMapping(3, 4); // trust:3 → cpp:4
    addMapping(4, 5); // trust:4 → cpp:5

    // Pack в бинарный msgpack
    auto data = ctx.packMapping();
    if (data.empty()) {
        trust::errs() << "Error: packMapping returned empty data\n";
        return 1;
    }

    // Запись в файл
    std::ofstream out(mapPath, std::ios::binary);
    if (!out.is_open()) {
        trust::errs() << "Error: Cannot write " << mapPath << "\n";
        return 1;
    }
    out.write(reinterpret_cast<const char*>(data.data()), data.size());
    out.close();

    trust::outs() << "Generated " << mapPath << " (" << data.size() << " bytes)\n";
    return 0;
}