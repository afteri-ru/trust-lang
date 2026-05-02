#ifndef TRUST_SOURCE_H
#define TRUST_SOURCE_H

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <optional>
#include <utility>

namespace trust {

// ── Основные типы ──

using FilePair = std::pair<std::string, std::string>;  // {trust_file, cpp_file}
using VarPair = std::pair<std::string, std::string>;   // {trust_var, cpp_var}

// Единый тип для номеров строк
using LineNumber = uint32_t;

using LinePair = std::pair<LineNumber, LineNumber>;     // {trust_line, cpp_line}

// Результат трансляции строки: {file_name, line_number}
using LineMapValue = std::pair<std::string, LineNumber>;

// Элемент VarMap: {cpp_var, {trust_line, cpp_line}}
using VarMapValue = std::pair<std::string, LinePair>;

// ── VarMapInfo: результат поиска переменной ──

struct VarMapInfo {
    FilePair files; // {trust_file, cpp_file}
    VarPair vars;   // {trust_var, cpp_var}
    LinePair lines; // {trust_line, cpp_line}
};

// ── FilePairEntry: единая структура хранения для пары файлов ──

struct FilePairEntry {
    FilePair files;                                                      // {trust_file, cpp_file}
    size_t cpp_line_inserted;
    std::map<LineNumber, LineNumber> trustToCppIndex;                    // trust_line → cpp_line (монотонно)
    std::unordered_map<std::string, VarMapValue> trustVarMapping;        // trust_var → VarMapValue
    std::unordered_multimap<std::string, std::string> cppToTrustVar;     // cpp_var → trust_var
};

// ── TrustSource ──

class TrustSource {
  public:
    TrustSource() = default;
    // basePath обязан быть непустым. При пустом cppPath берётся current_path()+"/.trust/".
    explicit TrustSource(std::string_view basePath, std::string_view cppPath = "");

    // === Добавление данных (работает с current_) ===

    const FilePairEntry *setFilePair(std::string_view trustFile, std::string_view cppFile);
    const FilePairEntry *currentFilePair() const;
    void setCppLineInserted(size_t n);

    bool addLineMapping(LineNumber trustLine, LineNumber cppLine);                         // trust_line → cpp_line
    bool addVarMapping(LineNumber trustLine, LineNumber cppLine,
                       std::string_view trustVar, std::string_view cppVar);                // trust_var → cpp_var

    static size_t new_line_count(std::string_view s);
    void include_append(const std::vector<std::string> &files);

    // === Трансляция строк (const, линейный поиск по entries_) ===

    // nearestTrustToCpp: exact, fallback — ближайший (≤) trust line
    std::optional<LineMapValue> nearestTrustToCpp(std::string_view trustFile, LineNumber trustLine) const;
    // nearestCppToTrust: обратный поиск cpp line → trust line через бинарный поиск по trustToCppIndex
    std::optional<LineMapValue> nearestCppToTrust(std::string_view cppFile, LineNumber cppLine) const;

    // === Трансляция переменных (const, линейный поиск по entries_) ===

    std::optional<VarMapInfo> getCppVar(std::string_view trustFile, LineNumber trustLine, std::string_view trustVar) const;
    std::optional<VarMapInfo> getTrustVar(std::string_view cppFile, LineNumber cppLine, std::string_view cppVar) const;

    // === Доступ к данным (для pack/unpack, тестов) ===

    const std::vector<FilePairEntry> &entries() const;

    // === Загрузка (фабричный метод: возвращает константный объект только для чтения) ===

    static std::unique_ptr<const TrustSource> LoadFromBinary(
        const std::string &binaryPath,
        const std::string &mapPath = "");

    // === Сериализация, утилиты ===

    static std::vector<unsigned char> pack(const TrustSource &ts);
    static std::unique_ptr<TrustSource> unpack(const unsigned char *data, size_t size, std::string *error = nullptr);
    static std::string generateEmbeddedMapCode(const std::vector<unsigned char> &mapData);
    static bool writeMapFile(const std::vector<unsigned char> &mapData, const std::string &path);

  private:
    // Единая нормализация пути:
    // - абсолютные пути → удаляется префикс baseDir + "/"
    // - относительные пути → lexically_normal() как есть (без резолвинга)
    // - если baseDir пуст → путь возвращается как есть (lexically_normal)
    std::string normalizePath(std::string_view path, const std::string &baseDir) const;

    // Проверяет монотонность trust_line/cpp_line для новой записи (trustLine, cppLine)
    // относительно текущего состояния trustToCppIndex у current_.
    // Если trustLine уже существует — проверяет что cppLine не уменьшился.
    // Возвращает false при нарушении монотонности.
    bool checkMonotonicity(LineNumber trustLine, LineNumber cppLine) const;

    static LineNumber findNearestCppLine(const std::map<LineNumber, LineNumber> &idx, LineNumber cppLine);

    static std::vector<unsigned char> readElfSection(const std::string &binaryPath, const std::string &sectionName);

    FilePairEntry *current_ = nullptr;
    std::vector<FilePairEntry> entries_;
    std::string base_directory_;
    std::string cpp_directory_;
};

} // namespace trust

#endif // TRUST_SOURCE_H