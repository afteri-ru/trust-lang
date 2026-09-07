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
    /// flagName/flagHelp/flagCategory (id - пер-компонентный enum). Необязательное default_value -
    /// значение по умолчанию value-флага (пусто = нет значения по умолчанию).
    template <typename T>
        requires std::is_enum_v<T>
    void add_flag(T id, std::string_view default_value = {}) {
        add_flag_impl(flagName(id), flagHelp(id), flagCategory(id), default_value);
    }

    /// Регистрирует feature-флаг, НЕ управляемый через `-W<name>`/`-Wno-<name>` (w_diagnostic=false).
    /// Такой флаг включается/выключается ТОЛЬКО поведенческим флагом драйвера (`-f<name>`/`-fno-<name>`),
    /// а `-W<name>` для него — неизвестная опция. is_enabled/set_enabled работают как обычно.
    template <typename T>
        requires std::is_enum_v<T>
    void add_flag_nonw(T id, std::string_view default_value = {}) {
        add_flag_nonw_impl(flagName(id), flagHelp(id), flagCategory(id), default_value);
    }

    // -- Severity-опции: шаблоны (пер-компонентный id) и name-методы (cli-имя) --

    /// Текущий уровень severity (учитывает -Werror). Severity::Ignore = диагностика выключена.
    template <typename T>
        requires std::is_enum_v<T>
    Severity get(T id) const {
        return getByName(diagName(id));
    }
    /// Текущий уровень severity по cli-имени; Severity::Ignore = выключена; бросает, если не зарегистрирована.
    [[nodiscard]] Severity getByName(std::string_view name) const;

    template <typename T>
        requires std::is_enum_v<T>
    void set(T id, Severity severity) {
        setByName(diagName(id), severity);
    }
    void setByName(std::string_view name, Severity severity);

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

    /// Тема справки, запрошенной через `-Whelp` / `-Whelp-dsl` / `-Whelp-predef-macros` /
    /// `-Whelp-check-areas`.
    /// Options (diag) — «лист» и не знает про DSL/макросы/области, поэтому печать самой справки
    /// выполняется ВНЕ Options (в приложении, trust.cpp); здесь фиксируется только факт
    /// и тема запроса.
    enum class HelpTopic : int {
        None = 0,     ///< справка не запрашивалась
        Diagnostics,  ///< `-Whelp` — список диагностик (printHelp)
        DslMacros,    ///< `-Whelp-dsl` — список макросов из загруженного dsl.src
        PredefMacros, ///< `-Whelp-predef-macros` — список предопределённых макросов @__...__
        CheckAreas,   ///< `-Whelp-check-areas` — список синтаксических областей @__CHECK_AREA__
    };

    /// true, если встретилась любая команда справки (`-Whelp` / `-Whelp-dsl` / ... ).
    [[nodiscard]] bool helpRequested() const { return help_topic_ != HelpTopic::None; }
    /// Тема запрошенной справки (None — справка не запрашивалась). Печать по теме — обязанность
    /// приложения (см. helpRequested).
    [[nodiscard]] HelpTopic helpTopic() const { return help_topic_; }

    /// Печатает список всех зарегистрированных диагностик (severity-опции и feature-флаги)
    /// в едином формате. Используется для `-Whelp` (HelpTopic::Diagnostics).
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
        Severity severity = Severity::Warning;         ///< текущий уровень (мутабельный); Severity::Ignore = выкл.
        WarnGroup warn_groups = WG_None;               ///< группы-агрегаты
        DiagGroup category = DiagGroup::Diagnostics;   ///< категория `-Whelp`
    };

    struct OptionDelta {
        std::string_view name; ///< cli-имя (литерал, стабильно)
        Severity previous_severity;
    };

    /// Запись булевого feature-флага: вкл/выкл + необязательное строковое значение + метаданные.
    struct FlagEntry {
        std::string_view name;
        std::string_view help;                    ///< подсказка для `-Whelp`
        DiagGroup category = DiagGroup::Analysis; ///< категория `-Whelp`
        bool enabled = false;
        bool w_diagnostic = true; ///< true — управляется через -W<name>; false — только -f<name>
        std::optional<std::string> value;
        FlagValidator validator; ///< валидатор допустимых значений value-флага (nullptr = нет)
    };

    /// Дельта изменения флага для отката push/pop.
    struct FlagDelta {
        std::string_view name;
        bool previous_enabled;
        std::optional<std::string> previous_value;
    };

    /// Регистрирует severity-диагностику (по cli-имени). Проверяет дубликаты, сохраняет метаданные.
    void add_impl(std::string_view name, std::string_view help, Severity default_severity, WarnGroup warn_groups, DiagGroup category);
    /// default_value - значение по умолчанию value-флага (пусто = нет); задаётся в момент регистрации.
    void add_flag_impl(std::string_view name, std::string_view help, DiagGroup category, std::string_view default_value);
    /// Как add_flag_impl, но флаг НЕ управляется через -W (w_diagnostic=false; см. add_flag_nonw).
    void add_flag_nonw_impl(std::string_view name, std::string_view help, DiagGroup category, std::string_view default_value);

    /// true, если флаг с данным cli-именем управляется через `-W<name>` (w_diagnostic).
    [[nodiscard]] bool flagAllowsW(std::string_view name) const;

    std::unordered_map<std::string_view, OptionEntry> by_name_;
    std::unordered_map<std::string_view, FlagEntry> flags_;
    std::stack<std::vector<OptionDelta>> history_;
    std::stack<std::vector<FlagDelta>> flag_history_;
    /// Тема запрошенной справки (None = справка не запрашивалась; -Whelp/-Whelp-dsl/-Whelp-predef-macros).
    HelpTopic help_topic_ = HelpTopic::None;
    /// Глобальный `-Werror`: повышает все предупреждения до ошибок (стиль clang/gcc).
    bool m_werror = false;
    DiagnosticEngine* m_diag = nullptr;
};

} // namespace trust
