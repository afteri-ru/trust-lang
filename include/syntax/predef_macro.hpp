#pragma once

// include/syntax/predef_macro.hpp
// Реестры предопределённых макросов (@__...__ и др.) и их раскрытие/подстановка.
// ДВЕ категории (см. predef_macro_x.hpp):
//   - PredefMacroId  - «значение-макросы»: фиксированные значения, вычислимые на парсере;
//   - ContextMacroId - «контекст-макросы» (информация анализатора): разрешаются на этапе анализатора
//                      (NameResolutionPass), часть (@__MODULE_NAME__) статически вычислима на парсере.
// Прагмы (директивы/опции) - отдельный класс PragmaEvaluator (pragma_evaluator.hpp).
// Вынесен из монолита Parser (src/syntax/parser.cpp) в отдельный класс - одна
// ответственность, изолированно тестируем. Реестры - общие на процесс (static),
// экземпляр нужен только для раскрытия (Context + timestamp).

#include "syntax/term_types.h"
#include "syntax/predef_macro_x.hpp"

#include "syntax/warning_push.h"
#include "parser.yy.h"
#include "syntax/warning_pop.h"

#include <ctime>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace trust {
class Context;

namespace syntax {

/// Идентификаторы значение-макросов (генерируются из TRUST_VALUE_MACROS).
enum class PredefMacroId : int {
#define X(Name, Str, Desc) Name,
    TRUST_VALUE_MACROS(X)
#undef X
        Count,
};

// Таблица (имя, описание), генерируемая из x-macro. Описание задано в скобках, поэтому
// запятые в тексте не разбивают аргумент макроса.
inline constexpr struct {
    const char* name;
    const char* desc;
} kPredefMacros[] = {
#define X(Name, Str, Desc) {Str, Desc},
    TRUST_VALUE_MACROS(X)
#undef X
};

/// Имя значение-макроса по идентификатору.
constexpr const char* predefMacroName(PredefMacroId id) {
    return kPredefMacros[static_cast<int>(id)].name;
}
/// Описание значение-макроса (для Context::macroDocs / справки).
constexpr const char* predefMacroDesc(PredefMacroId id) {
    return kPredefMacros[static_cast<int>(id)].desc;
}
/// Поиск идентификатора значение-макроса по имени (точное совпадение).
inline std::optional<PredefMacroId> predefMacroId(std::string_view name) {
    for (int i = 0; i < static_cast<int>(PredefMacroId::Count); ++i) {
        if (kPredefMacros[i].name == name) {
            return static_cast<PredefMacroId>(i);
        }
    }
    return std::nullopt;
}

/// Идентификаторы контекст-макросов (информация анализатора; из TRUST_CONTEXT_MACROS).
enum class ContextMacroId : int {
#define X(Name, Str, Desc) Name,
    TRUST_CONTEXT_MACROS(X)
#undef X
        Count,
};

// Таблица (имя, описание), генерируемая из x-macro.
inline constexpr struct {
    const char* name;
    const char* desc;
} kContextMacros[] = {
#define X(Name, Str, Desc) {Str, Desc},
    TRUST_CONTEXT_MACROS(X)
#undef X
};

/// Имя контекст-макроса по идентификатору.
constexpr const char* contextMacroName(ContextMacroId id) {
    return kContextMacros[static_cast<int>(id)].name;
}
/// Описание контекст-макроса (для Context::macroDocs / справки).
constexpr const char* contextMacroDesc(ContextMacroId id) {
    return kContextMacros[static_cast<int>(id)].desc;
}
/// Поиск идентификатора контекст-макроса по имени (точное совпадение).
inline std::optional<ContextMacroId> contextMacroId(std::string_view name) {
    for (int i = 0; i < static_cast<int>(ContextMacroId::Count); ++i) {
        if (kContextMacros[i].name == name) {
            return static_cast<ContextMacroId>(i);
        }
    }
    return std::nullopt;
}

/// true, если токен «выглядит» как предопределённый макрос (шаблон @__...__).
/// Общий предикат для pragmaCheck (это прагма?) и диагностики «not implemented»
/// в expandPredefMacro. Пользовательские макросы (@foo) этому шаблону не соответствуют.
inline bool looksLikePredefMacro(std::string_view text) {
    return text.size() > 5 && text.find("@__") == 0 && text.rfind("__") == text.size() - 2;
}

/// Реестры предопределённых макросов (значение- и контекст-) + раскрытие и подстановка внутри {% %}.
class PredefMacroResolver {
  public:
    explicit PredefMacroResolver(trust::Context& ctx);

    // -- Реестры (static: общие на процесс, не требуют инстанса) --

    /// Имена всех предопределённых макросов (значение- + контекст- + прагма-) - для автодополнения.
    static std::vector<std::string> predefMacroNames();
    /// true, если терм - зарегистрированный значение-макрос.
    static bool checkPredefMacro(const TermPtr& term);
    /// true, если терм - зарегистрированный контекст-макрос (информация анализатора).
    static bool checkContextMacro(const TermPtr& term);

    /// Имена прагма-макросов (@__OPTION_*, @__HYGIENIC__, @__PRAGMA_*). Обрабатываются
    /// PragmaEvaluator, но НЕ входят в реестры предопределённых макросов (иначе сломался бы
    /// pragmaCheck). Список нужен для автодополнения и диагностики внутри {% %}.
    static const std::vector<std::string>& pragmaMacroNames();

    // -- Раскрытие (нужен Context + timestamp) --

    /// Раскрывает предопределённый макрос @__NAME__ (или @::/$\\\\\\) в терм. Единая точка входа:
    /// значение-макросы подставляет (expandValueMacro), контекст-макросы штампует транзитными
    /// маркерами (stampContextMacro), прагмы оставляет без изменений (их ведёт PragmaEvaluator).
    parser::token_type expandPredefMacro(TermPtr& term);
    /// Раскрывает @__NAME__ (значение-макросы и статически вычислимые контекст-макросы вроде
    /// @__MODULE_NAME__) внутри C++-вставки {% ... %} «наравне с переменными»: каждое вхождение
    /// заменяется значением. Нераскрываемые контекст-макросы (@__FUNCTION__ и др.) и прагмы здесь
    /// дают диагностику.
    void expandEmbedPredefMacros(std::string& text, const MapperRange& range);

  private:
    /// Раскрывает значение-макрос в терм (подстановка значения).
    parser::token_type expandValueMacro(TermPtr& term);
    /// Штампует контекст-макрос транзитным маркером (MACRO_CONTEXT/NAMESPACE/NAME/MODULE);
    /// значение для статически вычислимых (@__MODULE_NAME__) подставляет на парсере.
    parser::token_type stampContextMacro(TermPtr& term);

    trust::Context& m_ctx;
    time_t m_timestamp;
};

} // namespace syntax
} // namespace trust
