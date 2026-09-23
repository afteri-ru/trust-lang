#pragma once

// include/semantic/debug_scope.hpp
// Дамп состояния скоупа анализатора AST (УРОВЕНЬ 2: C++ компилятора - источник данных).
//
// Используется двумя путями:
//   - C++ макрос TRUST_SCOPE(tags, symbols[, key=value...]) - ручной дамп из кода компилятора;
//   - системный макрос TrustLang `@__DEBUG_SCOPE__(<masks>[, key=value...])` - запрос дампа в точке
//     исходника (обрабатывается семантикой, см. analyzeDebugStmt).
//
// Разделение ответственности: фильтрация сообщений/ключевых слов и печать локации - в
// utils/trace.hpp (Site/match); реестр именованных опций (имена/допустимые значения) - в
// utils/trace_options.hpp (используется и парсером макроса, и здесь); здесь - применение опций
// к ScopeDumpOpts и форматирование скоуп-стека.

#include "utils/error.hpp"
#include "utils/trace.hpp"
#include "utils/trace_options.hpp"

#include "ast/token_base.hpp"
#include "analysis/symbol_table.hpp"

#include <initializer_list>
#include <vector>

#include <cstddef>
#include <format>
#include <string>
#include <string_view>

namespace trust::debug {

/// Опции дампа состояния скоупа (именованные аргументы; см. parseScopeDumpArgs).
struct ScopeDumpOpts {
    bool current_only = true; ///< level=current - только текущий (внутренний) уровень
    bool show_names = true;   ///< показывать имена символов уровня
    bool show_types = false;  ///< types=on - показывать TypeId и Storage символа
    std::size_t max_names = 20; ///< max=N - лимит имён на уровень (0 = без лимита)
    std::string name_mask;    ///< names=<маски> - фильтр имён (через ','; пусто = все)
    bool count_only = false;  ///< count=only - только depth/names, без разбивки по уровням
};

/// Читаемое имя класса памяти (Storage) - для types=on.
inline const char* storageName(Storage s) noexcept {
    switch (s) {
    case Storage::Global:
        return "global";
    case Storage::Local:
        return "local";
    case Storage::Static:
        return "static";
    case Storage::ThreadLocal:
        return "thread";
    }
    return "?";
}

/// Применение одной УЖЕ провалидированной именованной опции к opts
/// (валидация имён/значений - trust::trace::parseScopeDumpOption).
inline void applyScopeDumpOption(trust::trace::ScopeDumpOptionId id, std::string_view value, ScopeDumpOpts& opts) {
    switch (id) {
    case trust::trace::ScopeDumpOptionId::Level:
        opts.current_only = (value == "current");
        break;
    case trust::trace::ScopeDumpOptionId::Types:
        opts.show_types = (value == "on");
        break;
    case trust::trace::ScopeDumpOptionId::Max: {
        std::size_t n = 0;
        for (const char c : value) {
            n = n * 10 + static_cast<std::size_t>(c - '0');
        }
        opts.max_names = n;
        break;
    }
    case trust::trace::ScopeDumpOptionId::CountOnly:
        opts.count_only = true;
        break;
    case trust::trace::ScopeDumpOptionId::Count:
        break; // сентинел: недостижимо
    }
}

/// Разбор ИМЕНОВАННЫХ аргументов дампа (`key=value`, каждый - отдельным аргументом макроса;
/// позиционные маски сюда НЕ входят). false + err при ошибке (без тихого fallback): err - причина,
/// полный список поддерживаемых опций с допустимыми значениями - `trace::scopeDumpOptionsUsage()`.
inline bool parseScopeDumpArgs(const std::vector<std::string>& args, ScopeDumpOpts& opts, std::string& err) {
    for (const std::string& arg : args) {
        trust::trace::ScopeDumpOptionId id{};
        std::string_view value;
        if (!trust::trace::parseScopeDumpOption(arg, id, value, err)) {
            return false;
        }
        applyScopeDumpOption(id, value, opts);
    }
    return true;
}

/// Опции для C++-макроса TRUST_SCOPE (именованные аргументы): невалидные - FAULT (ошибка
/// программиста) с полным списком поддерживаемых опций.
inline ScopeDumpOpts scopeDumpOptsFrom(std::initializer_list<std::string_view> named = {}) {
    std::vector<std::string> args;
    args.reserve(named.size());
    for (const std::string_view a : named) {
        args.emplace_back(a);
    }
    ScopeDumpOpts opts;
    std::string err;
    if (!parseScopeDumpArgs(args, opts, err)) {
        FAULT("TRUST_SCOPE: {} (supported options: {})", err, trust::trace::scopeDumpOptionsUsage());
    }
    return opts;
}

namespace detail {

/// Загрузчик ссылок на скоупы для SymbolTable::forEachScope (именованный функтор вместо лямбды:
/// forEachScope принимает callable; держим его вне лямбд по стилю проекта).
struct ScopeRefCollector {
    std::vector<const SymbolTable::Scope*>& out;

