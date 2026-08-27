#include "syntax/pragma_evaluator.hpp"

#include "syntax/predef_macro.hpp"
#include "syntax/parser.h"
#include "syntax/term.h"

#include "diag/context.hpp"
#include "diag/severity.hpp"

#include "syntax/warning_push.h"
#include "parser.yy.h"
#include "syntax/warning_pop.h"

#include <utility>

using namespace trust;

namespace trust {
namespace syntax {

PragmaEvaluator::PragmaEvaluator(trust::Context& ctx, const syntax::PredefMacroResolver& predef)
: m_ctx(ctx)
, m_predef(predef) {
}

bool PragmaEvaluator::pragmaCheck(const TermPtr& term) const {
    if (term && looksLikePredefMacro(term->getText())) {
        // Контекст-макросы (@__NAMESPACE__, @__FUNCTION__, @__CLASS__, ...) - не прагмы,
        // а значения/имена, раскрываемые анализатором (информация анализатора). Иначе после
        // expandPredefMacro (m_id = MACRO_CONTEXT/NAMESPACE) они попадали бы в ветку прагм и съедались.
        if (term->m_id == TermID::MACRO_CONTEXT) {
            return false;
        }
        // Значение-макросы и контекст-макросы (по имени) прагмами не являются.
        if (m_predef.checkPredefMacro(term)) {
            return false;
        }
        if (m_predef.checkContextMacro(term)) {
            return false;
        }
        return true;
    }
    return false;
}

bool PragmaEvaluator::pragmaEval(const TermPtr& term, SequenceType& buffer) {
    // Директива препроцессора в стиле C `#pragma message` / GCC `_Pragma`:
    //   @__PRAGMA_MESSAGE__("Compiling ", __FILE__, "...")  - информационное сообщение
    //   @__PRAGMA_WARNING__("...")                          - предупреждение
    //   @__PRAGMA_ERROR__("...")                            - ошибка
    //   @__PRAGMA_EXPECTED__("=>", "==>", ...)              - ожидаемый следующий токен
    //   @__OPTION__ / @__OPTION_PUSH__ / @__OPTION_POP__ / @__OPTION_TRUE__/FALSE__/IIF__
    //   @__HYGIENIC__(name)                                 - гигиеническое имя
    ASSERT(term);

    const auto idOpt = pragmaMacroId(term->getText());
    if (!idOpt.has_value()) {
        m_ctx.diag().report(Severity::Error, term->m_mapperRange, "Uknown pragma '{}'", term->toString());
        return true;
    }
    const PragmaMacroId id = *idOpt;
    // Док прагмы - из x-macro (единственный источник описаний, без рассинхрона с кодом).
    Context::addMacroDoc(pragmaMacroName(id), pragmaMacroDesc(id));

    switch (id) {
    case PragmaMacroId::Hygienic:
        if (term->size() != 1) {
            m_ctx.diag().report(Severity::Error, term->m_mapperRange, "@__HYGIENIC__ expects exactly one argument!");
            return false;
        }
        {
            std::string ident(term->at(0).second->getText());
            auto iter = m_hygienic_names.find(ident);
            std::string gen;
            if (iter != m_hygienic_names.end()) {
                gen = iter->second;
            } else {
                m_hygienic_counter++;
                gen = ident + "__G" + std::to_string(m_hygienic_counter) + "_";
                m_hygienic_names.insert({ident, gen});
            }
            buffer.insert(buffer.begin(), Term::Create(TermID::NAME, gen, {}, parser::token_type::NAME));
        }
        return true;

    case PragmaMacroId::Option:
        if (term->size() != 2) {
            m_ctx.diag().report(Severity::Error, term->m_mapperRange, "@__OPTION__ expects exactly two arguments!");
            return false;
        }
        {
            std::string name(term->at(0).second->getText());
            std::string sev_name(term->at(1).second->getText());
            if (m_ctx.opts().isFlagByName(name)) {
                const std::string v = sev_name;
                if (v == "on" || v == "1" || v == "true") {
                    m_ctx.opts().setEnabledByName(name, true);
                } else if (v == "off" || v == "0" || v == "false" || v == "ignore") {
                    m_ctx.opts().setEnabledByName(name, false);
                } else if (!m_ctx.opts().setFlagValueByName(name, v)) {
                    m_ctx.diag().report(Severity::Error, term->m_mapperRange, "Invalid value '{}' for option '{}' in @__OPTION__!", v, name);
                    return false;
                }
                return true;
            }
            if (!m_ctx.opts().isRegisteredByName(name)) {
                m_ctx.diag().report(Severity::Error, term->m_mapperRange, "Unknown option '{}' in @__OPTION__!", name);
                return false;
            }
            auto sev = severityFromName(sev_name);
            if (!sev.has_value() && sev_name != "ignore") {
                m_ctx.diag().report(Severity::Error, term->m_mapperRange, "Unknown status '{}' in @__OPTION__!", sev_name);
                return false;
            }
            m_ctx.opts().setByName(name, sev);
        }
        return true;

    case PragmaMacroId::OptionPush:
        m_ctx.opts().push();
        return true;

    case PragmaMacroId::OptionPop:
        try {
            m_ctx.opts().pop();
        } catch (const std::runtime_error& e) {
            m_ctx.diag().report(Severity::Error, term->m_mapperRange, "{}", e.what());
        }
        return true;

    case PragmaMacroId::OptionTrue:
    case PragmaMacroId::OptionFalse: {
        // Доки обеих веток сидируются и здесь (fallback), и в evalOptionTrueFalseRaw (реальный путь).
        Context::addMacroDoc(pragmaMacroName(PragmaMacroId::OptionTrue), pragmaMacroDesc(PragmaMacroId::OptionTrue));
        Context::addMacroDoc(pragmaMacroName(PragmaMacroId::OptionFalse), pragmaMacroDesc(PragmaMacroId::OptionFalse));

        if (term->size() < 1) {
            m_ctx.diag().report(Severity::Error, term->m_mapperRange, "{} expects at least one argument (option flag name)!", term->getText());
            return false;
        }
        std::string flag(term->at(0).second->getText());
        if (!m_ctx.opts().isFlagByName(flag)) {
            m_ctx.diag().report(Severity::Error, term->m_mapperRange, "Unknown option flag '{}' in {}!", flag, term->getText());
            return false;
        }
        const bool enabled = m_ctx.opts().isEnabledByName(flag);
        const bool fire = (id == PragmaMacroId::OptionTrue) ? enabled : !enabled;
        if (fire) {
            SequenceType block;
            block.reserve(static_cast<size_t>(term->size() - 1));
            for (int i = 1; i < term->size(); i++) {
                TermPtr t = term->at(i).second;
                if (t) {
                    t->m_mapperRange = term->m_mapperRange;
                    block.push_back(t);
                }
            }
            buffer.insert(buffer.begin(), block.begin(), block.end());
        }
        return true;
    }

    case PragmaMacroId::OptionIIf: {
        if (term->size() != 3) {
            m_ctx.diag().report(Severity::Error, term->m_mapperRange, "{} expects exactly three arguments (option flag name, true, false)!", term->getText());
            return false;
        }
        std::string flag(term->at(0).second->getText());
        if (!m_ctx.opts().isFlagByName(flag)) {
            m_ctx.diag().report(Severity::Error, term->m_mapperRange, "Unknown option flag '{}' in {}!", flag, term->getText());
            return false;
        }
        const bool enabled = m_ctx.opts().isEnabledByName(flag);
        const int idx = enabled ? 1 : 2;
        TermPtr t = term->at(idx).second;
        if (t) {
            t->m_mapperRange = term->m_mapperRange;
            buffer.insert(buffer.begin(), t);
        }
        return true;
    }

    case PragmaMacroId::PragmaMessage:
    case PragmaMacroId::PragmaWarning:
    case PragmaMacroId::PragmaError: {
        // Сообщение прагмы = конкатенация текстов всех аргументов (место диагностики - сама прагма).
        std::string message;
        for (int i = 0; i < term->size(); i++) {
            message += term->at(i).second->getText();
        }
        if (id == PragmaMacroId::PragmaMessage) {
            m_ctx.diag().report(Severity::Note, term->m_mapperRange, "{}", message);
        } else if (id == PragmaMacroId::PragmaWarning) {
            m_ctx.diag().report(Severity::Warning, term->m_mapperRange, "{}", message);
        } else {
            m_ctx.diag().report(Severity::Error, term->m_mapperRange, "{}", message);
        }
        break;
    }

    case PragmaMacroId::PragmaExpected:
        if (term->size() == 0) {
            m_ctx.diag().report(Severity::Error, term->m_mapperRange, "@__PRAGMA_EXPECTED__ expects at least one expected token!");
            return false;
        }
        m_expected.clear();
        for (int i = 0; i < term->size(); i++) {
            m_expected.push_back(std::string(term->at(i).second->getText()));
        }
        return true;

    case PragmaMacroId::PragmaDoc:
        // Переопределение/установка документирующего комментария макроса (ЕДИНОЕ хранилище
        // Context::macroDocs, ключ = первый терм без '@'): @__PRAGMA_DOC__("имя", "текст").
        // setMacroDoc переопределяет только УЖЕ СУЩЕСТВУЮЩИЙ макрос; если ключа нет - error.
        if (term->size() != 2) {
            m_ctx.diag().report(Severity::Error, term->m_mapperRange, "@__PRAGMA_DOC__ expects exactly two arguments (macro name, documentation)!");
            return false;
        }
        {
            const std::string name = term->at(0).second->getText();
            if (!Context::setMacroDoc(name, term->at(1).second->getText())) {
                m_ctx.diag().report(Severity::Error, term->m_mapperRange, "@__PRAGMA_DOC__: unknown macro '{}' (no doc to override)", name);
            }
        }
        return true;

    case PragmaMacroId::Count:
        break;
    }
    return true;
}

bool PragmaEvaluator::evalOptionTrueFalseRaw(SequenceType& macroBuf) {
    SequenceType& buf = macroBuf;
    const std::string ptext = buf[0]->getText();
    if (ptext != "@__OPTION_TRUE__" && ptext != "@__OPTION_FALSE__") {
        return false; // обрабатываем только TRUE/FALSE; остальные - через ParseTerm/pragmaEval
    }
    // Доки сидируются здесь (реальный обработчик TRUE/FALSE), а не только в fallback-ветке
    // pragmaEval - иначе при успешном «сыром» разборе ветка pragmaEval не достигается.
    Context::addMacroDoc(pragmaMacroName(PragmaMacroId::OptionTrue), pragmaMacroDesc(PragmaMacroId::OptionTrue));
    Context::addMacroDoc(pragmaMacroName(PragmaMacroId::OptionFalse), pragmaMacroDesc(PragmaMacroId::OptionFalse));
    if (buf.size() < 4 || buf[1]->getText() != "(") {
        return false; // не распознан «сырой» синтаксис - обработка через ParseTerm/pragmaEval
    }
    const trust::MapperRange prange = buf[0]->m_mapperRange;
    std::string flag = buf[2]->getText();

    size_t skip;
    try {
        skip = Parser::SkipBrackets(buf, 1); // токены от '(' до ')' включительно; buf[skip] = ')'
    } catch (const ParserError&) {
        return false;
    }
    if (skip >= buf.size()) {
        return false;
    }

    size_t content_begin = 3;
    if (content_begin < skip && buf[content_begin]->getText() == ",") {
        content_begin++; // съём ведущей запятой (старая форма "@flag", <lex>)
    }

    // «Сырое» содержимое между именем флага и закрывающей скобкой. Каждому токену
    // присваивается range сайта вызова прагмы (prange) - как при раскрытии остальных
    // макросов/прагм (t->m_mapperRange = term->m_mapperRange). Это нужно, чтобы
    // преdef-макросы и управляющие конструкции (напр. FOLLOW) внутри содержимого имели
    // единый call-site range (иначе маппер падает, а локация «запекается» к dsl.src).
    SequenceType content;
    for (size_t i = content_begin; i < skip; i++) {
        TermPtr t = buf[i];
        if (t) {
            t->m_mapperRange = prange;
            content.push_back(t);
        }
    }

    // Стереть саму прагму @__OPTION_*(...) из буфера.
    buf.erase(buf.begin(), buf.begin() + skip + 1);

    if (flag.empty()) {
        m_ctx.diag().report(Severity::Error, prange, "{} expects option flag name!", ptext);
    }
    if (!m_ctx.opts().isFlagByName(flag)) {
        m_ctx.diag().report(Severity::Error, prange, "Unknown option flag '{}' in {}!", flag, ptext);
    }

    const bool enabled = m_ctx.opts().isEnabledByName(flag);
    const bool fire = (ptext == "@__OPTION_TRUE__") ? enabled : !enabled;

    if (fire) {
        buf.insert(buf.begin(), content.begin(), content.end());
    }
    return true;
}

} // namespace syntax
} // namespace trust
