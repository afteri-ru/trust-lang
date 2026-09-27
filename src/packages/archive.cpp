// src/packages/archive.cpp
#include "packages/archive.hpp"

#include "packages/errors.hpp"

#include "utils/hash.hpp"

#include <archive.h>
#include <archive_entry.h>

#include <algorithm>
#include <array>
#include <fstream>
#include <system_error>
#include <vector>

namespace trust::packages {
namespace {

namespace fs = std::filesystem;

std::string archiveError(struct archive* a, const char* action) {
    const char* message = archive_error_string(a);
    return std::string(action) + ": " + (message != nullptr ? message : "unknown libarchive error");
}

struct ArchiveWriter {
    struct archive* handle = nullptr;
    ~ArchiveWriter() {
        if (handle != nullptr) {
            archive_write_free(handle);
        }
    }
};

struct ArchiveReader {
    struct archive* handle = nullptr;
    ~ArchiveReader() {
        if (handle != nullptr) {
            archive_read_free(handle);
        }
    }
};

struct EntryGuard {
    struct archive_entry* entry = nullptr;
    ~EntryGuard() {
        if (entry != nullptr) {
            archive_entry_free(entry);
        }
    }
};

void writeFileData(struct archive* a, const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        fail("packages.archive.read", "cannot read file: '{}'", file.string());
    }
    std::array<char, 65536> buffer{};
    while (in) {
        in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize got = in.gcount();
        if (got > 0) {
            const la_ssize_t written = archive_write_data(a, buffer.data(), static_cast<std::size_t>(got));
            if (written < 0) {
                fail("packages.archive.pack", archiveError(a, "archive_write_data"));
            }
        }
    }
}

std::string readAll(struct archive* a) {
    std::string data;
    std::array<char, 65536> buffer{};
    for (;;) {
        const la_ssize_t got = archive_read_data(a, buffer.data(), buffer.size());
        if (got < 0) {
            fail("packages.archive.read", archiveError(a, "archive_read_data"));
        }
        if (got == 0) {
            break;
        }
        data.append(buffer.data(), static_cast<std::size_t>(got));
    }
    return data;
}

// Rejects entry names escaping the extraction root.
void checkSafeEntry(const std::string& path) {
    if (path.empty() || path[0] == '/' || path.find('\\') != std::string::npos) {
        fail("packages.archive.unsafe", "unsafe entry path: '{}'", path);
    }
    std::size_t pos = 0;
    while (pos <= path.size()) {
        const std::size_t slash = path.find('/', pos);
        const std::string_view part = (slash == std::string::npos) ? std::string_view(path).substr(pos) : std::string_view(path).substr(pos, slash - pos);
        if (part == "..") {
            fail("packages.archive.unsafe", "unsafe entry path: '{}'", path);
        }
        if (slash == std::string::npos) {
            break;
        }
        pos = slash + 1;
    }
}

} // namespace

std::string packArchive(const fs::path& root, const fs::path& out) {
    std::error_code ec;
    if (!fs::is_directory(root, ec)) {
        fail("packages.archive.root", "package root is not a directory: '{}'", root.string());
    }

    std::error_code iter_ec;
    std::vector<fs::path> files;
    for (fs::recursive_directory_iterator it(root, iter_ec), end; !iter_ec && it != end; it.increment(iter_ec)) {
        std::error_code fec;
        if (it->is_regular_file(fec)) {
            files.push_back(it->path());
        }
    }
    if (iter_ec) {
        fail("packages.archive.read", "cannot list package tree '{}': {}", root.string(), iter_ec.message());
    }
    std::error_code canon_ec;
    const fs::path out_abs = fs::weakly_canonical(out, canon_ec);
    if (canon_ec) {
        fail("packages.archive.pack", "cannot resolve output path '{}': {}", out.string(), canon_ec.message());
    }
    // Sort by relative path for a deterministic archive.
    std::sort(files.begin(), files.end(), [&root](const fs::path& a, const fs::path& b) {
        return a.lexically_relative(root).generic_string() < b.lexically_relative(root).generic_string();
    });

    ArchiveWriter writer;
    writer.handle = archive_write_new();
    if (writer.handle == nullptr) {
        fail("packages.archive.pack", "cannot create libarchive writer");
    }
    archive_write_set_format_pax_restricted(writer.handle);
    archive_write_add_filter_zstd(writer.handle);
    archive_write_set_filter_option(writer.handle, "zstd", "compression-level", "19");
    if (archive_write_open_filename(writer.handle, out.c_str()) != ARCHIVE_OK) {
        fail("packages.archive.pack", archiveError(writer.handle, "archive_write_open_filename"));
    }

    for (const fs::path& file : files) {
        std::error_code cec;
        const fs::path file_abs = fs::weakly_canonical(file, cec);
        if (cec) {
            fail("packages.archive.read", "cannot resolve path '{}': {}", file.string(), cec.message());
        }
        if (file_abs == out_abs) {
            continue; // never embed the output archive into itself
        }
        const std::string rel = file.lexically_relative(root).generic_string();
        std::error_code sec;
        const std::uintmax_t size = fs::file_size(file, sec);
        if (sec) {
            fail("packages.archive.read", "cannot stat file: '{}'", file.string());
        }
        EntryGuard guard;
        guard.entry = archive_entry_new();
        archive_entry_set_pathname(guard.entry, rel.c_str());
        archive_entry_set_filetype(guard.entry, AE_IFREG);
        archive_entry_set_perm(guard.entry, 0644);
        archive_entry_set_size(guard.entry, static_cast<la_int64_t>(size));
        archive_entry_set_mtime(guard.entry, 0, 0);
        archive_entry_set_atime(guard.entry, 0, 0);
        archive_entry_set_ctime(guard.entry, 0, 0);
        archive_entry_set_uid(guard.entry, 0);
        archive_entry_set_gid(guard.entry, 0);
        archive_entry_set_uname(guard.entry, "");
        archive_entry_set_gname(guard.entry, "");
        if (archive_write_header(writer.handle, guard.entry) != ARCHIVE_OK) {
            fail("packages.archive.pack", archiveError(writer.handle, "archive_write_header"));
        }
        writeFileData(writer.handle, file);
    }

    if (archive_write_close(writer.handle) != ARCHIVE_OK) {
        fail("packages.archive.pack", archiveError(writer.handle, "archive_write_close"));
    }
    return trust::utils::sha256File(out);
}

