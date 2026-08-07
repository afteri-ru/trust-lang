#include "diag/options.hpp"

#include "diag/diag.hpp"
#include "location/location.hpp"
#include "utils/error.hpp"

#include <algorithm>
#include <stdexcept>

namespace trust {

// Конструкторы: с/без привязки к DiagnosticEngine для отчётов об ошибках.
Options::Options(DiagnosticEngine& diag)
: m_diag(&diag) {
}
Options::Options()
: m_diag(nullptr) {
}

// Регистрация опции. Проверяет дубликаты, инициализирует name и severity.
void Options::add_option(OptKind kind, std::optional<Severity> default_severity) {
    if (by_kind_.count(kind)) {
        auto loc = MapperLocation();
        if (m_diag) {
            m_diag->report(Severity::Error, loc, "duplicate option id {}", static_cast<int>(kind));
        }
        throw std::invalid_argument("Duplicate option id");
    }

    auto name = OptName(kind);
    if (name_to_kind_.count(std::string(name))) {
        auto loc = MapperLocation();
        if (m_diag) {
            m_diag->report(Severity::Error, loc, "duplicate option name '{}'", name);
        }
        throw std::invalid_argument("Duplicate option name");
    }

    if (!default_severity.has_value()) {
        default_severity = OptDefaultSeverity(kind);
    }

    OptionEntry entry{.kind = kind, .severity = default_severity, .name = name};
    by_kind_[kind] = std::move(entry);
    name_to_kind_[std::string(name)] = kind;
}

// Регистрация булевого feature-флага (по умолчанию выключен).
void Options::register_flag(FlagKind kind) {
    if (flags_.count(kind)) {
        auto loc = MapperLocation();
        if (m_diag) {
            m_diag->report(Severity::Error, loc, "duplicate flag id {}", static_cast<int>(kind));
        }
        throw std::invalid_argument("Duplicate flag id");
    }
    auto name = FlagName(kind);
    if (flag_name_to_kind_.count(std::string(name))) {
        auto loc = MapperLocation();
        if (m_diag) {
            m_diag->report(Severity::Error, loc, "duplicate flag name '{}'", name);
        }
        throw std::invalid_argument("Duplicate flag name");
    }
    flags_[kind] = FlagEntry{};
    flag_name_to_kind_[std::string(name)] = kind;
}

bool Options::is_flag(std::string_view name) const {
    return flag_name_to_kind_.count(std::string(name)) != 0;
}

bool Options::is_enabled(FlagKind kind) const {
    auto it = flags_.find(kind);
    return it != flags_.end() && it->second.enabled;
}

bool Options::is_enabled(std::string_view name) const {
    auto it = flag_name_to_kind_.find(std::string(name));
    if (it == flag_name_to_kind_.end()) {
        return false;
    }
    return is_enabled(it->second);
}

void Options::set_enabled(FlagKind kind, bool enabled) {
    auto it = flags_.find(kind);
    if (it == flags_.end()) {
        auto loc = MapperLocation();
        if (m_diag) {
            m_diag->report(Severity::Error, loc, "unknown flag id {}", static_cast<int>(kind));
        }
        throw std::invalid_argument("Unknown flag id");
    }
    if (!flag_history_.empty()) {
        auto& delta = flag_history_.top();
        auto delta_it = std::find_if(delta.begin(), delta.end(), [&](const FlagDelta& d) { return d.kind == kind; });
        if (delta_it == delta.end()) {
            delta.push_back({.kind = kind, .previous_enabled = it->second.enabled, .previous_value = it->second.value});
        }
    }
    it->second.enabled = enabled;
    if (!enabled) {
        it->second.value.reset();
    }
}

bool Options::set_enabled(std::string_view name, bool enabled) {
    auto it = flag_name_to_kind_.find(std::string(name));
    if (it == flag_name_to_kind_.end()) {
        return false;
    }
    set_enabled(it->second, enabled);
    return true;
}

std::optional<std::string_view> Options::flag_value(FlagKind kind) const {
    auto it = flags_.find(kind);
    if (it == flags_.end()) {
        return std::nullopt;
    }
    if (!it->second.value.has_value()) {
        return std::nullopt;
    }
    return std::string_view{*it->second.value};
}

void Options::set_flag_value(FlagKind kind, std::string_view value) {
    auto it = flags_.find(kind);
    if (it == flags_.end()) {
        auto loc = MapperLocation();
        if (m_diag) {
            m_diag->report(Severity::Error, loc, "unknown flag id {}", static_cast<int>(kind));
        }
        throw std::invalid_argument("Unknown flag id");
    }
    if (!flag_history_.empty()) {
        auto& delta = flag_history_.top();
        auto delta_it = std::find_if(delta.begin(), delta.end(), [&](const FlagDelta& d) { return d.kind == kind; });
        if (delta_it == delta.end()) {
            delta.push_back({.kind = kind, .previous_enabled = it->second.enabled, .previous_value = it->second.value});
        }
    }
    it->second.enabled = true;
    it->second.value = std::string(value);
}

bool Options::set_flag_value(std::string_view name, std::string_view value) {
    auto it = flag_name_to_kind_.find(std::string(name));
    if (it == flag_name_to_kind_.end()) {
        return false;
    }
    set_flag_value(it->second, value);
    return true;
}

// Установка severity. Сохраняет предыдущее значение в верхушке стека history_ (если push был вызван).
void Options::set(OptKind kind, std::optional<Severity> severity) {
    auto it = by_kind_.find(kind);
    if (it == by_kind_.end()) {
        auto loc = MapperLocation();
        if (m_diag) {
            m_diag->report(Severity::Error, loc, "unknown option id {}", static_cast<int>(kind));
        }
        throw std::invalid_argument("Unknown option id");
    }
    if (!history_.empty()) {
        // Сохраняем предыдущее значение в текущем delta-слоёе (только первый set на уровне).
        auto& delta = history_.top();
        auto delta_it = std::find_if(delta.begin(), delta.end(), [&](const OptionDelta& d) { return d.kind == kind; });
        if (delta_it == delta.end()) {
            delta.push_back({.kind = kind, .previous_severity = it->second.severity});
        }
    }
    it->second.severity = severity;
}

void Options::set(std::string_view name, std::optional<Severity> severity) {
    auto it = name_to_kind_.find(std::string(name));
    if (it == name_to_kind_.end()) {
        auto loc = MapperLocation();
        if (m_diag) {
            m_diag->report(Severity::Error, loc, "unknown option '{}'", name);
        }
        throw std::invalid_argument("Unknown option name");
    }
    set(it->second, severity);
}

std::optional<Severity> Options::get(OptKind kind) const {
    auto it = by_kind_.find(kind);
    if (it == by_kind_.end()) {
        auto loc = MapperLocation();
        if (m_diag) {
            m_diag->report(Severity::Error, loc, "unknown option id {}", static_cast<int>(kind));
        }
        throw std::invalid_argument("Unknown option id");
    }
    return it->second.severity;
}

std::optional<Severity> Options::get(std::string_view name) const {
    auto it = name_to_kind_.find(std::string(name));
    if (it == name_to_kind_.end()) {
        auto loc = MapperLocation();
        if (m_diag) {
            m_diag->report(Severity::Error, loc, "unknown option '{}'", name);
        }
        throw std::invalid_argument("Unknown option name");
    }
    return by_kind_.at(it->second).severity;
}

// Парсинг CLI-аргументов формата -Wname[=value].
// Останавливается на первом аргументе без префикса -W.
std::span<char*> Options::parse_argv(std::span<char*> argv) {
    auto it = argv.begin();
    while (it != argv.end()) {
        std::string_view s(*it);
        if (!s.starts_with("-W")) {
            break;
        }
        std::string_view name = s.substr(2);

        const auto eq = name.find('=');
        std::string_view arg_name = (eq == std::string_view::npos) ? name : name.substr(0, eq);

        // -Wno-<flag> — выключить поведение флага. Для Comments «включено» означает ВЫВОД
        // комментариев в C++-коде (по умолчанию включено в Context), поэтому -Wno-comments →
        // set_enabled(false) (подавить), а голый -Wcomments → set_enabled(true) (выводить).
        bool negate = false;
        if (arg_name.starts_with("no-")) {
            negate = true;
            arg_name = arg_name.substr(3);
        }

        // Булев feature-флаг. Семантика (как в clang):
        //   -W<flag>          → включить (enabled=true, значение не меняется);
        //   -Wno-<flag>       → выключить (enabled=false, значение сбрасывается);
        //   -W<flag>=<value>  → включить + задать строковое значение.
        if (is_flag(arg_name)) {
            if (negate) {
                set_enabled(arg_name, false);
            } else if (eq != std::string_view::npos) {
                set_flag_value(arg_name, name.substr(eq + 1));
            } else {
                set_enabled(arg_name, true);
            }
            ++it;
            continue;
        }

        std::optional<Severity> sev;
        if (eq != std::string_view::npos) {
            auto val_str = name.substr(eq + 1);
            sev = parseSeverityName(val_str);
            if (!sev.has_value() && val_str != "ignore") {
                auto loc = MapperLocation();
                if (m_diag) {
                    m_diag->report(Severity::Error, loc, "unknown status '{}'", val_str);
                }
                throw std::invalid_argument(std::string("Unknown status value: ").append(val_str));
            }
            set(arg_name, sev);
        } else {
            if (!is_registered(name)) {
                auto loc = MapperLocation();
                if (m_diag) {
                    m_diag->report(Severity::Error, loc, "unknown option '-W{}'", name);
                }
                throw std::invalid_argument(std::string("Unknown option: -W").append(name));
            }
        }
        ++it;
    }

    return {it, argv.end()};
}

// Сохраняет текущий уровень настроек. Последующие set() записывают дельты.
void Options::push() {
    history_.emplace();
    flag_history_.emplace();
}

// Восстанавливает предыдущий уровень: откатывает все дельты верхнего слоя.
void Options::pop() {
    if (history_.empty()) {
        throw std::runtime_error("Options::pop: stack is empty");
    }
    // Откат severity-опций.
    auto& delta = history_.top();
    for (const auto& d : delta) {
        auto it = by_kind_.find(d.kind);
        if (it != by_kind_.end()) {
            it->second.severity = d.previous_severity;
        }
    }
    history_.pop();

    // Откат feature-флагов (включение + строковое значение).
    EXPECT(!flag_history_.empty() && "Options::pop: flag history out of sync");
    auto& fdelta = flag_history_.top();
    for (const auto& d : fdelta) {
        auto it = flags_.find(d.kind);
        if (it != flags_.end()) {
            it->second.enabled = d.previous_enabled;
            it->second.value = d.previous_value;
        }
    }
    flag_history_.pop();
}

std::optional<Severity> Options::severity(OptKind kind) const {
    return get(kind);
}
std::optional<Severity> Options::severity(std::string_view name) const {
    return get(name);
}

bool Options::is_registered(OptKind kind) const {
    return by_kind_.count(kind);
}
bool Options::is_registered(std::string_view name) const {
    return name_to_kind_.count(std::string(name));
}

std::string_view Options::name(OptKind kind) const {
    return OptName(kind);
}

Options Options::make(std::initializer_list<OptionInitInfo> opts) {
    Options result;
    for (auto& p : opts) {
        result.add_option(p.kind, p.severity);
    }
    return result;
}

std::optional<Severity> Options::parseSeverityName(std::string_view name) noexcept {
    if (name == "fatal") {
        return Severity::Fatal;
    }
    if (name == "error") {
        return Severity::Error;
    }
    if (name == "warning") {
        return Severity::Warning;
    }
    if (name == "remark") {
        return Severity::Remark;
    }
    if (name == "note") {
        return Severity::Note;
    }
    return std::nullopt;
}

} // namespace trust
