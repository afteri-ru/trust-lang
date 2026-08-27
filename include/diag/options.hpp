#pragma once

#include "diag/severity.hpp"
#include "diag/options_decl.hpp"

#include <algorithm>
#include <functional>
#include <initializer_list>
#include <optional>
#include <ostream>
#include <span>
#include <stack>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace trust {

class DiagnosticEngine;

// Options - реестр диагностик/флагов, ключуется по cli-имени.
// Каждая компонента объявляет СВОИ диагностики/флаги в своём заголовке через
// TRUST_DIAG_SET/TRUST_FLAG_SET (diag/diag_set.hpp) и регистрирует их через
// Options::add<T> / Options::add_flag<T> (метаданные берутся через ADL из пер-компонентной
// декларации; diag остаётся листом - не включает заголовки компонентов).
class Options {
  public:
    explicit Options(DiagnosticEngine& diag);

    // -- Регистрация (шаблоны; ADL-доступы из namespace компоненты) --

    /// Регистрирует severity-диагностику. Метаданные через ADL: diagName/diagHelp/
    /// diagDefaultSeverity/diagWarnGroups/diagCategory (id - пер-компонентный enum).
    template <typename T>
        requires std::is_enum_v<T>
    void add(T id) {
        add_impl(diagName(id), diagHelp(id), diagDefaultSeverity(id), diagWarnGroups(id), diagCategory(id));
    }

    /// Регистрирует feature-флаг (по умолчанию выключен). Метаданные через ADL:
    /// flagName/flagHelp/flagCategory (id - пер-компонентный enum).
    template <typename T>
        requires std::is_enum_v<T>
    void add_flag(T id) {
        add_flag_impl(flagName(id), flagHelp(id), flagCategory(id));
    }

    // -- Severity-опции: шаблоны (пер-компонентный id) и name-методы (cli-имя) --

    /// Текущий уровень severity (учитывает -Werror); nullopt = ignore.
    template <typename T>
        requires std::is_enum_v<T>
    std::optional<Severity> get(T id) const {
        return getByName(diagName(id));
    }
    /// Текущий уровень severity по cli-имени; nullopt = ignore; бросает, если не зарегистрирована.
    [[nodiscard]] std::optional<Severity> getByName(std::string_view name) const;

    template <typename T>
        requires std::is_enum_v<T>
    void set(T id, std::optional<Severity> severity) {
        setByName(diagName(id), severity);
    }
    void setByName(std::string_view name, std::optional<Severity> severity);

    template <typename T>
        requires std::is_enum_v<T>
    bool is_registered(T id) const {
        return isRegisteredByName(diagName(id));
    }
    [[nodiscard]] bool isRegisteredByName(std::string_view name) const;

    template <typename T>
        requires std::is_enum_v<T>
    WarnGroup warn_groups(T id) const {
        return warnGroupsByName(diagName(id));
    }
    /// Группы-агрегаты, к которым привязана диагностика (из зарегистрированной записи).
    [[nodiscard]] WarnGroup warnGroupsByName(std::string_view name) const;

    // -- Feature-флаги: шаблоны (пер-компонентный id) и name-методы (cli-имя) --

    /// true, если cli-имя является флагом (а не severity-опцией).
    [[nodiscard]] bool isFlagByName(std::string_view name) const;

    template <typename T>
        requires std::is_enum_v<T>
    bool is_enabled(T id) const {
        return isEnabledByName(flagName(id));
    }
    /// Текущее состояние флага по cli-имени (незарегистрированный = false).
    [[nodiscard]] bool isEnabledByName(std::string_view name) const;

    /// Включить/выключить флаг; false, если флаг не найден.
    template <typename T>
        requires std::is_enum_v<T>
    bool set_enabled(T id, bool enabled) {
        return setEnabledByName(flagName(id), enabled);
    }
    bool setEnabledByName(std::string_view name, bool enabled);

    template <typename T>
        requires std::is_enum_v<T>
    std::optional<std::string_view> flag_value(T id) const {
        return flagValueByName(flagName(id));
    }
    [[nodiscard]] std::optional<std::string_view> flagValueByName(std::string_view name) const;

