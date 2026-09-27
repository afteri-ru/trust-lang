// src/packages/source_tree.cpp
#include "packages/source_tree.hpp"

#include "packages/archive.hpp"
#include "packages/errors.hpp"

#include "utils/file_io.hpp"

#include <algorithm>
#include <system_error>

namespace trust::packages {
namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// DirectoryTree
// ---------------------------------------------------------------------------

DirectoryTree::DirectoryTree(fs::path root)
: m_root(std::move(root)) {
}

fs::path DirectoryTree::resolve(std::string_view relative) const {
    const fs::path rel{std::string(relative)};
    if (rel.is_absolute()) {
        fail("packages.tree.unsafe", "absolute path is not allowed: '{}'", std::string(relative));
    }
    for (const auto& part : rel) {
        if (part == "..") {
            fail("packages.tree.unsafe", "path escapes the tree: '{}'", std::string(relative));
        }
    }
    return m_root / rel;
}

bool DirectoryTree::isRegularFile(std::string_view relative) const {
    std::error_code ec;
    return fs::is_regular_file(resolve(relative), ec);
}

bool DirectoryTree::isDirectory(std::string_view relative) const {
    std::error_code ec;
    return fs::is_directory(resolve(relative), ec);
}

std::string DirectoryTree::read(std::string_view relative) const {
    const fs::path path = resolve(relative);
    auto data = trust::utils::FileIO::read<std::string>(path.string());
    if (!data) {
        fail("packages.read", "cannot read file: '{}'", path.string());
    }
    return std::move(*data);
}

std::vector<std::string> DirectoryTree::listRecursive() const {
    std::error_code ec;
    std::vector<std::string> out;
    for (fs::recursive_directory_iterator it(m_root, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code fec;
        if (it->is_regular_file(fec)) {
            out.push_back(it->path().lexically_relative(m_root).generic_string());
        }
    }
    if (ec) {
        fail("packages.tree.read", "cannot list tree '{}': {}", m_root.string(), ec.message());
    }
    std::sort(out.begin(), out.end());
    return out;
}

// ---------------------------------------------------------------------------
// ArchiveTree
// ---------------------------------------------------------------------------

ArchiveTree::ArchiveTree(fs::path archive)
: m_archive(std::move(archive)) {
    m_entries = listEntries(m_archive);
    std::sort(m_entries.begin(), m_entries.end());
}

bool ArchiveTree::isRegularFile(std::string_view relative) const {
    return std::binary_search(m_entries.begin(), m_entries.end(), std::string(relative));
}

bool ArchiveTree::isDirectory(std::string_view relative) const {
    if (relative.empty()) {
        return true; // archive root
    }
    std::string prefix(relative);
    prefix.push_back('/');
    for (const std::string& entry : m_entries) {
        if (entry.compare(0, prefix.size(), prefix) == 0) {
            return true;
        }
    }
    return false;
}

std::string ArchiveTree::read(std::string_view relative) const {
    return readArchiveEntry(m_archive, relative);
}

std::vector<std::string> ArchiveTree::listRecursive() const {
    return m_entries;
}

} // namespace trust::packages
