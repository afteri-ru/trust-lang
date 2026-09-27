#pragma once

// include/packages/source_tree.hpp
// Storage abstraction over a package tree: a directory or a `.trust` archive.
// Gives a uniform relative-path view so placement/search do not depend on storage.

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace trust::packages {

/// A read-only tree of source entries addressed by '/'-separated relative paths.
class SourceTree {
  public:
    virtual ~SourceTree() = default;
    SourceTree(const SourceTree&) = delete;
    SourceTree& operator=(const SourceTree&) = delete;

    /// True if a regular file exists at `relative`.
    [[nodiscard]] virtual bool isRegularFile(std::string_view relative) const = 0;
    /// True if a directory exists at `relative` ("" = tree root).
    [[nodiscard]] virtual bool isDirectory(std::string_view relative) const = 0;
    /// Reads a regular file. Throws PackageException("packages.read") if missing.
    [[nodiscard]] virtual std::string read(std::string_view relative) const = 0;
    /// Relative paths of ALL regular files (sorted, '/'-separated).
    [[nodiscard]] virtual std::vector<std::string> listRecursive() const = 0;

  protected:
    SourceTree() = default;
};

/// A tree backed by a filesystem directory.
class DirectoryTree : public SourceTree {
  public:
    explicit DirectoryTree(std::filesystem::path root);

    [[nodiscard]] bool isRegularFile(std::string_view relative) const override;
    [[nodiscard]] bool isDirectory(std::string_view relative) const override;
    [[nodiscard]] std::string read(std::string_view relative) const override;
    [[nodiscard]] std::vector<std::string> listRecursive() const override;

  private:
    [[nodiscard]] std::filesystem::path resolve(std::string_view relative) const;

    std::filesystem::path m_root;
};

/// A tree backed by a `.trust` archive (tar+zstd), read through the archiver.
class ArchiveTree : public SourceTree {
  public:
    explicit ArchiveTree(std::filesystem::path archive);

    [[nodiscard]] bool isRegularFile(std::string_view relative) const override;
    [[nodiscard]] bool isDirectory(std::string_view relative) const override;
    [[nodiscard]] std::string read(std::string_view relative) const override;
    [[nodiscard]] std::vector<std::string> listRecursive() const override;

  private:
    std::filesystem::path m_archive;
    std::vector<std::string> m_entries; ///< sorted, '/'-separated regular-file entries
};

} // namespace trust::packages
