// src/pm/package.cpp
#include "pm/package.hpp"

#include "pm/protocol.hpp"

#include <archive.h>
#include <archive_entry.h>

#include <algorithm>
#include <array>
#include <fstream>
#include <system_error>
#include <vector>

namespace trust::pm {
namespace {

namespace fs = std::filesystem;

[[noreturn]] void fail(const char* code, std::string message, nlohmann::json context) {
    throw PmException(PmError{code, std::move(message), std::move(context)});
}

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
        fail("pm.package.read", "cannot read file: " + file.string(), nlohmann::json{{"file", file.string()}});
    }
    std::array<char, 65536> buffer{};
    while (in) {
        in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize got = in.gcount();
        if (got > 0) {
            const la_ssize_t written = archive_write_data(a, buffer.data(), static_cast<std::size_t>(got));
            if (written < 0) {
                fail("pm.package.pack", archiveError(a, "archive_write_data"), nlohmann::json{{"file", file.string()}});
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
            fail("pm.package.read", archiveError(a, "archive_read_data"), nlohmann::json::object());
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
        fail("pm.package.unsafe", "unsafe entry path: " + path, nlohmann::json{{"entry", path}});
    }
    std::size_t pos = 0;
    while (pos <= path.size()) {
        const std::size_t slash = path.find('/', pos);
        const std::string_view part = (slash == std::string::npos) ? std::string_view(path).substr(pos) : std::string_view(path).substr(pos, slash - pos);
        if (part == "..") {
            fail("pm.package.unsafe", "unsafe entry path: " + path, nlohmann::json{{"entry", path}});
        }
        if (slash == std::string::npos) {
            break;
        }
        pos = slash + 1;
    }
}

} // namespace

void packPackage(const fs::path& root, const fs::path& out) {
    std::error_code ec;
    if (!fs::is_directory(root, ec)) {
        fail("pm.package.root", "package root is not a directory: " + root.string(), nlohmann::json{{"root", root.string()}});
    }

    std::vector<fs::path> files;
    for (fs::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code fec;
        if (it->is_regular_file(fec)) {
            files.push_back(it->path());
        }
    }
    const fs::path out_abs = fs::weakly_canonical(out, ec);
    // Sort by relative path for a deterministic archive.
    std::sort(files.begin(), files.end(),
              [&root](const fs::path& a, const fs::path& b) { return fs::relative(a, root).generic_string() < fs::relative(b, root).generic_string(); });

    ArchiveWriter writer;
    writer.handle = archive_write_new();
    if (writer.handle == nullptr) {
        fail("pm.package.pack", "cannot create libarchive writer", nlohmann::json::object());
    }
    archive_write_set_format_pax_restricted(writer.handle);
    archive_write_add_filter_zstd(writer.handle);
    archive_write_set_filter_option(writer.handle, "zstd", "compression-level", "19");
    if (archive_write_open_filename(writer.handle, out.c_str()) != ARCHIVE_OK) {
        fail("pm.package.pack", archiveError(writer.handle, "archive_write_open_filename"), nlohmann::json{{"out", out.string()}});
    }

    for (const fs::path& file : files) {
        std::error_code cec;
        if (!out_abs.empty() && fs::weakly_canonical(file, cec) == out_abs) {
            continue; // never embed the output archive into itself
        }
        const std::string rel = fs::relative(file, root).generic_string();
        std::error_code sec;
        const std::uintmax_t size = fs::file_size(file, sec);
        if (sec) {
            fail("pm.package.read", "cannot stat file: " + file.string(), nlohmann::json{{"file", file.string()}});
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
            fail("pm.package.pack", archiveError(writer.handle, "archive_write_header"), nlohmann::json{{"entry", rel}});
        }
        writeFileData(writer.handle, file);
    }

    if (archive_write_close(writer.handle) != ARCHIVE_OK) {
        fail("pm.package.pack", archiveError(writer.handle, "archive_write_close"), nlohmann::json{{"out", out.string()}});
    }
}

std::vector<std::string> listPackage(const fs::path& archive) {
    ArchiveReader reader;
    reader.handle = archive_read_new();
    archive_read_support_filter_all(reader.handle);
    archive_read_support_format_tar(reader.handle);
    if (archive_read_open_filename(reader.handle, archive.c_str(), 10240) != ARCHIVE_OK) {
        fail("pm.package.read", archiveError(reader.handle, "archive_read_open_filename"), nlohmann::json{{"archive", archive.string()}});
    }
    std::vector<std::string> entries;
    struct archive_entry* entry = nullptr;
    for (;;) {
        const int rc = archive_read_next_header(reader.handle, &entry);
        if (rc == ARCHIVE_EOF) {
            break;
        }
        if (rc != ARCHIVE_OK) {
            fail("pm.package.read", archiveError(reader.handle, "archive_read_next_header"), nlohmann::json{{"archive", archive.string()}});
        }
        if (archive_entry_filetype(entry) != AE_IFREG) {
            continue;
        }
        const char* name = archive_entry_pathname(entry);
        entries.emplace_back(name != nullptr ? name : "");
    }
    return entries;
}

std::string readPackageEntry(const fs::path& archive, const std::string& entry) {
    ArchiveReader reader;
    reader.handle = archive_read_new();
    archive_read_support_filter_all(reader.handle);
    archive_read_support_format_tar(reader.handle);
    if (archive_read_open_filename(reader.handle, archive.c_str(), 10240) != ARCHIVE_OK) {
        fail("pm.package.read", archiveError(reader.handle, "archive_read_open_filename"), nlohmann::json{{"archive", archive.string()}});
    }
    struct archive_entry* current = nullptr;
    for (;;) {
        const int rc = archive_read_next_header(reader.handle, &current);
        if (rc == ARCHIVE_EOF) {
            break;
        }
        if (rc != ARCHIVE_OK) {
            fail("pm.package.read", archiveError(reader.handle, "archive_read_next_header"), nlohmann::json{{"archive", archive.string()}});
        }
        const char* name = archive_entry_pathname(current);
        if (name != nullptr && entry == name && archive_entry_filetype(current) == AE_IFREG) {
            return readAll(reader.handle);
        }
    }
    fail("pm.package.entry_missing", "entry not found in package: " + entry, nlohmann::json{{"archive", archive.string()}, {"entry", entry}});
}

void extractPackage(const fs::path& archive, const fs::path& dest) {
    std::error_code ec;
    fs::create_directories(dest, ec);
    ArchiveReader reader;
    reader.handle = archive_read_new();
    archive_read_support_filter_all(reader.handle);
    archive_read_support_format_tar(reader.handle);
    if (archive_read_open_filename(reader.handle, archive.c_str(), 10240) != ARCHIVE_OK) {
        fail("pm.package.read", archiveError(reader.handle, "archive_read_open_filename"), nlohmann::json{{"archive", archive.string()}});
    }
    struct archive_entry* entry = nullptr;
    for (;;) {
        const int rc = archive_read_next_header(reader.handle, &entry);
        if (rc == ARCHIVE_EOF) {
            break;
        }
        if (rc != ARCHIVE_OK) {
            fail("pm.package.read", archiveError(reader.handle, "archive_read_next_header"), nlohmann::json{{"archive", archive.string()}});
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
            fail("pm.package.unsafe", "unsupported entry type (symlink/special): " + rel, nlohmann::json{{"entry", rel}});
        }
        fs::create_directories(target.parent_path(), ec);
        std::ofstream out(target, std::ios::binary);
        if (!out) {
            fail("pm.package.extract", "cannot write file: " + target.string(), nlohmann::json{{"file", target.string()}});
        }
        std::array<char, 65536> buffer{};
        for (;;) {
            const la_ssize_t got = archive_read_data(reader.handle, buffer.data(), buffer.size());
            if (got < 0) {
                fail("pm.package.read", archiveError(reader.handle, "archive_read_data"), nlohmann::json{{"entry", rel}});
            }
            if (got == 0) {
                break;
            }
            out.write(buffer.data(), got);
        }
    }
}

} // namespace trust::pm
