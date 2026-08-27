// src/pipeline/source_map.cpp
// Сохранение сгенерированного .cppt, копия LICENSE, экспорт-таблица и встраивание
// source-map + записи кеша --run в ELF-секции. Модуль (декомпозиция pipeline.cpp).
#include "pipeline/source_map.hpp"
#include "pipeline/io.hpp"
#include "utils/io.hpp"
#include "utils/file_io.hpp"
#include "trust/version.h"
#include <filesystem>
#include <fstream>
#include <string>
namespace trust {

// -- Helper: read a small text file into a string (for #embed) --
static std::string readFileContents(const std::filesystem::path& path) {
    auto content = trust::utils::FileIO::read<std::string>(path.string());
    return content ? *content : std::string{};
}

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

bool saveCppAndEmbedSourceMap(Context& ctx, MapperFile cpp_idx, const std::filesystem::path& cppt_path, bool verbose, const std::vector<ExportEntry>& exports,
                              bool embed_export_table, const std::string& program_record) {
    namespace fs = std::filesystem;
    {
        // -- Шапка автогенерируемого файла (1-я строка) --
        // Первая строка: сообщение о том, что файл автогенерируемый, название проекта,
        // полная версия компилятора и дата/время генерации. Кладётся в leading-префикс
        // выходного буфера (первой строкой, до инклудов), а source-map учитывает его
        // через prepend-смещение (toReader → prependSizes).
        // Текст LICENSE в выходной файл НЕ встраивается - лицензия просто копируется
        // в каталог сборки (ниже), рядом с Makefile/build.conf.
        std::string prefix;
        prefix += "// This file was generated automatically by TrustLang " TRUST_VERSION_FULL " on " + currentTimestamp() + "\n\n";
        ctx.source().output_prepend_leading(cpp_idx, prefix);

        std::string cpp_content = ctx.source().output_result(cpp_idx);

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

        if (embed_export_table) {
            cpp_content += "\n// Exported symbols for dynamic loading\n";
            std::string module_api_content = readFileContents(PROJECT_INCLUDE_DIR "/runtime/module_api.h");
            if (!module_api_content.empty()) {
                static const std::string pragma_once = "#pragma once\n";
                auto pos = module_api_content.find(pragma_once);
                if (pos != std::string::npos) {
                    module_api_content.erase(pos, pragma_once.length());
                }
                cpp_content += "// Embedded from include/runtime/module_api.h\n";
                cpp_content += module_api_content;
                cpp_content += "\n";
            } else {
                cpp_content += "struct __trust_export_entry {\n"
                               "    const char* name;\n"
                               "    void* addr;\n"
                               "};\n"
                               "struct __trust_exports {\n"
                               "    int count;\n"
                               "    const char* version;\n"
                               "    const __trust_export_entry* entries;\n"
                               "    const char* decls;\n"
                               "    const char* srcHash;\n"
                               "};\n\n";
            }
            // Строка-перечисление экспортируемых ПРЕДВАРИТЕЛЬНЫХ ОБЪЯВЛЕНИЙ в Trust-синтаксисе
            // (разделитель '\n') - реальные семантические конструкции, пригодные для парсинга
            // при загрузке модуля как бинарного файла (напр. "x:Int32 := ...;\n").
            std::string decls;
            for (const auto& entry : exports) {
                if (entry.fwdDecl.empty()) {
                    continue;
                }
                decls += entry.fwdDecl;
                decls += '\n';
            }
            cpp_content += "static const __trust_export_entry __trust_export_entries[] = {\n";
            for (const auto& entry : exports) {
                cpp_content += std::format("    {{ \"{}\", reinterpret_cast<void*>(&::{}) }},\n", entry.trustName, entry.cppName);
            }
            cpp_content += "};\n\n";
            cpp_content += "static const char __trust_export_decls[] = \"";
            cpp_content += cppStringEscape(decls);
            cpp_content += "\";\n\n";
            // Запись кеша --run: первая строка - версия компилятора "trust-lang\t<TRUST_VERSION_FULL>",
            // далее список "файл\tmd5\n" (главный файл, затем модули) - в отдельной ELF-секции
            // .debug_trust_hash. Читается через utils::elf::readElfSection - dlopen не
            // работает для PIE-исполняемых. Та же строка доступна и как __trust_exports.srcHash.
            cpp_content +=
                "static const char kTrustSrcHash[] __attribute__((section(\".debug_trust_hash\"), used)) = \"" + cppStringEscape(program_record) + "\";\n\n";
            cpp_content += "extern \"C\" __trust_exports __trust_get_exports(void)\n"
                           "    __attribute__((visibility(\"default\")));\n"
                           "extern \"C\" __trust_exports __trust_get_exports(void) {\n"
                           "    __trust_exports result;\n"
                           "    result.count = static_cast<int>("
                           "sizeof(__trust_export_entries) / sizeof(__trust_export_entries[0]));\n"
                           "    result.version = \"" TRUST_VERSION_FULL "\";\n"
                           "    result.entries = __trust_export_entries;\n"
                           "    result.decls = __trust_export_decls;\n"
                           "    result.srcHash = kTrustSrcHash;\n"
                           "    return result;\n"
                           "}\n\n";
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

    if (fs::exists(map_path)) {
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
