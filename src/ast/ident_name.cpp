// src/ast/ident_name.cpp
// Реализация IdentName - методы проверки, нормализации, манглинга идентификаторов.
// Перенесено из trust::Ident, работа через text()/m_text вместо *this.

#include "ast/ident_name.hpp"
#include "ast/attr_pool.hpp"
#include "syntax/term.h"
#include "utils/error.hpp"

#include <cctype>
#include <algorithm>
#include <filesystem>

namespace trust {

// ----------------------------------------------
// Конструкторы без Term (test-only): text() из m_text, range() → EXPECT.
// ----------------------------------------------

IdentName::IdentName(std::string name, AttrPool* pool)
: HasText(ParserToken::Kind::Ident, std::move(name)) {
    stripCaretAndApplyReadonly(pool);
}

// -- Терм-конструктор: имя читается из Term и нормализуется по kind=Ident (срез '^').
//    Признак иммутабельности → attr::ReadOnly применяется отдельно (convertAttrsToNode);
//    stripCaretAndApplyReadonly здесь НЕ вызывается, чтобы не задвоить attr.
IdentName::IdentName(TermPtr term, AttrPool* pool)
: IdentName(std::move(term), ParserToken::Kind::Ident, pool) {
}

IdentName::IdentName(TermPtr term, ParserToken::Kind k, AttrPool* pool)
: HasText(k, std::move(term)) {
    // Базовый HasText уже делает EXPECT(m_term) и m_text = normalizeTermText(k, ...).
    (void)pool;
}

// ----------------------------------------------
// Вспомогательные утилиты
// ----------------------------------------------

static bool is_unicode_letter(char32_t c) {
    return (c >= 0x0041 && c <= 0x005A) || // A-Z
           (c >= 0x0061 && c <= 0x007A) || // a-z
           (c >= 0x00C0 && c <= 0x024F) || // Latin Extended
           (c >= 0x0400 && c <= 0x04FF) || // Cyrillic
           (c >= 0x0500 && c <= 0x052F) || // Cyrillic Supplement
           (c >= 0x2DE0 && c <= 0x2DFF) || // Cyrillic Extended-A
           (c >= 0xA640 && c <= 0xA69F) || // Cyrillic Extended-B
           (c >= 0x0370 && c <= 0x03FF) || // Greek and Coptic
           (c >= 0x1F00 && c <= 0x1FFF) || // Greek Extended
           (c >= 0x0100 && c <= 0x017F) || // Latin Extended-A
           (c >= 0x0180 && c <= 0x024F) || // Latin Extended-B
           (c >= 0x1E00 && c <= 0x1EFF) || // Latin Extended Additional
           (c >= 0x4E00 && c <= 0x9FFF) || // CJK Unified Ideographs
           (c >= 0x3040 && c <= 0x309F) || // Hiragana
           (c >= 0x30A0 && c <= 0x30FF);   // Katakana
}

static bool is_unicode_digit(char32_t c) {
    return (c >= 0x0030 && c <= 0x0039) || // 0-9
           (c >= 0x0660 && c <= 0x0669) || // Arabic-Indic digits
           (c >= 0x06F0 && c <= 0x06F9) || // Extended Arabic-Indic
           (c >= 0x0966 && c <= 0x096F) || // Devanagari
           (c >= 0xFF10 && c <= 0xFF19);   // Fullwidth digits
}

static bool is_utf8_letter(const char*& p, const char* end) {
    if (p >= end) {
        return false;
    }
    unsigned char c = static_cast<unsigned char>(*p);
    if (c < 0x80) {
        char32_t ch = static_cast<char32_t>(c);
        if (is_unicode_letter(ch) || ch == '_') {
            ++p;
            return true;
        }
        return false;
    }
    int extra;
    char32_t code;
    if ((c & 0xE0) == 0xC0) {
        extra = 1;
        code = c & 0x1F;
    } else if ((c & 0xF0) == 0xE0) {
        extra = 2;
        code = c & 0x0F;
    } else if ((c & 0xF8) == 0xF0) {
        extra = 3;
        code = c & 0x07;
    } else {
        return false;
    }

    if (p + extra >= end) {
        return false;
    }
    for (int i = 0; i < extra; ++i) {
        ++p;
        if ((static_cast<unsigned char>(*p) & 0xC0) != 0x80) {
            return false;
        }
        code = (code << 6) | (static_cast<unsigned char>(*p) & 0x3F);
    }
    ++p;
    return is_unicode_letter(code);
}

static bool is_utf8_letter_or_digit(const char*& p, const char* end) {
    if (p >= end) {
        return false;
    }
    unsigned char c = static_cast<unsigned char>(*p);
    if (c < 0x80) {
        char32_t ch = static_cast<char32_t>(c);
        if (is_unicode_letter(ch) || ch == '_' || is_unicode_digit(ch)) {
            ++p;
            return true;
        }
        return false;
    }
    return is_utf8_letter(p, end);
}

static bool is_ascii_letter(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

static bool is_lower_letter(char c) {
    return (c >= 'a' && c <= 'z');
}

// ----------------------------------------------
// IdentName::dump
// ----------------------------------------------

std::string IdentName::dump(size_t indent) const {
    return detail::dumpQuotedName(kind(), text(), indent);
}

// ----------------------------------------------
// Тип имени
// ----------------------------------------------

bool IdentName::is_simple() const noexcept {
    return !text().empty() && !is_qualified() && !is_special() && !is_internal();
}

bool IdentName::is_qualified() const noexcept {
    if (text().empty() || is_special()) {
        return false;
    }
    return is_macro() || is_local() || is_static() || is_field() || is_module() || is_type() || is_native();
}

bool IdentName::is_special() const noexcept {
    return is_self() || is_parent() || is_args_dict() || is_last_result() || is_arg_ref();
}

bool IdentName::is_internal() const noexcept {
    if (text().empty()) {
        return false;
    }
    std::string_view s = text();
    char last = s.back();
    return last == '$' || last == ':' || last == '%';
}

// ----------------------------------------------
// Квалификаторные признаки
// ----------------------------------------------

bool IdentName::is_macro() const noexcept {
    return !text().empty() && text().front() == '@';
}

bool IdentName::is_local() const noexcept {
    std::string_view s = text();
    if (s.empty() || s.front() != '$') {
        return false;
    }
    if (s.size() == 1) {
        return true;
    }
    if (s.size() == 2 && (s[1] == '$' || s[1] == '0' || s[1] == '*' || s[1] == '^')) {
        return false;
    }
    if (s.size() >= 2 && s[1] >= '1' && s[1] <= '9') {
        return false;
    }
    return true;
}

bool IdentName::is_static() const noexcept {
    return text().find("::") != std::string_view::npos;
}

bool IdentName::is_field() const noexcept {
    return !text().empty() && text().front() == '.';
}

bool IdentName::is_module() const noexcept {
    return !text().empty() && text().front() == '\\';
}

bool IdentName::is_type() const noexcept {
    std::string_view s = text();
    if (s.empty() || s.front() != ':') {
        return false;
    }
    if (s.size() >= 2 && s[1] == ':') {
        return false;
    }
    return true;
}

bool IdentName::is_native() const noexcept {
    return !text().empty() && text().front() == '%';
}

bool IdentName::is_absolute_module() const noexcept {
    std::string_view s = text();
    return s.size() >= 2 && s[0] == '\\' && s[1] == '\\';
}

bool IdentName::is_relative_module() const noexcept {
    return is_module() && !is_absolute_module();
}

// ----------------------------------------------
// Специальные имена
// ----------------------------------------------

bool IdentName::is_self() const noexcept {
    return text() == "$0";
}

bool IdentName::is_parent() const noexcept {
    return text() == "$$";
}

bool IdentName::is_args_dict() const noexcept {
    return text() == "$*";
}

bool IdentName::is_last_result() const noexcept {
    return text() == "$^";
}

bool IdentName::is_arg_ref() const noexcept {
    std::string_view s = text();
    if (s.empty() || s.front() != '$' || s.size() < 2) {
        return false;
    }
    if (s[1] < '1' || s[1] > '9') {
        return false;
    }
    for (size_t i = 2; i < s.size(); ++i) {
        if (s[i] < '0' || s[i] > '9') {
            return false;
        }
    }
    return true;
}

// ----------------------------------------------
// Имя без квалификатора
// ----------------------------------------------

std::string_view IdentName::bare_name() const noexcept {
    std::string_view s = text();
    if (s.empty()) {
        return {};
    }

    size_t start = 0;
    size_t end = s.size();

    // Убрать '^' в конце, если есть
    if (end > 0 && s[end - 1] == '^') {
        --end;
    }

    while (start < end) {
        if (end - start >= 2 && s[start] == ':' && s[start + 1] == ':') {
            start += 2;
            continue;
        }
        char f = s[start];
        if (f == '@' || f == '$' || f == '.' || f == '\\' || f == ':' || f == '%') {
            ++start;
            continue;
        }
        break;
    }

    return s.substr(start, end - start);
}

// ----------------------------------------------
// Валидация
// ----------------------------------------------

bool IdentName::is_valid_simple_name(std::string_view s) noexcept {
    if (s.empty()) {
        return false;
    }
    if (s.size() > max_name_length) {
        return false;
    }

    const char* p = s.data();
    const char* end = p + s.size();

    if (p < end) {
        unsigned char c = static_cast<unsigned char>(*p);
        if (c < 0x80) {
            if (c >= '0' && c <= '9') {
                return false;
            }
            if (c != '_' && !is_ascii_letter(c)) {
                return false;
            }
            ++p;
        } else {
            if (!is_utf8_letter(p, end)) {
                return false;
            }
        }
    }

    while (p < end) {
        if (!is_utf8_letter_or_digit(p, end)) {
            return false;
        }
    }

    return true;
}

bool IdentName::is_valid_module_name(std::string_view s) noexcept {
    if (s.empty()) {
        return false;
    }
    if (s.size() > max_name_length) {
        return false;
    }

    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (!is_lower_letter(c) && c != '_' && !(c >= '0' && c <= '9')) {
            return false;
        }
    }

    if (s.front() == '_' || s.back() == '_') {
        return false;
    }

    return true;
}

// ----------------------------------------------
// Нормализация
// ----------------------------------------------

bool IdentName::is_normalized() const noexcept {
    // '^' (immutable marker) may only appear at the end
    if (!text().empty() && text().back() == '^') {
        return false;
    }
    if (is_macro()) {
        return false;
    }
    if (is_field()) {
        return false;
    }
    if (is_local()) {
        return false;
    }
    if (is_special()) {
        return false;
    }
    return true;
}

IdentName IdentName::normalized() const {
    std::string_view s = text();
    // '^' may only appear at the end
    if (!s.empty() && s.back() == '^') {
        s.remove_suffix(1);
    }
    IdentName result{std::string(s)};

    if (result.is_field()) {
        s = result.text();
        result = IdentName(std::string(s.substr(1)));
    }

    return result;
}

// ----------------------------------------------
// Внутреннее имя
// ----------------------------------------------

IdentName IdentName::to_internal() const {
    std::string_view s = text();
    // '^' may only appear at the end
    if (!s.empty() && s.back() == '^') {
        s.remove_suffix(1);
    }
    IdentName clean{std::string(s)};

    if (clean.is_internal()) {
        return clean;
    }

    if (clean.text().empty()) {
        return IdentName("::");
    }

    auto p = clean.parts();
    if (p.empty()) {
        return IdentName("::");
    }

    if (p.size() == 1) {
        std::string_view name = p[0];
        IdentName n{std::string(name)};

        if (n.is_type()) {
            return IdentName(std::string(n.bare_name()) + ":::");
        }
        if (n.is_native()) {
            return IdentName(std::string(n.bare_name()) + "%");
        }
        if (n.is_local()) {
            return IdentName(std::string(n.bare_name()) + "$");
        }
        if (n.is_static()) {
            return IdentName("::" + std::string(n.bare_name()) + "::");
        }
        return IdentName(std::string(n.bare_name()) + "::");
    }

    bool has_global = (p[0] == "::");

    bool has_pseudo_namespace = false;
    if (!has_global && p.size() >= 2) {
        IdentName first_part{std::string(p[0])};
        if (first_part.is_macro() || first_part.is_local() || first_part.is_field() || first_part.is_module() || first_part.is_type() ||
            first_part.is_native()) {
            has_pseudo_namespace = true;
        }
    }

    if (has_pseudo_namespace) {
        std::string combined;
        for (size_t i = 0; i < p.size(); ++i) {
            if (i > 0) {
                combined.append("::");
            }
            combined.append(p[i]);
        }
        IdentName combined_ident(std::move(combined));
        if (combined_ident.is_type()) {
            return IdentName(std::string(combined_ident.bare_name()) + ":::");
        }
        if (combined_ident.is_native()) {
            return IdentName(std::string(combined_ident.bare_name()) + "%");
        }
        if (combined_ident.is_local()) {
            return IdentName(std::string(combined_ident.bare_name()) + "$");
        }
        return IdentName(std::string(combined_ident.bare_name()) + "::");
    }

    std::string result;

    if (has_global) {
        result = "::";
    }

    size_t first = (has_global ? 1 : 0);
    size_t last_idx = p.size() - 1;

    std::string_view last_sv = p[last_idx];
    IdentName last_part{std::string(last_sv)};

    for (size_t i = first; i < last_idx; ++i) {
        result.append(p[i]);
        result.append("::");
    }

    if (last_part.is_type()) {
        result.append(last_part.bare_name());
        result.append(":::");
    } else if (last_part.is_native()) {
        result.append(last_part.bare_name());
        result.append("%");
    } else if (last_part.is_local()) {
        result.append(last_part.bare_name());
        result.append("$");
    } else {
        result.append(last_part.bare_name());
        result.append("::");
    }

    return IdentName(std::move(result));
}

// ----------------------------------------------
// Разбивка по ::
// ----------------------------------------------

std::vector<std::string_view> IdentName::parts() const {
    std::vector<std::string_view> result;
    std::string_view s = text();
    if (s.empty()) {
        return result;
    }

    if (s.size() >= 2 && s[0] == ':' && s[1] == ':') {
        result.emplace_back("::");
        s.remove_prefix(2);
    }

    size_t pos = 0;
    while (pos < s.size()) {
        size_t sep = s.find("::", pos);
        if (sep == std::string_view::npos) {
            if (pos < s.size()) {
                result.emplace_back(s.substr(pos));
            }
            break;
        }
        if (sep + 2 < s.size() && s[sep + 2] == ':') {
            result.emplace_back(s.substr(pos, sep + 3 - pos));
            pos = sep + 3;
            continue;
        }
        if (sep > pos) {
            result.emplace_back(s.substr(pos, sep - pos));
        }
        pos = sep + 2;
    }

    return result;
}

// ----------------------------------------------
// Манглинг
// ----------------------------------------------

IdentName IdentName::mangle(std::string_view module_name) const {
    std::string_view s = text();
    std::string result;

    if (module_name.empty()) {
        result = "_$$_";
    } else {
        std::string_view mod = module_name;
        if (!mod.empty() && mod.front() == '\\') {
            mod.remove_prefix(1);
        }
        result.reserve(mod.size() + 5);
        result.push_back('_');
        result.push_back('$');
        for (char c : mod) {
            if (c == '\\') {
                result.push_back('$');
            } else {
                result.push_back(c);
            }
        }
        result.push_back('$');
        result.push_back('_');
    }

    size_t i = 0;
    while (i < s.size()) {
        if (i + 2 < s.size() && s[i] == ':' && s[i + 1] == ':' && s[i + 2] == ':') {
            result.append("$$$");
            i += 3;
        } else if (s[i] == ':') {
            result.push_back('$');
            i += 1;
        } else if (s[i] == '\\') {
            result.push_back('$');
            i += 1;
        } else {
            result.push_back(s[i]);
            i += 1;
        }
    }

    return IdentName(std::move(result));
}

IdentName IdentName::demangle(std::string_view mangled) {
    std::string_view src = mangled;

    if (src.size() < 4 || src[0] != '_' || src[1] != '$') {
        return IdentName(std::string(src));
    }

    size_t prefix_end = std::string_view::npos;
    if (src.size() >= 4 && src[2] == '$' && src[3] == '_') {
        prefix_end = 4;
    } else {
        size_t pos = src.rfind("$_");
        if (pos != std::string_view::npos && pos >= 2 && pos + 2 <= src.size()) {
            prefix_end = pos + 2;
        }
    }

    if (prefix_end == std::string_view::npos) {
        return IdentName(std::string(src));
    }

    std::string_view body = src.substr(prefix_end);

    std::string result;
    result.reserve(body.size());
    size_t i = 0;
    while (i < body.size()) {
        if (i + 2 < body.size() && body[i] == '$' && body[i + 1] == '$' && body[i + 2] == '$') {
            result.append(":::");
            i += 3;
        } else if (body[i] == '$') {
            result.push_back(':');
            i += 1;
        } else {
            result.push_back(body[i]);
            i += 1;
        }
    }

    return IdentName(std::move(result));
}

// ----------------------------------------------
// Преобразование имени модуля в файловый путь
// ----------------------------------------------

std::filesystem::path IdentName::module_name_to_path(std::string_view module_name, const std::filesystem::path& base_dir,
                                                     const std::filesystem::path& sys_dir) {
    if (module_name.empty()) {
        return {};
    }

    if (module_name.size() >= 3 && module_name[0] == '\\' && module_name[1] == '\\' && module_name[2] == '\\') {
        std::string_view rest = module_name.substr(3);
        if (rest.empty()) {
            return std::filesystem::absolute(sys_dir);
        }
        std::filesystem::path result = sys_dir;
        size_t pos = 0;
        while (pos < rest.size()) {
            size_t sep = rest.find('\\', pos);
            std::string_view comp;
            if (sep == std::string_view::npos) {
                comp = rest.substr(pos);
                pos = rest.size();
            } else {
                comp = rest.substr(pos, sep - pos);
                pos = sep + 1;
            }
            if (!comp.empty()) {
                result /= comp;
            }
        }
        return std::filesystem::absolute(result);
    }

    if (module_name.size() >= 2 && module_name[0] == '\\' && module_name[1] == '\\') {
        std::string_view rest = module_name.substr(2);
        if (rest.empty()) {
            return std::filesystem::path("/");
        }
        std::filesystem::path result = std::filesystem::path("/");
        size_t pos = 0;
        while (pos < rest.size()) {
            size_t sep = rest.find('\\', pos);
            std::string_view comp;
            if (sep == std::string_view::npos) {
                comp = rest.substr(pos);
                pos = rest.size();
            } else {
                comp = rest.substr(pos, sep - pos);
                pos = sep + 1;
            }
            if (!comp.empty()) {
                result /= comp;
            }
        }
        return result.lexically_normal();
    }

    if (!module_name.empty() && module_name[0] == '\\') {
        std::string_view rest = module_name.substr(1);
        if (rest.empty()) {
            return std::filesystem::absolute(base_dir);
        }
        std::filesystem::path result = base_dir;
        size_t pos = 0;
        while (pos < rest.size()) {
            size_t sep = rest.find('\\', pos);
            std::string_view comp;
            if (sep == std::string_view::npos) {
                comp = rest.substr(pos);
                pos = rest.size();
            } else {
                comp = rest.substr(pos, sep - pos);
                pos = sep + 1;
            }
            if (!comp.empty()) {
                result /= comp;
            }
        }
        return std::filesystem::absolute(result);
    }

    FAULT("Invalid module name: '{}'", module_name);
    return {};
}

IdentName IdentName::path_to_module_name(const std::filesystem::path& path, const std::filesystem::path& base_dir) {
    if (path.empty()) {
        return IdentName();
    }

    std::filesystem::path abs_path = std::filesystem::absolute(path);
    std::filesystem::path abs_base = std::filesystem::absolute(base_dir);

    std::filesystem::path rel = abs_path.lexically_relative(abs_base);

    if (!rel.empty() && !rel.native().starts_with("..")) {
        std::string result;
        result.push_back('\\');
        bool first = true;
        for (const auto& component : rel) {
            if (first) {
                first = false;
            } else {
                result.push_back('\\');
            }
            std::string comp_str = component.generic_string();
            if (!IdentName::is_valid_module_name(comp_str)) {
                FAULT("Invalid module name component: '{}'", comp_str);
                return IdentName();
            }
            result.append(comp_str);
        }
        return IdentName(std::move(result));
    }

    std::filesystem::path root = abs_path.root_path();
    std::filesystem::path rel_to_root = abs_path.lexically_proximate(root);
    std::string result;
    result.append("\\\\");
    bool first = true;
    for (const auto& component : rel_to_root) {
        if (!first) {
            result.push_back('\\');
        }
        first = false;
        std::string comp_str = component.generic_string();
        if (!IdentName::is_valid_module_name(comp_str)) {
            FAULT("Invalid module name component: '{}'", comp_str);
            return IdentName();
        }
        result.append(comp_str);
    }
    return IdentName(std::move(result));
}

// ----------------------------------------------
// handleImmutable - обработка '^' в конце имени
// ----------------------------------------------

void IdentName::stripCaretAndApplyReadonly(AttrPool* pool) {
    if (m_text.empty() || m_text.back() != '^') {
        return;
    }
    if (is_special()) {
        return;
    }
    // Единый хелпер иммутабельности ('^' → attr::ReadOnly), тот же, что использует
    // конвертер Term→AST (convertAttrsToNode): kind=Ident допускает квалификатор.
    EXPECT(pool && "stripCaretAndApplyReadonly requires AttrPool");
    applyReadonlyFromCaret(*this, m_text, pool);
    while (!m_text.empty() && m_text.back() == '^') {
        m_text.pop_back();
    }
}

bool IdentName::expandQualified(std::string_view namespace_path) {
    if (m_text.rfind("@::", 0) != 0) {
        return false;
    }
    const std::string rest = m_text.substr(3);
    m_text = namespace_path.empty() ? rest : std::string(namespace_path) + "::" + rest;
    return true;
}

} // namespace trust
