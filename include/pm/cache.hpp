#pragma once

// include/pm/cache.hpp
// Package cache: content-addressed store (sha256) plus an advisory file lock
// guarding concurrent runs. Integrity is external (sidecar/index), never inside
// the manifest.

#include <filesystem>
#include <string>

namespace trust::pm {

/// Advisory exclusive lock (flock) held for the object lifetime.
class FileLock {
  public:
    explicit FileLock(const std::filesystem::path& path);
    ~FileLock();

    FileLock(const FileLock&) = delete;
    FileLock& operator=(const FileLock&) = delete;
    FileLock(FileLock&&) = delete;
    FileLock& operator=(FileLock&&) = delete;

  private:
    int m_fd = -1;
};

/// Content-addressed package cache rooted at a directory.
class Cache {
  public:
    explicit Cache(std::filesystem::path root);

    [[nodiscard]] const std::filesystem::path& root() const;

    /// Path of the artifact stored under `sha256` (may not exist yet).
    [[nodiscard]] std::filesystem::path artifactPath(const std::string& sha256) const;

    /// True if an artifact with this hash is present.
    [[nodiscard]] bool has(const std::string& sha256) const;

    /// Atomically stores `source` under `sha256` (creates the root), returns the path.
    std::filesystem::path store(const std::string& sha256, const std::filesystem::path& source);

    /// Acquires an exclusive lock for a mutating operation.
    [[nodiscard]] FileLock lock() const;

  private:
    std::filesystem::path m_root;
};

} // namespace trust::pm
