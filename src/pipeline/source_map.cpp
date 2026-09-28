// src/pipeline/source_map.cpp
// Сохранение сгенерированного .cppt, копия LICENSE и встраивание source-map + записи кеша
// --run в ELF-секции. Модуль (декомпозиция pipeline.cpp). Trust-интерфейс экспортов
// формируется как Trust-текст (buildInterfaceText) - отдельной ELF-секции больше нет.
#include "pipeline/source_map.hpp"
#include "pipeline/io.hpp"
#include "mangling/mangling.hpp"
#include "utils/hash.hpp"
#include "utils/io.hpp"
#include "trust/version.h"
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
namespace trust {

namespace {

// Маркер локального кеша генерации: первая строка `.cppt` вида `// trust-src-hash: <sha256>`.
// Хеш считается по записи кеша (версия + codegen-опции + md5 всех исходников программы).
constexpr const char* kSrcHashMarkerPrefix = "// trust-src-hash: ";

// True, если первая строка файла равна маркеру (файл сгенерирован из тех же исходников).
bool hasSourceHashMarker(const std::filesystem::path& path, const std::string& marker) {
    std::ifstream in(path);
    if (!in) {
        return false;
    }
    std::string first;
    if (!std::getline(in, first)) {
        return false;
    }
    return first == marker;
}

} // namespace

// -- Встроенная лицензия: текст LICENSE компилируется в бинарник через #embed --
// Относительный путь от каталога исходника (src/pipeline/ → корень проекта). Служит
// источником для копии LICENSE в каталог сборки и для `#embed "LICENSE"` в .cppt.
static constexpr char kEmbeddedLicense[] = {
#embed "../../LICENSE"
    , 0};

// Встроенный текст лицензии без хвостового NUL, добавленного при #embed.
static std::string embeddedLicenseText() {
    return std::string(kEmbeddedLicense, sizeof(kEmbeddedLicense) - 1);
}

// -- Free function: saveCppAndEmbedSourceMap --

// Экранирование для строкового литерала C++: переводы строк, табуляции, кавычки, слэши.
static std::string cppStringEscape(const std::string& s) {
    std::string out;
    for (const char ch : s) {
        if (ch == '\n') {
            out += "\\n";
        } else if (ch == '\r') {
            out += "\\r";
        } else if (ch == '\t') {
            out += "\\t";
        } else {
            if (ch == '"' || ch == '\\') {
                out += '\\';
            }
            out += ch;
        }
    }
    return out;
}

// Trust-интерфейс экспортов: единый источник интерфейсного текста модуля.
// Правая часть объявления (` := %<symbol>;`) формируется mangling::interfaceDecl
// (include/mangling/mangling.hpp).
std::vector<std::string> exportInterfaceDecls(const std::vector<ExportEntry>& exports) {
    std::vector<std::string> decls;
    for (const auto& entry : exports) {
        if (entry.fwdDecl.empty()) {
            continue;
        }
        decls.push_back(mangling::interfaceDecl(entry.fwdDecl, entry.symbolRef));
    }
    return decls;
}

std::string buildInterfaceText(const std::vector<ExportEntry>& exports) {
    std::string text;
    for (const std::string& decl : exportInterfaceDecls(exports)) {
        text += decl;
        text += '\n';
    }
    return text;
}

bool saveCppAndEmbedSourceMap(Context& ctx, MapperFile cpp_idx, const std::filesystem::path& cppt_path, bool verbose, const std::string& program_record,
                              bool embed_source_map, const std::string& trust_options, bool embed_elf_hash, bool* out_skipped) {
    namespace fs = std::filesystem;
    // -- Шапка автогенерируемого файла --
    // Первая строка - маркер локального кеша (см. ниже); далее сообщение об автогенерации,
    // версия компилятора и дата/время. Кладётся в leading-префикс выходного буфера (до
    // инклудов), а source-map учитывает его через prepend-смещение (toReader → prependSizes).
    // Префикс применяется ВСЕГДА (в т.ч. при кэш-хите), иначе emit-режимы, печатающие буфер
    // (--emit-cpp), выводили бы код без шапки. Текст LICENSE НЕ встраивается - только копия
    // в каталог сборки (ниже), рядом с Makefile/build.conf.
    const std::string src_hash_marker = std::string(kSrcHashMarkerPrefix) + utils::sha256Hex(program_record + '\x1f' + trust_options);
    {
        std::string prefix;
        prefix += src_hash_marker;
        prefix += '\n';
        prefix += "// This file was generated automatically by TrustLang " TRUST_VERSION " on " + currentTimestamp() + "\n";
        // Строка фактически заданных codegen-опций (трассируемость) - только при непустых опциях,
        // чтобы дефолтные сборки оставались неизменными. Входит в prefix (учитывается source-map).
        if (!trust_options.empty()) {
            prefix += "// trust-options: " + trust_options + "\n";
        }
        prefix += "\n";
        ctx.source().output_prepend_leading(cpp_idx, prefix);
    }
    std::string cpp_content = ctx.source().output_result(cpp_idx);

    // Локальный кеш: если `.cppt` уже сгенерирован из тех же исходников (маркер совпал),
    // не перезаписываем файл - make сам пропустит пересборку `.o` по mtime.
    if (fs::exists(cppt_path) && hasSourceHashMarker(cppt_path, src_hash_marker)) {
        if (out_skipped != nullptr) {
            *out_skipped = true;
        }
        if (verbose) {
            trust::errs() << "info: " << cppt_path.filename().string() << " is up to date (sources unchanged)\n";
        }
        return true;
    }
    if (out_skipped != nullptr) {
        *out_skipped = false;
    }
    {
        // -- Копия LICENSE в каталог сборки (рядом с Makefile/build.conf). --
        const std::string license_text = embeddedLicenseText();
        if (!license_text.empty()) {
            const fs::path license_path = cppt_path.parent_path() / "LICENSE";
            std::ofstream lf(license_path, std::ios::binary);
            if (lf) {
                lf.write(license_text.data(), static_cast<std::streamsize>(license_text.size()));
            } else if (verbose) {
                trust::errs() << "warning: failed to write LICENSE to " << license_path << "\n";
            }
        }

        // Запись кеша --run: первая строка - версия компилятора "trust-lang\t<TRUST_VERSION>",
        // далее список "файл\tmd5\n" (главный файл, затем модули) - в отдельной ELF-секции
        // .debug_trust_hash. Читается через utils::elf::readElfSection - dlopen не
        // работает для PIE-исполняемых. Только в главном `.cppt` (модульные не эмитят секцию:
        // секции объединяются линкером, дубликаты сломали бы разбор записи).
        if (embed_elf_hash) {
            cpp_content +=
                "static const char kTrustSrcHash[] __attribute__((section(\".debug_trust_hash\"), used)) = \"" + cppStringEscape(program_record) + "\";\n\n";
        }
        std::ofstream ofs(cppt_path);
        if (!ofs) {
            trust::errs() << "error: failed to write output file: " << cppt_path << "\n";
            return false;
        }
        ofs << cpp_content;
    }

    fs::path map_path = cppt_path;
    map_path.replace_extension(".src_map");
    {
        auto* reader = ctx.source().toReader();
        if (reader) {
            auto msgpack_data = reader->packToMsgpack();
            std::ofstream ofs(map_path, std::ios::binary);
            if (ofs) {
                ofs.write(reinterpret_cast<const char*>(msgpack_data.data()), static_cast<std::streamsize>(msgpack_data.size()));
                if (verbose) {
                    trust::errs() << "info: source map saved to " << map_path << "\n";
                }
            } else {
                trust::errs() << "warning: failed to write source map: " << map_path << "\n";
            }
        }
    }

    if (embed_source_map && fs::exists(map_path)) {
        std::ofstream ofs(cppt_path, std::ios::app);
        if (ofs) {
            ofs << "\n// Embedded source map (trust → cpp mapping)\n"
                << "static const unsigned char __debug_trust_source_map[]\n"
                << "    __attribute__((section(\".debug_trust_map\"), used)) = {\n"
                << "    #embed \"" << map_path.filename().string() << "\"\n"
                << "};\n";
            if (verbose) {
                trust::errs() << "info: source map embedded into " << cppt_path.filename() << "\n";
            }
        }
    }
    return true;
}
} // namespace trust
