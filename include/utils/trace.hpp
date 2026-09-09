#pragma once

// include/utils/trace.hpp
// Ядро отладочного вывода компилятора (УРОВЕНЬ 2: C++ компилятора - источник данных).
//
// Назначение: дешёвый способ печатать отладочную информацию (произвольные сообщения) с
// фильтрацией по ключевым словам/маскам. Управление фильтром задаётся из языка
// (УРОВЕНЬ 1: системные макросы TrustLang `@__DEBUG__`/`@__DEBUG_SCOPE__` в .src) через
// trace::setFilter(); сами сообщения - макрос TRUST_DEBUG в C++ коде компилятора.
//
// Ключевые свойства:
//   - при выключенной (release) сборке макросы ВЫРЕЗАЮТСЯ (аргументы не вычисляются, код не
//     генерируется): вызовы допустимы и становятся no-op;
//   - фильтрация ОБЯЗАТЕЛЬНА: без заданного фильтра вывод пуст (enabled() == false);
//   - авто-теги из __FILE__ (root/path/component/rel/stem/file) + явные теги вызова дают
//     независимые критерии фильтрации по маскам (`*`, `?`, списки через ',').
//
// Один TU-локальный `static const Site` на call-site: разбор __FILE__ выполняется один раз.

#include "utils/io.hpp"
#include "utils/strings.hpp"

#include <cstddef>
#include <format>
#include <ostream>
#include <string>
#include <string_view>

// Гейт сборки: задаётся CMake (TRUST_TRACE_ENABLED=1 в dev, 0 в release).
// Дефолт 0 - безопасный (нет отладочных сообщений), если заголовок включён вне CMake-сборки.
#ifndef TRUST_TRACE_ENABLED
#define TRUST_TRACE_ENABLED 0
#endif

namespace trust::trace {

inline constexpr std::size_t npos = std::string_view::npos;

/// Обрезка пробельных символов с обеих сторон (для элементов списка масок).
constexpr std::string_view trimView(std::string_view v) noexcept {
    constexpr std::string_view ws = " \t\n\r";
    while (!v.empty() && ws.find(v.front()) != npos) {
        v.remove_prefix(1);
    }
    while (!v.empty() && ws.find(v.back()) != npos) {
        v.remove_suffix(1);
    }
    return v;
}

/// Glob-сравнение: `*` - любая последовательность, `?` - один символ.
constexpr bool globMatch(std::string_view pat, std::string_view str) noexcept {
    std::size_t p = 0;
    std::size_t s = 0;
    std::size_t star = npos;
    std::size_t starMatch = 0;
    while (s < str.size()) {
        const bool literal = (p < pat.size()) && (pat[p] == '?' || pat[p] == str[s]);
        if (literal) {
            ++p;
            ++s;
        } else if (p < pat.size() && pat[p] == '*') {
            star = p;
            ++p;
            starMatch = s;
        } else if (star != npos) {
            p = star + 1;
            s = ++starMatch;
        } else {
            return false;
        }
    }
    while (p < pat.size() && pat[p] == '*') {
        ++p;
    }
    return p == pat.size();
}

/// Список масок (через ',') против одного токена: true, если совпала хотя бы одна маска.
inline bool maskListMatches(std::string_view masks, std::string_view token) noexcept {
    std::size_t start = 0;
    while (start <= masks.size()) {
        const std::size_t comma = masks.find(',', start);
        const std::string_view mask = trimView(masks.substr(start, comma == npos ? npos : comma - start));
        if (!mask.empty() && globMatch(mask, token)) {
            return true;
        }
        if (comma == npos) {
            break;
        }
        start = comma + 1;
    }
    return false;
}

/// Разбор `__FILE__` в набор токенов фильтрации. Пути приводятся к ОТНОСИТЕЛЬНЫМ от корня
/// проекта (по якорям `/src/`, `/include/`, `/test/`, `/examples/`); абсолютный путь не хранится.
/// Токены: root (первый сегмент), path (корневой относительный путь), component (каталог-родитель),
/// rel (`component/file`), stem (файл без расширения), file (файл с расширением).
struct Site {
    std::string_view path{};
    std::string_view rel{};
    std::string_view root{};
    std::string_view component{};
    std::string_view stem{};
    std::string_view file{};

    constexpr Site() = default;
    explicit constexpr Site(std::string_view sourcePath) { parse(sourcePath); }

