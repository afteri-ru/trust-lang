#ifndef TRUST_TEST_DATA_HPP
#define TRUST_TEST_DATA_HPP
// Общий хелпер для юнит-тестов, создающих временные файлы.
//
// Каждый тест обязан работать в СВОЁМ фиксированном подкаталоге внутри _build
// (TEST_DATA_DIR == ${CMAKE_BINARY_DIR}/test_data) и НЕ создавать новые
// временные каталоги/файлы с уникальным именем при каждом запуске
// (mkdtemp/mkstemps/getpid()/счётчики/...).
//
// Каталог фиксированный и переиспользуется между запусками, поэтому в начале
// каждого теста его нужно очищать (remove_all + create_directories). Файлы после
// теста НЕ удаляются - остаются в _build для анализа (см. test/MEMORY.md).

#include <filesystem>
#include <string_view>

// TEST_DATA_DIR определён в CMakeLists.txt как "${CMAKE_BINARY_DIR}/test_data".
#ifndef TEST_DATA_DIR
#error "TEST_DATA_DIR is not defined; include via unit_tests compile definitions"
#endif

namespace trust::test {

// Возвращает фиксированный путь TEST_DATA_DIR/<name> и готовит его к работе:
// удаляет содержимое от предыдущего прогона и создаёт каталог заново.
inline std::filesystem::path makeTestDataDir(std::string_view name) {
    auto dir = std::filesystem::path(TEST_DATA_DIR) / name;
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    return dir;
}

} // namespace trust::test

#endif // TRUST_TEST_DATA_HPP
