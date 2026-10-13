// include/location/module_name.hpp
// ЕДИНАЯ формула имени модуля в C++-идентификаторах. Один encoder обслуживает:
//   * предопределённый макрос `@__MODULE_NAME__`;
//   * имя entry-функции (`moduleEntryIdent`);
//   * модульную изоляцию C++-имён (префикс/namespace, этап B);
//   * проверку уникальности загруженных модулей.
// Слой `location` выбран потому, что encoder нужен и `syntax` (парсер/макросы), и `types`
// (C++-проекция имени типа), и `transpiler`, и `module_loader` — без зависимостей между ними.

#pragma once

#include "location/module_index.hpp"

#include <cctype>
#include <cstddef>
#include <string>
#include <string_view>

namespace trust {

/// Провайдер логического имени модуля (реализация — `ModuleResolver`/`ModuleLoader`).
/// Нужен слоям, которые проецируют модуль в C++-имя (`types`, `transpiler`), не завися
/// от загрузчика: невладеющий указатель внедряется владельцем компиляции (Pipeline).
class ModuleNameLookup {
  public:
    ModuleNameLookup() = default;
    virtual ~ModuleNameLookup() = default;
    ModuleNameLookup(const ModuleNameLookup&) = delete;
    ModuleNameLookup& operator=(const ModuleNameLookup&) = delete;

    /// Логическое имя модуля (заголовок без `;` либо stem программы). Индекс обязан быть
    /// зарегистрированным (иначе реализация сообщает FAULT/EXPECT).
    [[nodiscard]] virtual std::string_view moduleLogicalName(ModuleIndex index) const = 0;
};

/// Кодирование логического пути файла в идентификатор: тип scope (`\` файл, `\\` группа,
/// `\\\` пакет) входит в имя, разделитель `\` -> `_`; для СОСТАВНЫХ путей символ `_`
/// дополнительно кодируется как `__` (иначе `\\a\b_c` и `\\a_b\c` дали бы одно имя).
/// Однокомпонентное имя (`\file`/stem) остаётся как есть.
/// Тип scope обязателен: без него проектный `\\a\b` и пакетный `\\\a\b` совпали бы
/// (`a_b`), а C++-символы двух разных модулей — столкнулись бы при линковке.
/// Остаточная неоднозначность (одинаковый путь из разных корней) диагностируется загрузчиком.
[[nodiscard]] inline std::string encodeModuleNameForIdent(std::string_view logicalPath) {
    std::size_t markers = 0;
    while (markers < logicalPath.size() && logicalPath[markers] == '\\') {
        ++markers;
    }
    const std::string_view path = logicalPath.substr(markers);
    if (path.find('\\') == std::string_view::npos) {
        return std::string(path);
    }
    std::string out;
    out.reserve(path.size() * 2 + 2);
    switch (markers) {
    case 2: // `\\group\file` — файл группы
        out.append("m_");
        break;
    case 3: // `\\\pkg\...` — файл пакета
        out.append("p_");
        break;
    default: // `\file` — файл в корне проекта; иные формы загрузчик отвергает
        break;
    }
    for (const char ch : path) {
        if (ch == '\\') {
            out.push_back('_');
        } else if (ch == '_') {
            out.append("__");
        } else {
            out.push_back(ch);
        }
    }
    return out;
}

/// Имя entry-функции модуля в C++: `<кодированное имя модуля>__main__`.
/// ЕДИНСТВЕННАЯ формула: её строит DSL-макрос `main`, её же проверяет обёртка `_main.cppt`.
[[nodiscard]] inline std::string moduleEntryIdent(std::string_view logicalPath) {
    return encodeModuleNameForIdent(logicalPath) + "__main__";
}

/// Модуль ИМПОРТИРУЕМ (есть заголовок `\path…`): только такие юниты изолируются в C++.
/// Headerless root-программа (logicalName = stem без `\`) импортирована быть не может —
/// ссылаться на её символы некому, изоляция не нужна (генерируемый C++ проще).
[[nodiscard]] inline bool isImportableModuleName(std::string_view logicalPath) noexcept {
    return !logicalPath.empty() && logicalPath.front() == '\\';
}

/// C++-namespace модуля (`<enc>`, без префикса `c_`): ЕДИНСТВЕННЫЙ механизм модульной изоляции
/// (именованные сущности, типы и свободные операторы). Не конфликтует с trust-namespace
/// (`c_<ns>`), с идентификаторами (`c_<name>`) и с entry-функцией. Пусто — без изоляции
/// (неимпортируемый headerless root).
[[nodiscard]] inline std::string moduleCppNamespace(std::string_view logicalPath) {
    if (!isImportableModuleName(logicalPath)) {
        return {};
    }
    return encodeModuleNameForIdent(logicalPath);
}

/// Проверяет логическое имя модуля: 1–3 ведущих '\' (корни), затем компоненты через '\'.
/// Символы — строчные латинские, цифры и '_'; '_' не может начинать/завершать имя или
/// компонент; пустые компоненты запрещены (иначе один файл достижим разными написаниями).
[[nodiscard]] inline bool isValidModuleLogicalName(std::string_view name) noexcept {
    std::size_t pos = 0;
    while (pos < name.size() && name[pos] == '\\') {
        ++pos;
    }
    if (pos == 0 || pos > 3 || pos == name.size()) {
        return false;
    }
    bool componentStart = true;
    bool prevUnderscore = false;
    for (; pos < name.size(); ++pos) {
        const char c = name[pos];
        if (c == '\\') {
            if (componentStart || prevUnderscore) {
                return false;
            }
            componentStart = true;
            prevUnderscore = false;
            continue;
        }
        if (!(c == '_' || std::islower(static_cast<unsigned char>(c)) || std::isdigit(static_cast<unsigned char>(c)))) {
            return false;
        }
        if (componentStart && c == '_') {
            return false;
        }
        componentStart = false;
        prevUnderscore = (c == '_');
    }
    return !componentStart && !prevUnderscore;
}

} // namespace trust