std::vector<std::string> listEntries(const fs::path& archive) {
    ArchiveReader reader;
    reader.handle = archive_read_new();
    archive_read_support_filter_all(reader.handle);
    archive_read_support_format_tar(reader.handle);
    if (archive_read_open_filename(reader.handle, archive.c_str(), 10240) != ARCHIVE_OK) {
        fail("packages.archive.read", archiveError(reader.handle, "archive_read_open_filename"));
    }
    std::vector<std::string> entries;
    struct archive_entry* entry = nullptr;
    for (;;) {
        const int rc = archive_read_next_header(reader.handle, &entry);
        if (rc == ARCHIVE_EOF) {
            break;
        }
        if (rc != ARCHIVE_OK) {
            fail("packages.archive.read", archiveError(reader.handle, "archive_read_next_header"));
        }
        if (archive_entry_filetype(entry) != AE_IFREG) {
            continue;
        }
        const char* name = archive_entry_pathname(entry);
        entries.emplace_back(name != nullptr ? name : "");
    }
    return entries;
}

std::string readArchiveEntry(const fs::path& archive, std::string_view entry) {
    ArchiveReader reader;
    reader.handle = archive_read_new();
    archive_read_support_filter_all(reader.handle);
    archive_read_support_format_tar(reader.handle);
    if (archive_read_open_filename(reader.handle, archive.c_str(), 10240) != ARCHIVE_OK) {
        fail("packages.archive.read", archiveError(reader.handle, "archive_read_open_filename"));
    }
    struct archive_entry* current = nullptr;
    for (;;) {
        const int rc = archive_read_next_header(reader.handle, &current);
        if (rc == ARCHIVE_EOF) {
            break;
        }
        if (rc != ARCHIVE_OK) {
            fail("packages.archive.read", archiveError(reader.handle, "archive_read_next_header"));
        }
        const char* name = archive_entry_pathname(current);
        if (name != nullptr && entry == std::string_view(name) && archive_entry_filetype(current) == AE_IFREG) {
            return readAll(reader.handle);
        }
    }
    fail("packages.archive.entry_missing", "entry not found in package: '{}'", std::string(entry));
}

void unpackArchive(const fs::path& archive, const fs::path& dest) {
    std::error_code ec;
    fs::create_directories(dest, ec);
    ArchiveReader reader;
    reader.handle = archive_read_new();
    archive_read_support_filter_all(reader.handle);
    archive_read_support_format_tar(reader.handle);
    if (archive_read_open_filename(reader.handle, archive.c_str(), 10240) != ARCHIVE_OK) {
        fail("packages.archive.read", archiveError(reader.handle, "archive_read_open_filename"));
    }
    struct archive_entry* entry = nullptr;
    for (;;) {
        const int rc = archive_read_next_header(reader.handle, &entry);
        if (rc == ARCHIVE_EOF) {
            break;
        }
        if (rc != ARCHIVE_OK) {
            fail("packages.archive.read", archiveError(reader.handle, "archive_read_next_header"));
        }
        const char* name = archive_entry_pathname(entry);
        const std::string rel = name != nullptr ? name : "";
        checkSafeEntry(rel);
        const fs::path target = dest / fs::path(rel);
        if (archive_entry_filetype(entry) == AE_IFDIR) {
            fs::create_directories(target, ec);
            continue;
        }
        if (archive_entry_filetype(entry) != AE_IFREG) {
            fail("packages.archive.unsafe", "unsupported entry type (symlink/special): '{}'", rel);
        }
        fs::create_directories(target.parent_path(), ec);
        std::ofstream out(target, std::ios::binary);
        if (!out) {
            fail("packages.archive.extract", "cannot write file: '{}'", target.string());
        }
        std::array<char, 65536> buffer{};
        for (;;) {
            const la_ssize_t got = archive_read_data(reader.handle, buffer.data(), buffer.size());
            if (got < 0) {
                fail("packages.archive.read", archiveError(reader.handle, "archive_read_data"));
            }
            if (got == 0) {
                break;
            }
            out.write(buffer.data(), got);
        }
    }
}

} // namespace trust::packages
