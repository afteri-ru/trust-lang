/**
 * gen_test_map.cpp — Generates binary .map files for LSP integration tests.
 *
 * Build & run:
 *   cmake --build build --target gen_test_map
 *   ./gen_test_map <trust_file> <cpp_file> <output_map>
 */

#include "debug/trust_source.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

int main(int argc, char *argv[]) {
    if (argc != 4) {
        std::cerr << "Usage: " << argv[0] << " <trust_file> <cpp_file> <output_map>\n";
        return 1;
    }

    std::string trustFile = argv[1];
    std::string cppFile   = argv[2];
    std::string mapPath   = argv[3];

    // Создаём TrustSource и заполняем маппинги
    trust::TrustSource ts(std::filesystem::current_path().string());
    ts.setFilePair(trustFile, cppFile);

    // Маппинг trust lines → cpp lines
    // trust:  fn main() -> int              → cpp:  #include <iostream>
    // trust:    let x: int = 42             → cpp:  int main() {
    // trust:    let y: int = x + 1          → cpp:      int x = 42;
    // trust:    return y                    → cpp:      int y = x + 1;
    ts.addLineMapping(1, 2);  // trust:1 → cpp:2
    ts.addLineMapping(2, 3);  // trust:2 → cpp:3
    ts.addLineMapping(3, 4);  // trust:3 → cpp:4
    ts.addLineMapping(4, 5);  // trust:4 → cpp:5

    // Pack в бинарный msgpack
