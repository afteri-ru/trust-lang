#include "diag/options.hpp"

#include "diag/diag.hpp"
#include "location/location.hpp"
#include "utils/error.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace trust {

// Конструктор: с привязкой к DiagnosticEngine для отчётов об ошибках.
Options::Options(DiagnosticEngine& diag)
: m_diag(&diag) {
}

// Регистрация severity-диагностики (по cli-имени). Проверяет дубликаты, сохраняет метаданные.
void Options::add_impl(std::string_view name, std::string_view help, Severity default_severity, WarnGroup warn_groups, DiagGroup category) {
    if (by_name_.count(name)) {
        auto loc = MapperLocation();
        if (m_diag) {
            m_diag->report(Severity::Error, loc, "duplicate option name '{}'", name);
        }
        throw std::invalid_argument("Duplicate option name");
    }
    by_name_[name] = OptionEntry{
        .name = name, .help = help, .default_severity = default_severity, .severity = default_severity, .warn_groups = warn_groups, .category = category};
}

// Регистрация булевого feature-флага (по cli-имени; по умолчанию выключен).
void Options::add_flag_impl(std::string_view name, std::string_view help, DiagGroup category) {
    if (flags_.count(name)) {
        auto loc = MapperLocation();
        if (m_diag) {
            m_diag->report(Severity::Error, loc, "duplicate flag name '{}'", name);
        }
        throw std::invalid_argument("Duplicate flag name");
    }
    flags_[name] = FlagEntry{.name = name, .help = help, .category = category, .enabled = false, .value = std::nullopt};
}

bool Options::isFlagByName(std::string_view name) const {
    return flags_.count(name) != 0;
}

bool Options::isEnabledByName(std::string_view name) const {
    auto it = flags_.find(name);
    return it != flags_.end() && it->second.enabled;
}

bool Options::setEnabledByName(std::string_view name, bool enabled) {
    auto it = flags_.find(name);
    if (it == flags_.end()) {
        return false;
    }
    if (!flag_history_.empty()) {
        auto& delta = flag_history_.top();
        auto delta_it = std::find_if(delta.begin(), delta.end(), [&](const FlagDelta& d) { return d.name == name; });
        if (delta_it == delta.end()) {
            // Храним стабильное (литерал) имя из записи, а не переданный string_view (может висеть).
            delta.push_back({.name = it->second.name, .previous_enabled = it->second.enabled, .previous_value = it->second.value});
        }
    }
    it->second.enabled = enabled;
    if (!enabled) {
        it->second.value.reset();
    }
    return true;
}

std::optional<std::string_view> Options::flagValueByName(std::string_view name) const {
    auto it = flags_.find(name);
    if (it == flags_.end() || !it->second.value.has_value()) {
        return std::nullopt;
    }
    return std::string_view{*it->second.value};
}

bool Options::setFlagValueByName(std::string_view name, std::string_view value) {
    auto it = flags_.find(name);
    if (it == flags_.end()) {
        return false;
    }
    // Валидатор допустимых значений (если зарегистрирован): невалидно → false (вызывающий ошибка).
    if (it->second.validator && !it->second.validator(value)) {
        return false;
    }
    if (!flag_history_.empty()) {
        auto& delta = flag_history_.top();
        auto delta_it = std::find_if(delta.begin(), delta.end(), [&](const FlagDelta& d) { return d.name == name; });
        if (delta_it == delta.end()) {
            delta.push_back({.name = it->second.name, .previous_enabled = it->second.enabled, .previous_value = it->second.value});
        }
    }
    it->second.enabled = true;
    it->second.value = std::string(value);
    return true;
}

bool Options::setFlagValidatorByName(std::string_view name, FlagValidator validator) {
    auto it = flags_.find(name);
    if (it == flags_.end()) {
        return false;
    }
    it->second.validator = std::move(validator);
    return true;
}

std::optional<Severity> Options::getByName(std::string_view name) const {
    auto it = by_name_.find(name);
    if (it == by_name_.end()) {
        auto loc = MapperLocation();
        if (m_diag) {
            m_diag->report(Severity::Error, loc, "unknown option '{}'", name);
        }
        throw std::invalid_argument("Unknown option name");
    }
    // Глобальный -Werror: все предупреждения повышаются до ошибок (стиль clang/gcc).
    const std::optional<Severity> sev = it->second.severity;
    if (m_werror && sev == Severity::Warning) {
        return Severity::Error;
    }
    return sev;
}

