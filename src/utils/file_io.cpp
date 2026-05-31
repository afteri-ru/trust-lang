#include "utils/file_io.hpp"

#include <cstdint>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

namespace trust::utils {

std::optional<std::vector<char>> FileIO::readImpl(const std::string& path) {
    std::ifstream ifs(path, std::ios::in | std::ios::binary);
    if (!ifs)
        return std::nullopt;

    ifs.seekg(0, std::ios::end);
    auto file_size = ifs.tellg();
    if (file_size < 0)
        return std::nullopt;
    ifs.seekg(0, std::ios::beg);

    std::vector<char> buf(static_cast<std::size_t>(file_size));
    if (!ifs.read(buf.data(), static_cast<std::streamsize>(buf.size())))
        return std::nullopt;

    return buf;
}

} // namespace trust::utils
