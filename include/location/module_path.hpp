// include/location/module_path.hpp
// ЕДИНСТВЕННЫЙ парсер/сборщик написания пути модуля (`\file`, `\\group\file`,
// `\\\pkg\group\file`). Домены: spelling пути (этот тип) ≠ логическое имя (заголовок) ≠
// путь ФС (`ModuleRegistry::modulePath`) ≠ `ModuleIndex`.
// Header-only: слой `location` — интерфейсная библиотека (см. CMakeLists.txt).

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace trust {

struct ModulePath {
    /// Тип ПЕРВОГО компонента: 1 — файл, 2 — группа, 3 — пакет.
    enum class Kind : uint8_t { File = 1, Group = 2, Package = 3 };
    /// Относительный якорь пути: `\$$` — self-файл, `\\$$` — текущий каталог,
    /// `\\\$$` — корень текущего пакета (для проектного файла — корень проекта).
    enum class Anchor : uint8_t { None, SelfFile, CurrentDir, PackageRoot };

    Kind kind = Kind::File;
    Anchor anchor = Anchor::None;
    /// Компоненты после префикса/якоря (для Package — БЕЗ имени пакета).
    std::vector<std::string> components;
    /// Имя пакета; непусто только для Kind::Package.
    std::string package;

    [[nodiscard]] std::size_t prefixLength() const noexcept { return static_cast<std::size_t>(kind); }
    [[nodiscard]] bool isAnchored() const noexcept { return anchor != Anchor::None; }

    /// Путь из компонентов: тип определяется ФОРМОЙ пути (непустой package — пакетный,
    /// один компонент — файл, несколько — группа). ЕДИНСТВЕННАЯ точка вывода типа из формы:
    /// загрузчик не пересчитывает `kind` вручную при построении пути.
    [[nodiscard]] static ModulePath fromComponents(std::string pathPackage, std::vector<std::string> pathComponents) {
        ModulePath path;
        path.package = std::move(pathPackage);
        path.components = std::move(pathComponents);
        if (!path.package.empty()) {
            path.kind = Kind::Package;
        } else {
            path.kind = path.components.size() == 1 ? Kind::File : Kind::Group;
        }
        return path;
    }

    /// Разбор spelling. `nullopt` — нет префикса `\`/`\\`/`\\\` либо пустой компонент.
    /// Валидность символов компонентов проверяет `isValidModuleLogicalName` (location), не этот тип.
    [[nodiscard]] static std::optional<ModulePath> parse(std::string_view spelling) {
        std::size_t prefix = 0;
        while (prefix < spelling.size() && spelling[prefix] == '\\') {
            ++prefix;
        }
        if (prefix == 0 || prefix > 3) {
            return std::nullopt;
        }
        ModulePath path;
        path.kind = static_cast<Kind>(prefix);
        std::string_view rest = spelling.substr(prefix);
        if (rest.starts_with("$$")) {
            if (rest.size() > 2 && rest[2] != '\\') {
                return std::nullopt;
            }
            path.anchor = prefix == 1 ? Anchor::SelfFile : prefix == 2 ? Anchor::CurrentDir : Anchor::PackageRoot;
            if (rest.size() == 2) {
                return path;
            }
            rest.remove_prefix(3);
            if (path.anchor == Anchor::SelfFile || rest.empty() || !appendComponents(rest, path.components)) {
                return std::nullopt;
            }
            return path;
        }
        if (path.kind == Kind::Package) {
            const std::size_t sep = rest.find('\\');
            const std::string_view package = rest.substr(0, sep);
            if (package.empty()) {
                return std::nullopt;
            }
            path.package = std::string(package);
            if (sep == std::string_view::npos) {
                return path; // `\\\pkg` — весь пакет, без относительного пути
            }
            rest.remove_prefix(sep + 1);
        }
        if (rest.empty() || !appendComponents(rest, path.components)) {
            return std::nullopt;
        }
        return path;
    }

    /// Сборка spelling (обратна `parse`).
    [[nodiscard]] std::string toString() const {
        std::string out(prefixLength(), '\\');
        if (isAnchored()) {
            out += "$$";
            for (const std::string& component : components) {
                out += '\\';
                out += component;
            }
            return out;
        }
        if (kind == Kind::Package) {
            out += package;
            if (!components.empty()) {
                out += '\\';
            }
        }
        for (std::size_t i = 0; i < components.size(); ++i) {
            if (i != 0) {
                out += '\\';
            }
            out += components[i];
        }
        return out;
    }

    /// Относительный путь внутри корня (разделитель '/', без расширения).
    [[nodiscard]] std::string relative() const {
        std::string out;
        for (std::size_t i = 0; i < components.size(); ++i) {
            if (i != 0) {
                out += '/';
            }
            out += components[i];
        }
        return out;
    }

  private:
    static bool appendComponents(std::string_view text, std::vector<std::string>& out) {
        std::size_t start = 0;
        while (start <= text.size()) {
            const std::size_t sep = text.find('\\', start);
            const std::string_view part = text.substr(start, sep == std::string_view::npos ? std::string_view::npos : sep - start);
            if (part.empty()) {
                return false;
            }
            out.emplace_back(part);
            if (sep == std::string_view::npos) {
                break;
            }
            start = sep + 1;
        }
        return true;
    }
};

} // namespace trust