    /// Установить значение флага (неявно включает); false, если флаг не найден.
    template <typename T>
        requires std::is_enum_v<T>
    bool set_flag_value(T id, std::string_view value) {
        return setFlagValueByName(flagName(id), value);
    }
    bool setFlagValueByName(std::string_view name, std::string_view value);

    /// Валидатор допустимых значений value-флага. Если зарегистрирован, setFlagValueByName
    /// отклоняет недопустимые значения (возвращает false - вызывающий выдаёт ошибку). Это
    /// гарантирует «no silent fallback»: потребитель флага видит только валидные значения.
    using FlagValidator = std::function<bool(std::string_view)>;
    template <typename T>
        requires std::is_enum_v<T>
    bool set_flag_validator(T id, FlagValidator validator) {
        return setFlagValidatorByName(flagName(id), std::move(validator));
    }
    bool setFlagValidatorByName(std::string_view name, FlagValidator validator);

    // -- CLI-парсинг и справка --

    std::span<char*> parse_argv(std::span<char*> argv);

    /// true, если в CLI встретилась команда `-Whelp` / `-Whelp` (справка по диагностикам).
    [[nodiscard]] bool helpRequested() const { return help_requested_; }

    /// Печатает список всех зарегистрированных диагностик (severity-опции и feature-флаги)
    /// в едином формате. Используется для `-Whelp`.
    void printHelp(std::ostream& os) const;

    /// Все зарегистрированные cli-имена (severity-опции и флаги) с префиксом `-W` — для
    /// shell-completion (`trust --complete-options`).
    [[nodiscard]] std::vector<std::string> allWNames() const;

    void push();
    void pop();

    /// CLI-суффикс группы → WarnGroup ("all"→WG_Wall, "unused"→WG_Wunused, ...);
    /// WG_None, если имя не является группой.
    [[nodiscard]] static WarnGroup warnGroupFromCli(std::string_view cli);

  private:
    struct OptionEntry {
        std::string_view name;
        std::string_view help;                         ///< подсказка для `-Whelp`
        Severity default_severity = Severity::Warning; ///< уровень по умолчанию (для -Wall сброса)
        std::optional<Severity> severity;              ///< текущий уровень (мутабельный)
        WarnGroup warn_groups = WG_None;               ///< группы-агрегаты
        DiagGroup category = DiagGroup::Diagnostics;   ///< категория `-Whelp`
    };

    struct OptionDelta {
        std::string_view name; ///< cli-имя (литерал, стабильно)
        std::optional<Severity> previous_severity;
    };

    /// Запись булевого feature-флага: вкл/выкл + необязательное строковое значение + метаданные.
    struct FlagEntry {
        std::string_view name;
        std::string_view help;                    ///< подсказка для `-Whelp`
        DiagGroup category = DiagGroup::Analysis; ///< категория `-Whelp`
        bool enabled = false;
        std::optional<std::string> value;
        FlagValidator validator; ///< валидатор допустимых значений value-флага (nullptr = нет)
    };

    /// Дельта изменения флага для отката push/pop.
    struct FlagDelta {
        std::string_view name;
        bool previous_enabled;
        std::optional<std::string> previous_value;
    };

    void add_impl(std::string_view name, std::string_view help, Severity default_severity, WarnGroup warn_groups, DiagGroup category);
    void add_flag_impl(std::string_view name, std::string_view help, DiagGroup category);

    std::unordered_map<std::string_view, OptionEntry> by_name_;
    std::unordered_map<std::string_view, FlagEntry> flags_;
    std::stack<std::vector<OptionDelta>> history_;
    std::stack<std::vector<FlagDelta>> flag_history_;
    bool help_requested_ = false;
    /// Глобальный `-Werror`: повышает все предупреждения до ошибок (стиль clang/gcc).
    bool m_werror = false;
    DiagnosticEngine* m_diag = nullptr;
};

} // namespace trust
