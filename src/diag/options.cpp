#include "diag/options.hpp"

#include "diag/location.hpp"

#include <algorithm>
#include <stdexcept>

namespace trust {

// Конструкторы: с/без привязки к DiagnosticEngine для отчётов об ошибках.
Options::Options(DiagnosticEngine& diag) : m_diag(&diag) {}
Options::Options() : m_diag(nullptr) {}

// Регистрация опции. Проверяет дубликаты, инициализирует name и severity.
void Options::add_option(OptKind kind, std::optional<Severity> default_severity) {
    if (by_kind_.count(kind)) {
        auto loc = SourceLoc::invalid();
        if (m_diag)
            m_diag->report(loc, Severity::Error, "duplicate option id {}", static_cast<int>(kind));
        throw std::invalid_argument("Duplicate option id");
    }

    auto name = OptName(kind);
    if (name_to_kind_.count(std::string(name))) {
        auto loc = SourceLoc::invalid();
        if (m_diag)
            m_diag->report(loc, Severity::Error, "duplicate option name '{}'", name);
        throw std::invalid_argument("Duplicate option name");
    }

    if (!default_severity.has_value())
        default_severity = OptDefaultSeverity(kind);

    OptionEntry entry{.kind = kind, .severity = default_severity, .name = name};
    by_kind_[kind] = std::move(entry);
    name_to_kind_[std::string(name)] = kind;
}

// Установка severity. Сохраняет предыдущее значение в верхушке стека history_ (если push был вызван).
void Options::set(OptKind kind, std::optional<Severity> severity) {
    auto it = by_kind_.find(kind);
    if (it == by_kind_.end()) {
        auto loc = SourceLoc::invalid();
        if (m_diag)
            m_diag->report(loc, Severity::Error, "unknown option id {}", static_cast<int>(kind));
        throw std::invalid_argument("Unknown option id");
    }
    if (!history_.empty()) {
        // Сохраняем предыдущее значение в текущем delta-слоёе (только первый set на уровне).
        auto& delta = history_.top();
        auto delta_it = std::find_if(delta.begin(), delta.end(),
                                     [&](const OptionDelta& d) { return d.kind == kind; });
        if (delta_it == delta.end()) {
            delta.push_back({.kind = kind, .previous_severity = it->second.severity});
        }
    }
    it->second.severity = severity;
}

void Options::set(std::string_view name, std::optional<Severity> severity) {
    auto it = name_to_kind_.find(std::string(name));
    if (it == name_to_kind_.end()) {
        auto loc = SourceLoc::invalid();
        if (m_diag)
            m_diag->report(loc, Severity::Error, "unknown option '{}'", name);
        throw std::invalid_argument("Unknown option name");
    }
    set(it->second, severity);
}

std::optional<Severity> Options::get(OptKind kind) const {
    auto it = by_kind_.find(kind);
    if (it == by_kind_.end()) {
        auto loc = SourceLoc::invalid();
        if (m_diag)
            m_diag->report(loc, Severity::Error, "unknown option id {}", static_cast<int>(kind));
        throw std::invalid_argument("Unknown option id");
    }
    return it->second.severity;
}

std::optional<Severity> Options::get(std::string_view name) const {
    auto it = name_to_kind_.find(std::string(name));
    if (it == name_to_kind_.end()) {
        auto loc = SourceLoc::invalid();
        if (m_diag)
            m_diag->report(loc, Severity::Error, "unknown option '{}'", name);
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

        std::optional<Severity> sev;
        if (auto eq = name.find('='); eq != std::string_view::npos) {
            auto arg_name = std::string(name.substr(0, eq));
            auto val_str = name.substr(eq + 1);
            if (val_str == "fatal") sev = Severity::Fatal;
            else if (val_str == "error") sev = Severity::Error;
            else if (val_str == "warning") sev = Severity::Warning;
            else if (val_str == "ignore") sev = std::nullopt;
            else if (val_str == "remark") sev = Severity::Remark;
            else if (val_str == "note") sev = Severity::Note;
            else {
                auto loc = SourceLoc::invalid();
                if (m_diag)
                    m_diag->report(loc, Severity::Error, "unknown status '{}'", val_str);
                throw std::invalid_argument(std::string("Unknown status value: ").append(val_str));
            }
            set(arg_name, sev);
        } else {
            if (!is_registered(name)) {
                auto loc = SourceLoc::invalid();
                if (m_diag)
                    m_diag->report(loc, Severity::Error, "unknown option '-W{}'", name);
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
}

// Восстанавливает предыдущий уровень: откатывает все дельты верхнего слоя.
void Options::pop() {
    if (history_.empty()) {
        throw std::runtime_error("Options::pop: stack is empty");
    }
    auto& delta = history_.top();
    for (const auto& d : delta) {
        auto it = by_kind_.find(d.kind);
        if (it != by_kind_.end()) {
            it->second.severity = d.previous_severity;
        }
    }
    history_.pop();
}

std::optional<Severity> Options::severity(OptKind kind) const { return get(kind); }
std::optional<Severity> Options::severity(std::string_view name) const { return get(name); }

bool Options::is_registered(OptKind kind) const { return by_kind_.count(kind); }
bool Options::is_registered(std::string_view name) const { return name_to_kind_.count(std::string(name)); }

std::string_view Options::name(OptKind kind) const { return OptName(kind); }

Options Options::make(std::initializer_list<OptionInitInfo> opts) {
    Options result;
    for (auto& p : opts) {
        result.add_option(p.kind, p.severity);
    }
    return result;
}

} // namespace trust