void Options::setByName(std::string_view name, std::optional<Severity> severity) {
    auto it = by_name_.find(name);
    if (it == by_name_.end()) {
        auto loc = MapperLocation();
        if (m_diag) {
            m_diag->report(Severity::Error, loc, "unknown option '{}'", name);
        }
        throw std::invalid_argument("Unknown option name");
    }
    if (!history_.empty()) {
        // Сохраняем предыдущее значение в текущем delta-слое (только первый set на уровне).
        // Храним стабильное (литерал) имя из записи, а не переданный string_view (может висеть).
        auto& delta = history_.top();
        auto delta_it = std::find_if(delta.begin(), delta.end(), [&](const OptionDelta& d) { return d.name == name; });
        if (delta_it == delta.end()) {
            delta.push_back({.name = it->second.name, .previous_severity = it->second.severity});
        }
    }
    it->second.severity = severity;
}

bool Options::isRegisteredByName(std::string_view name) const {
    return by_name_.count(name) != 0;
}

WarnGroup Options::warnGroupsByName(std::string_view name) const {
    auto it = by_name_.find(name);
    return (it == by_name_.end()) ? WG_None : it->second.warn_groups;
}

// Парсинг CLI-аргументов формата -Wname[=value]. Останавливается на первом аргументе без -W.
std::span<char*> Options::parse_argv(std::span<char*> argv) {
    auto it = argv.begin();
    while (it != argv.end()) {
        std::string_view s(*it);
        if (!s.starts_with("-W")) {
            break;
        }
        std::string_view name = s.substr(2);

        // `-Whelp` - команда вывода справки по диагностикам (стиль rustc).
        if (name == "help") {
            help_requested_ = true;
            ++it;
            continue;
        }

        const auto eq = name.find('=');
        std::string_view arg_name = (eq == std::string_view::npos) ? name : name.substr(0, eq);

        bool negate = false;
        if (arg_name.starts_with("no-")) {
            negate = true;
            arg_name = arg_name.substr(3);
        }

        // Агрегаты и группы (стиль clang): -Wall/-Wextra/-Wpedantic и -W<group> включают все
        // диагностики, привязанные к данной группе (WarnGroup); -Wno-<group> выключает (ignore).
        if (eq == std::string_view::npos) {
            if (WarnGroup grp = warnGroupFromCli(arg_name); grp != WG_None) {
                for (auto& [nm, entry] : by_name_) {
                    if ((entry.warn_groups & grp) == WG_None) {
                        continue;
                    }
                    const std::optional<Severity> target = negate ? std::nullopt : std::optional<Severity>(entry.default_severity);
                    setByName(nm, target);
                }
                ++it;
                continue;
            }
        }

        // -Werror / -Wno-error: глобальный переключатель (стиль clang/gcc).
        if (arg_name == "error") {
            m_werror = !negate;
            ++it;
            continue;
        }

        // Булев feature-флаг:
        //   -W<flag> → включить; -Wno-<flag> → выключить (сброс значения);
        //   -W<flag>=<value> → включить + задать строковое значение.
        if (isFlagByName(arg_name)) {
            if (negate) {
                setEnabledByName(arg_name, false);
            } else if (eq != std::string_view::npos) {
                // Валидация значения (валидатор флага): невалидно → ошибка (no silent fallback).
                if (!setFlagValueByName(arg_name, name.substr(eq + 1))) {
                    auto loc = MapperLocation();
                    if (m_diag) {
                        m_diag->report(Severity::Error, loc, "invalid value '{}' for option '-W{}'", name.substr(eq + 1), arg_name);
                    }
                    throw std::invalid_argument(std::string("Invalid flag value for -W").append(arg_name));
                }
            } else {
                setEnabledByName(arg_name, true);
            }
            ++it;
            continue;
        }

        if (eq != std::string_view::npos) {
            auto val_str = name.substr(eq + 1);
            std::optional<Severity> sev = severityFromName(val_str);
            if (!sev.has_value() && val_str != "ignore") {
                auto loc = MapperLocation();
                if (m_diag) {
                    m_diag->report(Severity::Error, loc, "unknown status '{}'", val_str);
                }
                throw std::invalid_argument(std::string("Unknown status value: ").append(val_str));
            }
            setByName(arg_name, sev);
        } else {
            // `-W<name>` / `-Wno-<name>` для severity-диагностики (стиль clang/gcc):
            //   -W<name>    -> уровень по умолчанию (включить);
            //   -Wno-<name> -> ignore (выключить).
            auto it = by_name_.find(arg_name);
            if (it == by_name_.end()) {
                auto loc = MapperLocation();
                if (m_diag) {
                    m_diag->report(Severity::Error, loc, "unknown option '-W{}'", name);
                }
                throw std::invalid_argument(std::string("Unknown option: -W").append(name));
            }
            setByName(arg_name, negate ? std::nullopt : std::optional<Severity>(it->second.default_severity));
        }
        ++it;
    }

    return {it, argv.end()};
}

