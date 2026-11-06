#include "utils/hash.hpp"

#include "utils/utils.hpp"

#include <fstream>
#include <iomanip>
#include <iterator>
#include <sstream>

#include <llvm/Support/MD5.h>

namespace trust::utils {

std::string md5Hex(std::string_view data) {
    const auto hash = llvm::MD5Hash(data);
    std::ostringstream oss;
    oss << std::hex << std::setw(16) << std::setfill('0') << hash;
    return oss.str();
}

std::optional<std::string> md5HexFile(const std::filesystem::path& path) {
    auto content = FileIO::read<std::string>(path.string());
    if (!content) {
        return std::nullopt;
    }
    return md5Hex(*content);
}

} // namespace trust::utils