    void operator()(const SymbolTable::Scope& scope) const { out.push_back(&scope); }
};

} // namespace detail

/// Формирует текстовый дамп состояния скоупа (стек SymbolTable, уровни, имена).
/// Уровень 0 - глобальный; уровни нумеруются от внутреннего к внешнему по убыванию.
inline std::string formatScopeStack(const SymbolTable& symbols, const ScopeDumpOpts& opts) {
    std::vector<const SymbolTable::Scope*> scopes;
    symbols.forEachScope(detail::ScopeRefCollector{scopes});

    std::size_t totalNames = 0;
    for (const SymbolTable::Scope* s : scopes) {
        totalNames += s->symbols.size();
    }
    std::string result = std::format("scope: depth={} names={}", scopes.size(), totalNames);
    if (opts.count_only) {
        return result;
    }

    const std::size_t limit = opts.current_only ? (scopes.empty() ? 0 : 1) : scopes.size();
    for (std::size_t i = 0; i < limit; ++i) {
        const SymbolTable::Scope& s = *scopes[i];
        const std::size_t level = scopes.size() - 1 - i; // 0 - глобальный
        std::string_view ckind;
        std::string_view ctext;
        if (s.creator) {
            ckind = ParserToken::name(s.creator->kind());
            if (s.creator->term()) {
                ctext = s.creator->text();
            }
        } else {
            ckind = (level == 0) ? "global" : "-";
        }
        result += std::format("\n  [{}] {}", level, ckind);
        if (!ctext.empty()) {
            result += std::format(" `{}`", ctext);
        }
        if (!s.importedNamespaces.empty()) {
            result += " imports=[";
            for (std::size_t k = 0; k < s.importedNamespaces.size(); ++k) {
                result += (k == 0 ? "" : ",");
                result += s.importedNamespaces[k];
            }
            result += "]";
        }
        if (!opts.show_names) {
            continue;
        }
        std::size_t shown = 0;
        bool truncated = false;
        std::string names;
        for (const auto& [name, sym] : s.symbols) {
            if (!opts.name_mask.empty() && !trust::trace::maskListMatches(opts.name_mask, name)) {
                continue;
            }
            if (opts.max_names != 0 && shown >= opts.max_names) {
                truncated = true;
                break;
            }
            names += (shown == 0 ? "" : ", ");
            if (opts.show_types) {
                names += std::format("{}<T{},{}>", name, static_cast<unsigned long long>(sym.type), storageName(sym.storage));
            } else {
                names += name;
            }
            ++shown;
        }
        if (!names.empty()) {
            result += std::format(": {}", names);
        }
        if (truncated) {
            result += " ...";
        }
    }
    return result;
}

} // namespace trust::debug

#if TRUST_TRACE_ENABLED

// Дамп состояния скоупа анализатора: <tags> - явные теги (строковый литерал),
// <symbols> - выражение типа const SymbolTable& (обычно symbols()), далее - именованные опции
// дампа строками "key=value" (см. utils/trace_options.hpp). Печатает `path:line: <дамп>` в
// trace::out(). Пример: TRUST_SCOPE("scope", symbols(), "level=all", "names=x*");
#define TRUST_SCOPE(tags, symbols, ...)                                                               \
    do {                                                                                              \
        static const ::trust::trace::Site _trust_trace_site{__FILE__};                                \
        if (::trust::trace::enabled() && ::trust::trace::match(_trust_trace_site, tags)) {            \
            ::trust::trace::out()                                                                     \
                << ::trust::utils::prefixEachLine(                                                    \
                       ::trust::debug::formatScopeStack(                                              \
                           (symbols), ::trust::debug::scopeDumpOptsFrom(                              \
                                          std::initializer_list<std::string_view>{__VA_OPT__(__VA_ARGS__)})), \
                       std::format(".../{}:{}: ", _trust_trace_site.file, static_cast<long>(__LINE__))); \
        }                                                                                             \
    } while (0)

#else

// Release: макрос ВЫРЕЗАЕТСЯ - аргументы не вычисляются, код не генерируется; вызовы - no-op.
#define TRUST_SCOPE(...) ((void)0)

#endif