    constexpr void parse(std::string_view f) {
        std::size_t start = 0;
        bool anchored = false;
        // Относительный путь, начинающийся сразу с корневой зоны проекта (`src/…`, `test/…`).
        constexpr std::string_view prefixAnchors[] = {"src/", "include/", "test/", "examples/"};
        for (std::string_view a : prefixAnchors) {
            if (f.starts_with(a)) {
                anchored = true;
                break;
            }
        }
        if (!anchored) {
            constexpr std::string_view anchors[] = {"/src/", "/include/", "/test/", "/examples/"};
            std::size_t anchor = npos;
            for (std::string_view a : anchors) {
                const std::size_t p = f.rfind(a);
                if (p != npos && (anchor == npos || p > anchor)) {
                    anchor = p;
                }
            }
            if (anchor != npos) {
                start = anchor + 1; // отбросить ведущий '/'
                anchored = true;
            }
        }
        if (!anchored) {
            // Нет якоря (генерируемые/внешние/голые пути): последние два сегмента `<dir>/<file>`.
            const std::size_t last = f.rfind('/');
            if (last != npos && last > 0) {
                const std::size_t prev = f.rfind('/', last - 1);
                start = (prev == npos) ? 0 : prev + 1;
            }
        }
        path = f.substr(start);

        const std::size_t firstSlash = path.find('/');
        root = (firstSlash == npos) ? std::string_view{} : path.substr(0, firstSlash);

        const std::size_t ps = path.rfind('/');
        file = (ps == npos) ? path : path.substr(ps + 1);
        const std::size_t dot = file.rfind('.');
        stem = (dot == npos || dot == 0) ? file : file.substr(0, dot);

        if (ps == npos) {
            component = root;
            rel = path;
            return;
        }
        std::size_t compStart = 0;
        if (ps > 0) {
            const std::size_t prev = path.rfind('/', ps - 1);
            compStart = (prev == npos) ? 0 : prev + 1;
        }
        component = path.substr(compStart, ps - compStart);
        rel = path.substr(compStart);
    }
};

/// Хранилище фильтра (маски через ','). Пустая строка - вывод выключен.
/// Компиляция однопоточная; LSP использует этот механизм только на dev-сборках.
inline std::string& filterStorage() {
    static std::string masks;
    return masks;
}

/// Вывод включён? (задан непустой фильтр)
inline bool enabled() noexcept {
    return !filterStorage().empty();
}

/// Текущий фильтр (маски через ',').
inline const std::string& filter() {
    return filterStorage();
}

/// Задать фильтр (пустая строка - выключить вывод).
inline void setFilter(std::string_view masks) {
    filterStorage().assign(masks.data(), masks.size());
}

/// Сбросить фильтр (выключить вывод).
inline void clearFilter() {
    filterStorage().clear();
}

/// Поток отладочного вывода (stderr - не смешивается со stdout программы).
inline std::ostream& out() {
    return trust::errs();
}

/// Подходит ли сообщение под фильтр: любой из авто-токенов Site ИЛИ явных тегов попадает
/// под любую маску. Без фильтра всегда false (фильтрация обязательна).
inline bool match(const Site& site, std::string_view tags) noexcept {
    const std::string& masks = filterStorage();
    if (masks.empty()) {
        return false;
    }
    const std::string_view tokens[] = {site.root, site.path, site.component, site.rel, site.stem, site.file};
    for (const std::string_view t : tokens) {
        if (!t.empty() && maskListMatches(masks, t)) {
            return true;
        }
    }
    std::size_t start = 0;
    while (start <= tags.size()) {
        const std::size_t comma = tags.find(',', start);
        const std::string_view tag = trimView(tags.substr(start, comma == npos ? npos : comma - start));
        if (!tag.empty() && maskListMatches(masks, tag)) {
            return true;
        }
        if (comma == npos) {
            break;
        }
        start = comma + 1;
    }
    return false;
}

} // namespace trust::trace

#if TRUST_TRACE_ENABLED

// Отладочное сообщение: <tags> - явные теги (строковый литерал), <fmt> - форматная строка
// (строковый литерал), далее - аргументы. Каждая строка сообщения печатается с префиксом
// `.../файл:строка: ` МЕСТА ВЫЗОВА TRUST_DEBUG (в исходнике компилятора) в trace::out().
#define TRUST_DEBUG(tags, fmt, ...)                                                                              \
    do {                                                                                                         \
        static const ::trust::trace::Site _trust_trace_site{__FILE__};                                           \
        if (::trust::trace::enabled() && ::trust::trace::match(_trust_trace_site, tags)) {                       \
            const std::string _trust_trace_msg = std::format(fmt __VA_OPT__(, ) __VA_ARGS__);                     \
            ::trust::trace::out() << ::trust::utils::prefixEachLine(                                              \
                _trust_trace_msg,                                                                                 \
                std::format(".../{}:{}: ", _trust_trace_site.file, static_cast<long>(__LINE__)));                 \
        }                                                                                                        \
    } while (0)

#else

// Release: макрос ВЫРЕЗАЕТСЯ - аргументы не вычисляются, код не генерируется; вызовы - no-op.
#define TRUST_DEBUG(...) ((void)0)

#endif
