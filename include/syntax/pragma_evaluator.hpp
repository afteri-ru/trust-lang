#pragma once

// include/syntax/pragma_evaluator.hpp
// Обработка прагм и опций компилятора (@__PRAGMA_*, @__OPTION_*, @__HYGIENIC__).
// Вынесен из монолита Parser (src/syntax/parser.cpp) в отдельный класс - одна
// ответственность. Владеет связанным состоянием: гигиенические имена (общие с
// раскрытием макросов) и список ожидаемых токенов @__PRAGMA_EXPECTED__.

#include "syntax/term_types.h"
#include "syntax/pragma_macro_x.hpp"
#include "parser.yy.h"   // parser::token_type (TRY_ALL_END/TRY_MINUS_END/TRY_PLUS_END)

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace trust {
class Context;

namespace syntax {
class PredefMacroResolver;

/// Идентификаторы прагм (генерируются из TRUST_PRAGMA_MACROS).
enum class PragmaMacroId : int {
#define X(Name, Str, Desc) Name,
    TRUST_PRAGMA_MACROS(X)
#undef X
        Count,
};

// Таблица (имя, описание), генерируемая из x-macro. Описание - та же справка, что сидируется
// в Context::macroDocs (единый источник без рассинхрона между кодом и справкой).
inline constexpr struct {
    const char* name;
    const char* desc;
} kPragmaMacros[] = {
#define X(Name, Str, Desc) {Str, Desc},
    TRUST_PRAGMA_MACROS(X)
#undef X
};

/// Имя прагмы по идентификатору.
constexpr const char* pragmaMacroName(PragmaMacroId id) {
    return kPragmaMacros[static_cast<int>(id)].name;
}
/// Описание прагмы (для Context::macroDocs / справки).
constexpr const char* pragmaMacroDesc(PragmaMacroId id) {
    return kPragmaMacros[static_cast<int>(id)].desc;
}
/// Поиск идентификатора прагмы по имени (точное совпадение).
inline std::optional<PragmaMacroId> pragmaMacroId(std::string_view name) {
    for (int i = 0; i < static_cast<int>(PragmaMacroId::Count); ++i) {
        if (kPragmaMacros[i].name == name) {
            return static_cast<PragmaMacroId>(i);
        }
    }
    return std::nullopt;
}

/// Выполнение препроцессорных прагм парсера (PragmaCheck/PragmaEval/EvalOptionTrueFalseRaw).
/// Ранее - часть Parser вместе с гигиеническим состоянием (m_hygienic_*) и m_expected.
class PragmaEvaluator {
  public:
    PragmaEvaluator(trust::Context& ctx, const syntax::PredefMacroResolver& predef);

    /// Проверяет, является ли терм прагмой (препроцессорной командой) с учётом того,
    /// что контекст-макросы (@__NAMESPACE__, @__FUNCTION__) прагмами не являются.
    bool pragmaCheck(const TermPtr& term) const;
    /// Выполняет прагму; buffer - буфер макросов (модифицируется: вставка результата).
    bool pragmaEval(const TermPtr& term, SequenceType& buffer);
    /// Сырая обработка @__OPTION_TRUE__/@__OPTION_FALSE__ прямо в GetNextToken:
    /// содержимое после имени флага берётся «сырым» (токены до закрывающей скобки),
    /// без разбивки по запятым и без ParseTerm-пре-парсинга. При срабатывании флага
    /// токены вставляются как есть. Возвращает true, если прагма распознана и обработана.
    bool evalOptionTrueFalseRaw(SequenceType& macroBuf);

    /// Захват встроенного контекст-макроса `@__CHECK_AREA__(<area> [, <behavior>] [, <attr>...])`
    /// прямо в GetNextToken (ДО expandPredefMacro/прагм): вызов стирается из буфера и заменяется
    /// ОДНИМ терм-маркером (TermID::MACRO_CONTEXT, текст "@__CHECK_AREA__", аргументы - дочерние
    /// NAME-термы: [area, behavior?, attr...]). Терм term_to_ast превращает в узел CheckAreaStmt,
    /// семантика валидирует по текущей области и удаляет (кода не даёт). Разбор/валидация области
    /// и поведения строгие (не-поведение во 2-й позиции - ошибка). Возвращает true, если токен
    /// был @__CHECK_AREA__ (независимо от успеха разбора: при ошибке вызов удалён + диагностика).
    bool evalCheckArea(SequenceType& macroBuf);

    /// Сброс кэша гигиенических имён при новом дереве раскрытия макроса (вызывает macro.cpp).
    void clearHygienicNames() { m_hygienic_names.clear(); }

    /// Список ожидаемых токенов, заданный @__PRAGMA_EXPECTED__ (читает GetNextToken).
    const std::vector<std::string>& expected() const { return m_expected; }
    /// Очистка списка ожидаемых токенов (после одноразовой проверки в GetNextToken).
    void clearExpected() { m_expected.clear(); }

    /// Закрывающий try-блока из @__PRAGMA_TRY_CLOSE__ (parser::token_type::TRY_ALL_END/
    /// TRY_MINUS_END/TRY_PLUS_END). END = нет ожидания. GetNextToken конвертирует финальную
    /// '}' в этот токен TRY_*_END.
    parser::token_type tryClose() const { return m_tryClose; }
    /// Очистка (после того как закрывающая '}' была конвертирована).
    void clearTryClose() { m_tryClose = parser::token_type::END; }

  private:
    trust::Context& m_ctx;
    const syntax::PredefMacroResolver& m_predef;
    std::map<std::string, std::string> m_hygienic_names;
    int m_hygienic_counter = 0;
    std::vector<std::string> m_expected;
    parser::token_type m_tryClose = parser::token_type::END; // закрывающий try-блока (END = нет)
};

} // namespace syntax
} // namespace trust
