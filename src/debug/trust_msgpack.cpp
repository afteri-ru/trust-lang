#include "debug/trust_source.h"
#include "trust/version.h"
#include <msgpack.h>
#include <algorithm>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace trust {

// ═══════════════════════════════════════════
//    Формат msgpack:
//
//   array 3:
//     [0] uint  – version_major (TRUST_VERSION_MAJOR)
//     [1] uint  – version_minor (TRUST_VERSION_MINOR)
//     [2] array – file_entries:
//       file_entry = array 5:
//         [0] str  – trust_file (нормализованный)
//         [1] str  – cpp_file (нормализованный)
//         [2] uint – cpp_line_inserted
//         [3] array – line_pairs: [[trust_line, cpp_line], ...]
//         [4] (optional) array – var_renames:
//             var_rename = array 4:
//               [0] uint – trust_line
//               [1] uint – cpp_line
//               [2] str  – trust_var
//               [3] str  – cpp_var
//       Если var_renames пуст — вместо [4] может быть nil или отсутствовать.
//
//   Версия НЕ проверяется при распаковке — только записывается для информации.
// ═══════════════════════════════════════════

// ═══════════════════════════════════════════
//        Сериализация TrustSource → msgpack
// ═══════════════════════════════════════════

std::vector<unsigned char> TrustSource::pack(const TrustSource &ts) {
    std::vector<unsigned char> buf;
    msgpack_packer *pk = msgpack_packer_new(
        &buf, [](void *ctx, const char *d, size_t len) -> int {
            auto *b = static_cast<std::vector<unsigned char> *>(ctx);
            b->insert(b->end(), d, d + len);
            return 0;
        });

    const auto &entries = ts.entries();
    size_t nPairs = entries.size();

    // Корень: [major, minor, [file_entries...]]
    msgpack_pack_array(pk, 3);
    msgpack_pack_uint32(pk, TRUST_VERSION_MAJOR);
    msgpack_pack_uint32(pk, TRUST_VERSION_MINOR);

    // file_entries
    msgpack_pack_array(pk, nPairs);
    for (size_t i = 0; i < nPairs; ++i) {
        const auto &entry = entries[i];

        // file_entry = [trust_file, cpp_file, cpp_line_inserted, line_pairs, var_renames (optional)]
        // Всегда пишем 5 элементов: если var_renames пуст — nil
        msgpack_pack_array(pk, 5);

        // [0] trust_file
        msgpack_pack_str(pk, entry.files.first.size());
        msgpack_pack_str_body(pk, entry.files.first.c_str(), entry.files.first.size());

        // [1] cpp_file
        msgpack_pack_str(pk, entry.files.second.size());
        msgpack_pack_str_body(pk, entry.files.second.c_str(), entry.files.second.size());

        // [2] cpp_line_inserted
        msgpack_pack_uint32(pk, static_cast<uint32_t>(entry.cpp_line_inserted));

        // [3] line_pairs: [[trust_line, cpp_line], ...]
        // trustToCppIndex уже отсортирован по trust_line (ключ)
        msgpack_pack_array(pk, entry.trustToCppIndex.size());
        for (const auto &[trustLine, cppLine] : entry.trustToCppIndex) {
            msgpack_pack_array(pk, 2);
            msgpack_pack_int32(pk, trustLine);
            msgpack_pack_int32(pk, cppLine);
        }

        // [4] var_renames: [[trust_line, cpp_line, trust_var, cpp_var], ...] или nil
        if (entry.trustVarMapping.empty()) {
            msgpack_pack_nil(pk);
        } else {
            // Собираем var_renames в вектор и сортируем для детерминированного порядка
            struct VarRenameEntry {
                int trustLine;
                int cppLine;
                std::string_view trustVar;
                std::string_view cppVar;
            };
            std::vector<VarRenameEntry> sortedVars;
            sortedVars.reserve(entry.trustVarMapping.size());
            for (const auto &[trustVar, varInfo] : entry.trustVarMapping) {
                int trustLine = varInfo.second.first;
                int cppLine = varInfo.second.second;
                sortedVars.push_back({trustLine, cppLine, trustVar, varInfo.first});
            }
            // Сортируем по trust_line, затем по cpp_line, затем по trust_var
            std::sort(sortedVars.begin(), sortedVars.end(),
                [](const VarRenameEntry &a, const VarRenameEntry &b) {
                    if (a.trustLine != b.trustLine) return a.trustLine < b.trustLine;
                    if (a.cppLine != b.cppLine) return a.cppLine < b.cppLine;
                    return a.trustVar < b.trustVar;
                });

            msgpack_pack_array(pk, entry.trustVarMapping.size());
            for (const auto &vr : sortedVars) {
                msgpack_pack_array(pk, 4);
                msgpack_pack_int32(pk, vr.trustLine);
                msgpack_pack_int32(pk, vr.cppLine);
                msgpack_pack_str(pk, vr.trustVar.size());
                msgpack_pack_str_body(pk, vr.trustVar.data(), vr.trustVar.size());
                msgpack_pack_str(pk, vr.cppVar.size());
                msgpack_pack_str_body(pk, vr.cppVar.data(), vr.cppVar.size());
            }
        }
    }

    msgpack_packer_free(pk);
    return buf;
}

