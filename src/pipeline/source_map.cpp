// src/pipeline/source_map.cpp
// Сохранение сгенерированного .cppt, копия LICENSE и встраивание source-map + записи кеша
// --run в ELF-секции. Модуль (декомпозиция pipeline.cpp). Semantic-интерфейс пакета (.pcp)
// формируется как Trust-текст (buildInterfaceText) - отдельной ELF-секции больше нет.
#include "pipeline/source_map.hpp"
#include "pipeline/io.hpp"
#include "mangling/mangling.hpp"
#include "utils/io.hpp"
#include "trust/version.h"
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
namespace trust {

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

// Trust-интерфейс экспортов: единый источник для интерфейсного `.src` в `.pcp`.
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
                              bool embed_source_map, const std::string& trust_options) {
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
        prefix += "// This file was generated automatically by TrustLang " TRUST_VERSION " on " + currentTimestamp() + "\n";
        // Строка фактически заданных codegen-опций (трассируемость) - только при непустых опциях,
        // чтобы дефолтные сборки оставались неизменными. Входит в prefix (учитывается source-map).
        if (!trust_options.empty()) {
            prefix += "// trust-options: " + trust_options + "\n";
        }
        prefix += "\n";
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

        // Запись кеша --run: первая строка - версия компилятора "trust-lang\t<TRUST_VERSION>",
        // далее список "файл\tmd5\n" (главный файл, затем модули) - в отдельной ELF-секции
        // .debug_trust_hash. Читается через utils::elf::readElfSection - dlopen не
        // работает для PIE-исполняемых.
        cpp_content +=
            "static const char kTrustSrcHash[] __attribute__((section(\".debug_trust_hash\"), used)) = \"" + cppStringEscape(program_record) + "\";\n\n";
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
