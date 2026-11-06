// Разобранный список масок импорта `\module(mod, m1, m2)`. ЕДИНСТВЕННЫЙ парсер масок:
// потребители (загрузчик/семантика) не разбирают строку повторно и не дублируют правила
// матчинга. Маска с `::` сравнивается с полным qualified trust-именем, без `::` - с коротким;
// glob (`*`/`?`), через запятую = OR; пустая строка = «без фильтра» (все имена).

#pragma once

#include "utils/strings/strings_text.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <vector>

namespace trust::utils {

class ImportMasks {
  public:
    ImportMasks() = default;

    /// Разобрать строку масок (запятые + пробелы вокруг). Пустой элемент игнорируется.
    [[nodiscard]] static ImportMasks parse(std::string_view commaMasks) {
        ImportMasks masks;
        std::size_t start = 0;
        while (start <= commaMasks.size()) {
            std::size_t end = commaMasks.find(',', start);
            if (end == std::string_view::npos) {
                end = commaMasks.size();
            }
            std::string_view mask = commaMasks.substr(start, end - start);
            while (!mask.empty() && std::isspace(static_cast<unsigned char>(mask.front()))) {
                mask.remove_prefix(1);
            }
            while (!mask.empty() && std::isspace(static_cast<unsigned char>(mask.back()))) {
                mask.remove_suffix(1);
            }
            if (!mask.empty()) {
                masks.m_patterns.emplace_back(mask);
            }
            if (end == commaMasks.size()) {
                break;
            }
            start = end + 1;
        }
        return masks;
    }

    /// Нет масок = «без фильтра»: подходит любое имя.
    [[nodiscard]] bool empty() const noexcept { return m_patterns.empty(); }

    [[nodiscard]] bool matches(std::string_view qualifiedName) const {
        return matches(utils::namespacePathOf(qualifiedName), utils::shortNameOf(qualifiedName));
    }

    /// Матчинг по УЖЕ разделённым частям имени (без склейки в общем случае).
    [[nodiscard]] bool matches(std::string_view namespacePath, std::string_view name) const {
        if (m_patterns.empty()) {
            return true;
        }
        for (const std::string& pattern : m_patterns) {
            if (matchesPattern(pattern, namespacePath, name)) {
                return true;
            }
        }
        return false;
    }

    /// Маски, не совпавшие ни с одним из переданных qualified-имён (в порядке объявления).
    /// Принимаются views: копий qualified-имён не создаётся.
    [[nodiscard]] std::vector<std::string> unmatchedIn(const std::vector<std::string_view>& qualifiedNames) const {
        std::vector<std::string> result;
        if (m_patterns.empty()) {
            return result; // «без фильтра»: несовпавших масок нет
        }
        for (const std::string& pattern : m_patterns) {
            bool matched = false;
            for (const auto& name : qualifiedNames) {
                if (matchesPattern(pattern, name)) {
                    matched = true;
                    break;
                }
            }
            if (!matched) {
                result.push_back(pattern);
            }
        }
        return result;
    }

    [[nodiscard]] const std::vector<std::string>& patterns() const noexcept { return m_patterns; }

    /// ЕДИНСТВЕННАЯ нормализация набора масок: порядок и дубликаты не значимы (сорт+уникализация),
    /// кодирование `длина:паттерн` исключает коллизии при любых символах. Ключ кэша фильтра и
    /// дедупликации сайтов импорта.
    [[nodiscard]] std::string normalizedKey() const {
        std::vector<std::string_view> sorted;
        sorted.reserve(m_patterns.size());
        for (const std::string& pattern : m_patterns) {
            sorted.emplace_back(pattern);
        }
        std::sort(sorted.begin(), sorted.end());
        sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());
        std::string key;
        for (const std::string_view pattern : sorted) {
            key += std::to_string(pattern.size());
            key += ':';
            key += pattern;
        }
        return key;
    }

    /// Тот же набор масок: сравнение по нормализованному ключу (порядок/дубликаты не значимы).
    /// Нужен для дедупликации сайтов импорта, запомненных семантикой (проекция видимости для LSP).
    [[nodiscard]] bool sameAs(const ImportMasks& other) const { return normalizedKey() == other.normalizedKey(); }

  private:
    /// Единственное правило матчинга: маска с `::` — по полному имени, без `::` — по короткому.
    [[nodiscard]] static bool matchesPattern(const std::string& pattern, std::string_view namespacePath, std::string_view name) {
        if (pattern.find("::") == std::string::npos) {
            return globMatch(pattern, name);
        }
        return globMatch(pattern, utils::qualifyTrustName(namespacePath, name));
    }

    [[nodiscard]] static bool matchesPattern(const std::string& pattern, std::string_view qualifiedName) {
        return matchesPattern(pattern, utils::namespacePathOf(qualifiedName), utils::shortNameOf(qualifiedName));
    }

    std::vector<std::string> m_patterns;
};

/// Оставляет в `common` только маски, присутствующие также в `candidate` (порядок `common`
/// сохраняется). Сайт импорта с несколькими файловыми частями: маска несовпавшая, только если не
/// совпала ни в одной части.
inline void retainMasksPresentInBoth(std::vector<std::string>& common, const std::vector<std::string>& candidate) {
    std::erase_if(common, [&candidate](const std::string& mask) { return std::find(candidate.begin(), candidate.end(), mask) == candidate.end(); });
}

} // namespace trust::utils