// ═══════════════════════════════════════════
//        Десериализация msgpack → TrustSource
// ═══════════════════════════════════════════

std::unique_ptr<TrustSource> TrustSource::unpack(const unsigned char *data, size_t size,
                                                  std::string *error) {
    msgpack_unpacked msg;
    msgpack_unpacked_init(&msg);

    auto ret = msgpack_unpack_next(
        &msg, reinterpret_cast<const char *>(data), size, nullptr);
    if (ret != MSGPACK_UNPACK_SUCCESS) {
        if (error) *error = "msgpack unpack failed";
        msgpack_unpacked_destroy(&msg);
        return nullptr;
    }

    // Корень: array 3 (major, minor, file_entries)
    if (msg.data.type != MSGPACK_OBJECT_ARRAY || msg.data.via.array.size < 3) {
        if (error) *error = "root is not an array of 3";
        msgpack_unpacked_destroy(&msg);
        return nullptr;
    }

    msgpack_object *root = msg.data.via.array.ptr;

    // [0] — version_major (не проверяем, только для информации)
    if (root[0].type != MSGPACK_OBJECT_POSITIVE_INTEGER) {
        if (error) *error = "version_major is not an integer";
        msgpack_unpacked_destroy(&msg);
        return nullptr;
    }

    // [1] — version_minor (не проверяем, только для информации)
    if (root[1].type != MSGPACK_OBJECT_POSITIVE_INTEGER) {
        if (error) *error = "version_minor is not an integer";
        msgpack_unpacked_destroy(&msg);
        return nullptr;
    }

    // [2] — file_entries array
    if (root[2].type != MSGPACK_OBJECT_ARRAY) {
        if (error) *error = "file_entries is not an array";
        msgpack_unpacked_destroy(&msg);
        return nullptr;
    }

    // Собираем новую TrustSource через приватный конструктор + публичный интерфейс
    auto newTs = std::unique_ptr<TrustSource>(new TrustSource());
    msgpack_object &fileEntries = root[2];

    for (uint32_t fi = 0; fi < fileEntries.via.array.size; ++fi) {
        msgpack_object &entry = fileEntries.via.array.ptr[fi];
        if (entry.type != MSGPACK_OBJECT_ARRAY || entry.via.array.size < 4) {
            if (error) *error = "file_entry is not an array of 4+";
            msgpack_unpacked_destroy(&msg);
            return nullptr;
        }

        msgpack_object *entryFields = entry.via.array.ptr;

        // [0] — trust_file
        if (entryFields[0].type != MSGPACK_OBJECT_STR) {
            if (error) *error = "trust_file is not a string";
            msgpack_unpacked_destroy(&msg);
            return nullptr;
        }
        std::string trustFile(entryFields[0].via.str.ptr, entryFields[0].via.str.size);

        // [1] — cpp_file
        if (entryFields[1].type != MSGPACK_OBJECT_STR) {
            if (error) *error = "cpp_file is not a string";
            msgpack_unpacked_destroy(&msg);
            return nullptr;
        }
        std::string cppFile(entryFields[1].via.str.ptr, entryFields[1].via.str.size);

        newTs->setFilePair(trustFile, cppFile);

        // [2] — cpp_line_inserted
        if (entry.via.array.size >= 5 && entryFields[2].type == MSGPACK_OBJECT_POSITIVE_INTEGER) {
            newTs->setCppLineInserted(static_cast<size_t>(entryFields[2].via.u64));
        }

        // [3] — line_pairs array
        size_t linePairsIdx = 3;
        if (entryFields[linePairsIdx].type != MSGPACK_OBJECT_ARRAY) {
            if (error) *error = "line_pairs is not an array";
            msgpack_unpacked_destroy(&msg);
            return nullptr;
        }

        msgpack_object &linePairs = entryFields[linePairsIdx];
        for (uint32_t pi = 0; pi < linePairs.via.array.size; ++pi) {
            msgpack_object &pair = linePairs.via.array.ptr[pi];
            if (pair.type != MSGPACK_OBJECT_ARRAY || pair.via.array.size < 2) {
                if (error) *error = "line_pair is not an array of 2";
                msgpack_unpacked_destroy(&msg);
                return nullptr;
            }

            msgpack_object *pairFields = pair.via.array.ptr;

            if (pairFields[0].type != MSGPACK_OBJECT_POSITIVE_INTEGER) {
                if (error) *error = "trust_line is not an integer";
                msgpack_unpacked_destroy(&msg);
                return nullptr;
            }
            int trustLine = static_cast<int>(pairFields[0].via.u64);

            if (pairFields[1].type != MSGPACK_OBJECT_POSITIVE_INTEGER) {
                if (error) *error = "cpp_line is not an integer";
                msgpack_unpacked_destroy(&msg);
                return nullptr;
            }
            int cppLine = static_cast<int>(pairFields[1].via.u64);

            if (!newTs->addLineMapping(trustLine, cppLine)) {
                if (error) *error = "line mapping collision: " +
                    std::to_string(trustLine) + " <-> " + std::to_string(cppLine);
                msgpack_unpacked_destroy(&msg);
                return nullptr;
            }
        }

        // [4] — var_renames (optional, может быть nil)
        size_t varRenamesIdx = 4;
        if (entry.via.array.size > varRenamesIdx && entryFields[varRenamesIdx].type == MSGPACK_OBJECT_ARRAY) {
            msgpack_object &varRenames = entryFields[varRenamesIdx];
            for (uint32_t vi = 0; vi < varRenames.via.array.size; ++vi) {
                msgpack_object &vr = varRenames.via.array.ptr[vi];
                if (vr.type != MSGPACK_OBJECT_ARRAY || vr.via.array.size < 4) {
                    if (error) *error = "var_rename is not an array of 4";
                    msgpack_unpacked_destroy(&msg);
                    return nullptr;
                }

                msgpack_object *vrFields = vr.via.array.ptr;

                // [0] — trust_line
                if (vrFields[0].type != MSGPACK_OBJECT_POSITIVE_INTEGER) {
                    if (error) *error = "var_rename trust_line is not an integer";
                    msgpack_unpacked_destroy(&msg);
                    return nullptr;
                }
                int trustLine = static_cast<int>(vrFields[0].via.u64);

                // [1] — cpp_line
                if (vrFields[1].type != MSGPACK_OBJECT_POSITIVE_INTEGER) {
                    if (error) *error = "var_rename cpp_line is not an integer";
                    msgpack_unpacked_destroy(&msg);
                    return nullptr;
                }
                int cppLine = static_cast<int>(vrFields[1].via.u64);

                // [2] — trust_var
                if (vrFields[2].type != MSGPACK_OBJECT_STR) {
                    if (error) *error = "var_rename trust_var is not a string";
                    msgpack_unpacked_destroy(&msg);
                    return nullptr;
                }
                std::string trustVar(vrFields[2].via.str.ptr, vrFields[2].via.str.size);

                // [3] — cpp_var
                if (vrFields[3].type != MSGPACK_OBJECT_STR) {
                    if (error) *error = "var_rename cpp_var is not a string";
                    msgpack_unpacked_destroy(&msg);
                    return nullptr;
                }
                std::string cppVar(vrFields[3].via.str.ptr, vrFields[3].via.str.size);

                if (!newTs->addVarMapping(trustLine, cppLine, trustVar, cppVar)) {
                    if (error) *error = "failed to add var mapping: " +
                        trustVar + " -> " + cppVar;
                    msgpack_unpacked_destroy(&msg);
                    return nullptr;
                }
            }
        }
    }

    msgpack_unpacked_destroy(&msg);
    return newTs;
}

} // namespace trust