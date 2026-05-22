// src/parser/naming.cpp
// Реализация Ident — обработка идентификаторов по правилам NAMING.md

#include "ast/naming.hpp"
#include "utils/error.hpp"

#include <cctype>
#include <algorithm>
#include <filesystem>

namespace trust {

// ──────────────────────────────────────────────
// Вспомогательные утилиты
// ──────────────────────────────────────────────

static bool is_unicode_letter(char32_t c) {
    // Базовое определение Unicode-буквы: проверяем основные диапазоны
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

// Простейший UTF-8 декодер (только для проверки идентификаторов)
static bool is_utf8_letter(const char*& p, const char* end) {
    if (p >= end)
        return false;
    unsigned char c = static_cast<unsigned char>(*p);
    if (c < 0x80) {
        char32_t ch = static_cast<char32_t>(c);
        // ASCII буква или _
        if (is_unicode_letter(ch) || ch == '_') {
            ++p;
            return true;
        }
        return false;
    }
    // Многобайтовые последовательности
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
    } else
        return false;

    if (p + extra >= end)
        return false;
    for (int i = 0; i < extra; ++i) {
        ++p;
        if ((static_cast<unsigned char>(*p) & 0xC0) != 0x80)
            return false;
        code = (code << 6) | (static_cast<unsigned char>(*p) & 0x3F);
    }
    ++p;
    return is_unicode_letter(code);
}

static bool is_utf8_letter_or_digit(const char*& p, const char* end) {
    if (p >= end)
        return false;
    unsigned char c = static_cast<unsigned char>(*p);
    if (c < 0x80) {
        char32_t ch = static_cast<char32_t>(c);
        if (is_unicode_letter(ch) || ch == '_' || is_unicode_digit(ch)) {
            ++p;
            return true;
        }
        return false;
    }
    // Для многобайтовых — проверяем, является ли буквой
    return is_utf8_letter(p, end);
}

static bool is_ascii_letter(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

static bool is_lower_letter(char c) {
    return (c >= 'a' && c <= 'z');
}

bool Ident::is_simple() const noexcept {
    return !empty() && !is_qualified() && !is_special() && !is_internal();
}
bool Ident::is_qualified() const noexcept {
    if (empty() || is_special())
        return false;
    return is_macro() || is_temp() || is_static() || is_field() || is_module() || is_type() || is_native();
}
bool Ident::is_special() const noexcept {
    return is_self() || is_parent() || is_args_dict() || is_last_result() || is_arg_ref();
}
bool Ident::is_internal() const noexcept {
    if (empty())
        return false;
    char last = back();
    return last == '$' || last == ':' || last == '%';
}

// ──────────────────────────────────────────────
// Квалификаторные признаки
// ──────────────────────────────────────────────

bool Ident::is_macro() const noexcept {
    return !empty() && front() == '@';
}

bool Ident::is_temp() const noexcept {
    // $temp — начинается с $, но не $$, не $0, не $1..$N, не $*, не $^
    if (empty() || front() != '$')
        return false;
    // Одиночный $ — временная переменная (пустое имя после $)
    if (size() == 1)
        return true;
    // $$ — parent, $0 — self, $* — args_dict, $^ — last_result, $1..$N — arg_ref
    if (size() == 2 && (at(1) == '$' || at(1) == '0' || at(1) == '*' || at(1) == '^'))
        return false;
    if (size() >= 2 && at(1) >= '1' && at(1) <= '9')
        return false;
    return true;
}

bool Ident::is_static() const noexcept {
    return find("::") != npos;
}

bool Ident::is_field() const noexcept {
    return !empty() && front() == '.';
}

bool Ident::is_module() const noexcept {
    return !empty() && front() == '\\';
}

bool Ident::is_type() const noexcept {
    // :Type — начинается с :, но не :: и не :::
    if (empty() || front() != ':')
        return false;
    if (size() >= 2 && at(1) == ':')
        return false; // :: — это namespace, не тип
    return true;
}

bool Ident::is_native() const noexcept {
    return !empty() && front() == '%';
}

bool Ident::is_absolute_module() const noexcept {
    return size() >= 2 && at(0) == '\\' && at(1) == '\\';
}

bool Ident::is_relative_module() const noexcept {
    return is_module() && !is_absolute_module();
}

// ──────────────────────────────────────────────
// Специальные имена
// ──────────────────────────────────────────────

bool Ident::is_self() const noexcept {
    return *this == "$0";
}
bool Ident::is_parent() const noexcept {
    return *this == "$$";
}
bool Ident::is_args_dict() const noexcept {
    return *this == "$*";
}
bool Ident::is_last_result() const noexcept {
    return *this == "$^";
}

bool Ident::is_arg_ref() const noexcept {
    // $1..$N
    if (empty() || front() != '$' || size() < 2)
        return false;
    if (at(1) < '1' || at(1) > '9')
        return false;
    // остальные символы — только цифры
    for (size_t i = 2; i < size(); ++i) {
        if (at(i) < '0' || at(i) > '9')
            return false;
    }
    return true;
}

// ──────────────────────────────────────────────
// Имя без квалификатора
// ──────────────────────────────────────────────

std::string_view Ident::bare_name() const noexcept {
    if (empty())
        return {};

    size_t start = 0;
    size_t end = size();

    // Убрать '^' в конце, если есть
    if (end > 0 && at(end - 1) == '^') {
        --end;
    }

    // Циклически пропускаем последовательность квалификаторов и :: между ними.
    // Например: @:: :type → пропускаем @, ::, : → остаётся type
    while (start < end) {
        // Пропустить ведущую '::' (глобальное пространство имён или после квалификатора)
        if (end - start >= 2 && at(start) == ':' && at(start + 1) == ':') {
            start += 2;
            continue;
        }
        // Пропустить ведущий одиночный квалификатор
        char f = at(start);
        if (f == '@' || f == '$' || f == '.' || f == '\\' || f == ':' || f == '%') {
            ++start;
            continue;
        }
        // Не квалификатор и не :: — конец пропуска
        break;
    }

    return std::string_view(data() + start, end - start);
}

// ──────────────────────────────────────────────
// Иммутабельность
// ──────────────────────────────────────────────

bool Ident::has_immutable() const noexcept {
    return find('^') != npos;
}

Ident Ident::without_immutable() const {
    std::string result;
    result.reserve(size());
    for (char c : *this) {
        if (c != '^') {
            result.push_back(c);
        }
    }
    return result;
}

// ──────────────────────────────────────────────
// Валидация
// ──────────────────────────────────────────────

bool Ident::is_valid_simple_name(std::string_view s) noexcept {
    if (s.empty())
        return false;
    if (s.size() > max_name_length)
        return false;

    const char* p = s.data();
    const char* end = p + s.size();

    // Первый символ — не цифра (ASCII) и не Unicode-цифра
    if (p < end) {
        unsigned char c = static_cast<unsigned char>(*p);
        if (c < 0x80) {
            if (c >= '0' && c <= '9')
                return false;
            if (c != '_' && !is_ascii_letter(c))
                return false;
            ++p;
        } else {
            if (!is_utf8_letter(p, end))
                return false;
        }
    }

    // Остальные символы — буквы, цифры, _
    while (p < end) {
        if (!is_utf8_letter_or_digit(p, end))
            return false;
    }

    return true;
}

bool Ident::is_valid_module_name(std::string_view s) noexcept {
    if (s.empty())
        return false;
    if (s.size() > max_name_length)
        return false;

    // Только строчные буквы, цифры и _
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (!is_lower_letter(c) && c != '_' && !(c >= '0' && c <= '9')) {
            return false;
        }
    }

    // _ не может быть первым или последним
    if (s.front() == '_' || s.back() == '_')
        return false;

    return true;
}

// ──────────────────────────────────────────────
// Нормализация
// ──────────────────────────────────────────────

bool Ident::is_normalized() const noexcept {
    // Нормализованное имя не содержит ^ и не содержит нераскрытых @-макросов
    if (has_immutable())
        return false;
    // Проверка на наличие @ не как части bare_name,
    // а как квалификатора макроса (т.е. @ в начале)
    if (is_macro())
        return false;
    // Также проверяем, что '.' (field) удалён
    if (is_field())
        return false;
    // Временные имена ($var) и специальные имена ($0, $$, $*, $^, $1..$N) не нормализованы
    if (is_temp())
        return false;
    if (is_special())
        return false;
    return true;
}

Ident Ident::normalized() const {
    Ident result = without_immutable();

    // Убрать ведущую точку (field квалификатор)
    if (result.is_field()) {
        result = result.substr(1);
    }

    // Если начинается с @ — это макрос, который требует раскрытия в AST
    // Для целей данного класса просто возвращаем как есть
    // (т.к. полная нормализация требует контекста AST)

    return result;
}

// ──────────────────────────────────────────────
// Внутреннее имя
// ──────────────────────────────────────────────

Ident Ident::to_internal() const {
    // Удаляем символ иммутабельности ^ (он не входит во внутреннее имя — NAMING.md п.77)
    Ident clean = without_immutable();

    // Если уже внутреннее — возвращаем как есть
    if (clean.is_internal())
        return clean;

    // Пустое имя → ::
    if (clean.empty())
        return Ident("::");

    // Разбираем имя на части по ::
    auto p = clean.parts();
    if (p.empty())
        return Ident("::");

    // Если нет :: — это простой идентификатор с одним квалификатором
    // Обрабатываем напрямую
    if (p.size() == 1) {
        std::string_view name = p[0]; // может быть "@macro", "$temp", ":Type", "%native", ".field", "var" и т.д.
        Ident n(name);

        if (n.is_type()) {
            // :Type → Type:::
            return Ident(std::string(n.bare_name()) + ":::");
        }
        if (n.is_native()) {
            // %func → func%
            return Ident(std::string(n.bare_name()) + "%");
        }
        if (n.is_temp()) {
            // $var → var$
            return Ident(std::string(n.bare_name()) + "$");
        }
        if (n.is_static()) {
            // ::var → ::var:: (глобальное)
            return Ident("::" + std::string(n.bare_name()) + "::");
        }
        // Простое имя → name::
        return Ident(std::string(n.bare_name()) + "::");
    }

    // Много частей (ns::var, ::ns::var, ns::%func, @::var и т.д.)
    // Первая часть может быть "::" (глобальный префикс)
    bool has_global = (p[0] == "::");

    // Если первая часть — это не "::", но содержит :: или является квалификатором,
    // который не должен быть namespace (например, @), то это не настоящий namespace.
    // В таком случае объединяем все части и обрабатываем как одно имя.
    bool has_pseudo_namespace = false;
    if (!has_global && p.size() >= 2) {
        // Проверяем первую часть: если она является квалификатором (@, $, ., \, :, %)
        // а не реальным именем, то это псевдо-namespace
        Ident first_part(p[0]);
        if (first_part.is_macro() || first_part.is_temp() || first_part.is_field() || first_part.is_module() || first_part.is_type() ||
            first_part.is_native()) {
            has_pseudo_namespace = true;
        }
    }

    if (has_pseudo_namespace) {
        // Собираем всё в одно имя и применяем to_internal к нему (без рекурсии)
        std::string combined;
        for (size_t i = 0; i < p.size(); ++i) {
            if (i > 0)
                combined.append("::");
            combined.append(p[i]);
        }
        Ident combined_ident(combined);
        // Обрабатываем как одно имя с квалификаторами
        if (combined_ident.is_type()) {
            return Ident(std::string(combined_ident.bare_name()) + ":::");
        }
        if (combined_ident.is_native()) {
            return Ident(std::string(combined_ident.bare_name()) + "%");
        }
        if (combined_ident.is_temp()) {
            return Ident(std::string(combined_ident.bare_name()) + "$");
        }
        return Ident(std::string(combined_ident.bare_name()) + "::");
    }

    std::string result;

    if (has_global) {
        result = "::";
    }

    // Идём по частям: все кроме последней — namespace
    size_t first = (has_global ? 1 : 0);
    size_t last_idx = p.size() - 1;

    // Проверяем последнюю часть на квалификаторы
    std::string_view last_sv = p[last_idx];
    Ident last_part(last_sv);

    // Формируем namespace-префикс (все части кроме последней)
    for (size_t i = first; i < last_idx; ++i) {
        result.append(p[i]);
        result.append("::");
    }

    // Обрабатываем последнюю часть
    if (last_part.is_type()) {
        result.append(last_part.bare_name());
        result.append(":::");
    } else if (last_part.is_native()) {
        result.append(last_part.bare_name());
        result.append("%");
    } else if (last_part.is_temp()) {
        result.append(last_part.bare_name());
        result.append("$");
    } else {
        // var → var:: (все остальные случаи)
        result.append(last_part.bare_name());
        result.append("::");
    }

    return result;
}

// ──────────────────────────────────────────────
// Разбивка по ::
// ──────────────────────────────────────────────

std::vector<std::string_view> Ident::parts() const {
    std::vector<std::string_view> result;
    if (empty())
        return result;

    std::string_view s = *this;

    // Обработка ведущего ::
    if (s.size() >= 2 && s[0] == ':' && s[1] == ':') {
        result.emplace_back("::");
        s.remove_prefix(2);
    }

    // Разбивка по :: (но не по ::: — это тип, разбиваем только ::)
    // :: встречается парно, но не тройное :::
    size_t pos = 0;
    while (pos < s.size()) {
        // Ищем ::
        size_t sep = s.find("::", pos);
        if (sep == npos) {
            // Последний фрагмент — только если не пустой
            if (pos < s.size()) {
                result.emplace_back(s.substr(pos));
            }
            break;
        }
        // Проверяем, что это не :::
        if (sep + 2 < s.size() && s[sep + 2] == ':') {
            // Это ::: — не разделитель, ищем дальше
            result.emplace_back(s.substr(pos, sep + 3 - pos));
            pos = sep + 3;
            continue;
        }
        // Это :: — разделитель
        if (sep > pos) {
            result.emplace_back(s.substr(pos, sep - pos));
        }
        // Если sep == pos, значит пустой фрагмент между ::
        // (например, trailing :: после имени) — не добавляем его
        pos = sep + 2;
    }

    return result;
}

// ──────────────────────────────────────────────
// Манглинг
// ──────────────────────────────────────────────

Ident Ident::mangle(std::string_view module_name) const {
    // Манглинг: : → $, ::: → $$$
    std::string result;

    // Префикс модуля
    if (module_name.empty()) {
        // Главный модуль: префикс _$$
        result = "_$$_";
    } else {
        // Обычный модуль: _$module_name$_
        // Убираем ведущий \ (квалификатор модуля)
        std::string_view mod = module_name;
        if (!mod.empty() && mod.front() == '\\') {
            mod.remove_prefix(1);
        }
        result.reserve(mod.size() + 5);
        result.push_back('_');
        result.push_back('$');
        for (char c : mod) {
            if (c == '\\') {
                result.push_back('$'); // \ → $
            } else {
                result.push_back(c);
            }
        }
        result.push_back('$');
        result.push_back('_');
    }

    // Манглинг внутреннего имени (без дополнительного разделителя)
    size_t i = 0;
    while (i < size()) {
        if (i + 2 < size() && at(i) == ':' && at(i + 1) == ':' && at(i + 2) == ':') {
            // ::: → $$$
            result.append("$$$");
            i += 3;
        } else if (at(i) == ':') {
            // : → $
            result.push_back('$');
            i += 1;
        } else if (at(i) == '\\') {
            // \ → $
            result.push_back('$');
            i += 1;
        } else {
            result.push_back(at(i));
            i += 1;
        }
    }

    return result;
}

Ident Ident::demangle(std::string_view mangled) {
    // Деманглинг: обратный процесс
    // Убираем префикс _$$_ (главный) или _$...$_ (именованный)
    std::string_view src = mangled;

    if (src.size() < 4 || src[0] != '_' || src[1] != '$')
        return Ident(src);

    size_t prefix_end = npos;
    if (src.size() >= 4 && src[2] == '$' && src[3] == '_') {
        // Главный модуль: _$$_
        prefix_end = 4;
    } else {
        // Именованный модуль: _$...$_
        // Ищем последнее вхождение $_ (конец префикса)
        size_t pos = src.rfind("$_");
        if (pos != npos && pos >= 2 && pos + 2 <= src.size()) {
            prefix_end = pos + 2;
        }
    }

    if (prefix_end == npos)
        return Ident(src);

    std::string_view body = src.substr(prefix_end);

    // $$$ → :::, $ → :
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

    return result;
}

// ──────────────────────────────────────────────
// Преобразование имени модуля в файловый путь
// ──────────────────────────────────────────────

std::filesystem::path Ident::module_name_to_path(std::string_view module_name, const std::filesystem::path& base_dir, const std::filesystem::path& sys_dir) {
    if (module_name.empty())
        return {};

    // Системный модуль: \\\... — три обратных слеша
    if (module_name.size() >= 3 && module_name[0] == '\\' && module_name[1] == '\\' && module_name[2] == '\\') {
        std::string_view rest = module_name.substr(3);
        if (rest.empty())
            return std::filesystem::absolute(sys_dir);
        // Разбиваем по \ и собираем path
        std::filesystem::path result = sys_dir;
        size_t pos = 0;
        while (pos < rest.size()) {
            size_t sep = rest.find('\\', pos);
            std::string_view comp;
            if (sep == npos) {
                comp = rest.substr(pos);
                pos = rest.size();
            } else {
                comp = rest.substr(pos, sep - pos);
                pos = sep + 1;
            }
            if (!comp.empty())
                result /= comp;
        }
        return std::filesystem::absolute(result);
    }

    // Абсолютный модуль: \\... — два обратных слеша
    if (module_name.size() >= 2 && module_name[0] == '\\' && module_name[1] == '\\') {
        std::string_view rest = module_name.substr(2);
        if (rest.empty())
            return std::filesystem::path("/");
        std::filesystem::path result = std::filesystem::path("/");
        size_t pos = 0;
        while (pos < rest.size()) {
            size_t sep = rest.find('\\', pos);
            std::string_view comp;
            if (sep == npos) {
                comp = rest.substr(pos);
                pos = rest.size();
            } else {
                comp = rest.substr(pos, sep - pos);
                pos = sep + 1;
            }
            if (!comp.empty())
                result /= comp;
        }
        return result.lexically_normal();
    }

    // Относительный модуль: \... — один обратный слеш
    if (!module_name.empty() && module_name[0] == '\\') {
        std::string_view rest = module_name.substr(1);
        if (rest.empty())
            return std::filesystem::absolute(base_dir);
        std::filesystem::path result = base_dir;
        size_t pos = 0;
        while (pos < rest.size()) {
            size_t sep = rest.find('\\', pos);
            std::string_view comp;
            if (sep == npos) {
                comp = rest.substr(pos);
                pos = rest.size();
            } else {
                comp = rest.substr(pos, sep - pos);
                pos = sep + 1;
            }
            if (!comp.empty())
                result /= comp;
        }
        return std::filesystem::absolute(result);
    }

    // Невалидное имя модуля — не начинается с \
    FAULT("Invalid module name: '{}'", module_name);
    return {};
}

// ──────────────────────────────────────────────
// Преобразование файлового пути в имя модуля
// ──────────────────────────────────────────────

Ident Ident::path_to_module_name(const std::filesystem::path& path, const std::filesystem::path& base_dir) {
    if (path.empty())
        return Ident();

    std::filesystem::path abs_path = std::filesystem::absolute(path);
    std::filesystem::path abs_base = std::filesystem::absolute(base_dir);

    // Пытаемся получить относительный путь от base_dir
    std::filesystem::path rel = abs_path.lexically_relative(abs_base);

    if (!rel.empty() && !rel.native().starts_with("..")) {
        // Успешно: путь внутри base_dir → относительный модуль
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
            // Проверка на валидность имени компонента
            if (!Ident::is_valid_module_name(comp_str)) {
                FAULT("Invalid module name component: '{}'", comp_str);
                return Ident();
            }
            result.append(comp_str);
        }
        return Ident(result);
    }

    // Вне base_dir → абсолютный модуль
    // Используем lexically_proximate от корня ФС, чтобы получить путь без корневого "/"
    std::filesystem::path root = abs_path.root_path(); // e.g. "/" на POSIX
    std::filesystem::path rel_to_root = abs_path.lexically_proximate(root);
    std::string result;
    result.append("\\\\");
    bool first = true;
    for (const auto& component : rel_to_root) {
        if (!first)
            result.push_back('\\');
        first = false;
        std::string comp_str = component.generic_string();
        if (!Ident::is_valid_module_name(comp_str)) {
            FAULT("Invalid module name component: '{}'", comp_str);
            return Ident();
        }
        result.append(comp_str);
    }
    return Ident(result);
}

// ──────────────────────────────────────────────
// special() — проверка специальных имён
// ──────────────────────────────────────────────

} // namespace trust