namespace {
// Имя severity для справки (nullopt = "ignore").
const char* sevToName(const std::optional<Severity>& s) {
    return s.has_value() ? severityName(*s).data() : "ignore";
}
// CLI-суффиксы групп, к которым привязана диагностика (через запятую).
std::string groupNames(WarnGroup wg) {
    std::string s;
    for (WarnGroup g : kAllWarnGroups) {
        if ((wg & g) != WG_None) {
            if (!s.empty()) {
                s += ",";
            }
            s += std::string(warnGroupCli(g));
        }
    }
    return s;
}
} // namespace

// Печать списка диагностик для `-Whelp` (единый формат, сгруппирован по DiagGroup).
void Options::printHelp(std::ostream& os) const {
    os << "Diagnostics (-W<name>, -Wno-<name>, -W<name>=<severity>):\n";

    os << "\nGroups (-W<group>, -Wno-<group>):\n";
    for (WarnGroup g : kAllWarnGroups) {
        os << "  -W" << warnGroupCli(g) << "  (" << warnGroupName(g) << ")\n";
    }
    os << "  -Werror / -Wno-error   treat warnings as errors\n";

    for (int g = 0; g <= static_cast<int>(DiagGroup::Codegen); ++g) {
        const auto grp = static_cast<DiagGroup>(g);
        bool any = false;

        std::vector<const OptionEntry*> diags;
        for (const auto& [name, entry] : by_name_) {
            (void)name;
            if (entry.category == grp) {
                diags.push_back(&entry);
            }
        }
        std::sort(diags.begin(), diags.end(), [](const OptionEntry* a, const OptionEntry* b) { return a->name < b->name; });
        for (const OptionEntry* e : diags) {
            if (!any) {
                os << "\n" << diagGroupName(grp) << ":\n";
                any = true;
            }
            os << "  -W" << e->name << "  " << e->help;
            const std::string grps = groupNames(e->warn_groups);
            if (!grps.empty()) {
                os << "  [in: " << grps << "]";
            }
            os << "  (default: " << sevToName(e->severity) << ")\n";
        }

        std::vector<const FlagEntry*> flgs;
        for (const auto& [name, entry] : flags_) {
            (void)name;
            if (entry.category == grp) {
                flgs.push_back(&entry);
            }
        }
        std::sort(flgs.begin(), flgs.end(), [](const FlagEntry* a, const FlagEntry* b) { return a->name < b->name; });
        for (const FlagEntry* f : flgs) {
            if (!any) {
                os << "\n" << diagGroupName(grp) << ":\n";
                any = true;
            }
            os << "  -W" << f->name << " / -Wno-" << f->name << "  " << f->help << "  (" << (f->enabled ? "on" : "off") << ")\n";
        }
    }

    os << "\n  -Whelp   show this list\n";
}

std::vector<std::string> Options::allWNames() const {
    std::vector<std::string> out;
    for (const auto& [name, entry] : by_name_) {
        (void)entry;
        out.push_back(std::string("-W") + std::string(name));
    }
    for (const auto& [name, entry] : flags_) {
        (void)entry;
        out.push_back(std::string("-W") + std::string(name));
    }
    std::sort(out.begin(), out.end());
    return out;
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
    auto& delta = history_.top();
    for (const auto& d : delta) {
        auto it = by_name_.find(d.name);
        if (it != by_name_.end()) {
            it->second.severity = d.previous_severity;
        }
    }
    history_.pop();

    EXPECT(!flag_history_.empty() && "Options::pop: flag history out of sync");
    auto& fdelta = flag_history_.top();
    for (const auto& d : fdelta) {
        auto it = flags_.find(d.name);
        if (it != flags_.end()) {
            it->second.enabled = d.previous_enabled;
            it->second.value = d.previous_value;
        }
    }
    flag_history_.pop();
}

WarnGroup Options::warnGroupFromCli(std::string_view cli) {
    // Принимаем и суффикс (`-Wall` → "all"), и имя группы (`-Wno-Wall` → "Wall") - регистронезависимо.
    std::string s(cli);
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    for (WarnGroup g : kAllWarnGroups) {
        auto lower = [](std::string_view v) {
            std::string r(v);
            std::transform(r.begin(), r.end(), r.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return r;
        };
        if (s == lower(warnGroupCli(g)) || s == lower(warnGroupName(g))) {
            return g;
        }
    }
    return WG_None;
}

} // namespace trust
