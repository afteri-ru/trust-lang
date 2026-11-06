// Сохранение сгенерированного .cppt, копия LICENSE, экспорт-таблица и встраивание
// source-map + записи кеша --run в ELF-секции. Модуль (декомпозиция pipeline.cpp).
#include "pipeline/sourcemap/source_map.hpp"
#include "session/run_services.hpp"
#include "utils/build_record.hpp"
#include "utils/utils.hpp"
#include "trust/version.h"
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
namespace trust {

// Относительный путь от каталога исходника (src/pipeline/ → корень проекта). Служит
// источником для копии LICENSE в каталог сборки; текст встроен байтовым массивом
// (генерируемый .inc, cmake/embed_assets.cmake).
static constexpr char kEmbeddedLicense[] = {
#include "pipeline/license_bytes.inc"
    , 0};

// Встроенный текст лицензии без хвостового NUL.
static std::string embeddedLicenseText() {
    return std::string(kEmbeddedLicense, sizeof(kEmbeddedLicense) - 1);
}

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

bool saveCppAndEmbedSourceMap(const RunServices& services, MapperFile cpp_idx, const std::filesystem::path& cppt_path, bool verbose,
                              const std::string& program_record, bool embed_source_map, const std::string& trust_options) {
    namespace fs = std::filesystem;
    {
        // Первая строка: сообщение о том, что файл автогенерируемый, проект и версия
        // компилятора. Кладётся в leading-префикс выходного буфера (первой строкой,
        // до инклудов), а source-map учитывает его через prepend-смещение
        // (toReader → prependSizes).
        // ВАЖНО: в шапке только ДАТА — иначе MD5 выхода в .src_map менялся бы каждый прогон
        // и повторные сборки неизменного кода не давали бы ccache-хитов тулчейна.
        // Текст LICENSE в выходной файл НЕ встраивается - лицензия просто копируется
        // в каталог сборки (ниже), рядом с Makefile/build.conf.
        std::string prefix;
        prefix += "// This file was generated automatically by TrustLang " TRUST_VERSION " on " + utils::currentDate() + "\n";
        // Строка фактически заданных codegen-опций (трассируемость) - только при непустых опциях,
        // чтобы дефолтные сборки оставались неизменными. Входит в prefix (учитывается source-map).
        if (!trust_options.empty()) {
            prefix += "// trust-options: " + trust_options + "\n";
        }
        prefix += "\n";
        services.source().output_prepend_leading(cpp_idx, prefix);

        std::string cpp_content = services.source().output_result(cpp_idx);

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

        // Запись кеша --run в ELF-секцию `.debug_trust_hash`: читается через
        // utils::elf::readElfSection (dlopen не работает для PIE-исполняемых). Эмитится ТОЛЬКО
        // для главного файла (непустая program_record): модульные единицы передают пустую запись,
        // иначе объединение секций от нескольких .cppt сломало бы разбор.
        if (!program_record.empty()) {
            cpp_content += "static const char kTrustSrcHash[] __attribute__((section(\"" + std::string(utils::kRunCacheSection) + "\"), used)) = \"" +
                           cppStringEscape(program_record) + "\";\n\n";
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
    // Байты карты остаются в памяти: тот же буфер, что записан в .src_map, уходит и в артефакт,
    // поэтому встроенная карта не зависит от успеха записи отдельного файла (R40).
    std::vector<unsigned char> map_bytes;
    {
        auto* reader = services.source().toReader();
        if (reader) {
            map_bytes = reader->packToMsgpack();
            std::ofstream ofs(map_path, std::ios::binary);
            if (ofs) {
                ofs.write(reinterpret_cast<const char*>(map_bytes.data()), static_cast<std::streamsize>(map_bytes.size()));
                if (verbose) {
                    trust::errs() << "info: source map saved to " << map_path << "\n";
                }
            } else {
                trust::errs() << "warning: failed to write source map: " << map_path << "\n";
            }
        }
    }

    if (embed_source_map && !map_bytes.empty()) {
        std::ofstream ofs(cppt_path, std::ios::app);
        if (!ofs) {
            return false;
        }
        // Байтовый литерал — стандартный C++: артефакт собирается clang и gcc (R47).
        // Единственное расширение — GNU-атрибут секции (носитель ELF).
        ofs << "\n// Embedded source map (trust → cpp mapping)\n"
            << "static const unsigned char __debug_trust_source_map[]\n"
            << "    __attribute__((section(\".debug_trust_map\"), used)) = {\n";
        static constexpr std::size_t kBytesPerLine = 16;
        static constexpr char kHexDigits[] = "0123456789abcdef";
        for (std::size_t i = 0; i < map_bytes.size(); ++i) {
            if (i % kBytesPerLine == 0) {
                ofs << "    ";
            }
            ofs << "0x" << kHexDigits[(map_bytes[i] >> 4) & 0xF] << kHexDigits[map_bytes[i] & 0xF] << ',';
            if (i % kBytesPerLine == kBytesPerLine - 1 || i + 1 == map_bytes.size()) {
                ofs << "\n";
            }
        }
        ofs << "};\n";
        if (verbose) {
            trust::errs() << "info: source map embedded into " << cppt_path.filename() << "\n";
        }
    }
    return true;
}
} // namespace trust
