#include "syntax/pragma_evaluator.hpp"

#include "syntax/predef_macro.hpp"
#include "syntax/parser.h"
#include "syntax/term.h"

#include "ast/check_area.hpp"
#include "session/context.hpp"
#include "diag/severity.hpp"
#include "utils/trace.hpp"
#include "utils/trace_options.hpp"

#include "syntax/warning_push.h"
#include "parser.yy.h"
#include "syntax/warning_pop.h"

#include <utility>

using namespace trust;

namespace trust {
namespace syntax {

namespace {

/// Снятие обрамляющих кавычек у аргумента-строки ("area"/'area').
std::string stripQuotes(std::string_view t) {
    if (t.size() >= 2 && t.front() == t.back() && (t.front() == '"' || t.front() == '\'')) {
        return std::string(t.substr(1, t.size() - 2));
    }
    return std::string(t);
}

/// Разбор сырых токенов буфера [begin, close) на аргументы по ',' (строковые аргументы приходят
/// единым токеном, поэтому ',' внутри строки не режет). Кавычки снимаются.
std::vector<std::string> splitRawArgs(const SequenceType& buf, size_t begin, size_t close) {
    std::vector<std::string> args;
    std::string cur;
    bool any = false;
    for (size_t i = begin; i < close; ++i) {
        const std::string& t = buf[i]->getText();
        if (t == ",") {
            if (any) {
                args.push_back(stripQuotes(cur));
                cur.clear();
                any = false;
            }
        } else {
            cur += t;
            any = true;
        }
    }
    if (any) {
        args.push_back(stripQuotes(cur));
    }
    return args;
}

} // namespace

PragmaEvaluator::PragmaEvaluator(trust::Context& ctx, const syntax::PredefMacroResolver& predef)
: m_ctx(ctx)
, m_predef(predef) {
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

bool PragmaEvaluator::evalCheckArea(SequenceType& buf) {
    if (buf.empty()) {
        return false;
    }
    const std::string& name = buf[0]->getText();
    // Наш встроенный маркер обрабатываем только в «сыром» виде. Уже сформированный нами
    // терм-маркер имеет m_id = MACRO_CONTEXT - его повторно не захватываем (иначе бесконечный
    // вызов: после вставки маркер сам оказывается во главе буфера).
    if (name != "@__CHECK_AREA__" || buf[0]->m_id == TermID::MACRO_CONTEXT) {
        return false;
    }
    // Док контекст-макроса сидируется из x-macro-реестра (единый источник описаний).
    Context::addMacroDoc(contextMacroName(ContextMacroId::CheckArea), contextMacroDesc(ContextMacroId::CheckArea));

    const MapperRange prange = buf[0]->m_mapperRange;
    // Аргумент обязан быть обрамлён скобками: @__CHECK_AREA__( <area> [, <behavior>] [, <attr>...] ).
    if (buf.size() < 2 || buf[1]->getText() != "(") {
        m_ctx.diag().report(Severity::Error, prange, "@__CHECK_AREA__ expects arguments: ( <area> [, <behavior>] [, <attr>...] )");
        buf.erase(buf.begin()); // съедаем имя, чтобы не сыпать вторичных ошибок грамматики
        return true;
    }
    size_t close = 0;
    try {
        close = Parser::SkipBrackets(buf, 1); // токены от '(' до ')' включительно; buf[close] = ')'
    } catch (const ParserError&) {
        m_ctx.diag().report(Severity::Error, prange, "@__CHECK_AREA__: unbalanced '(' - closing ')' not found");
        buf.erase(buf.begin());
        return true;
    }
    if (close >= buf.size() || buf[close]->getText() != ")") {
        buf.erase(buf.begin()); // defensive: ')' не найден корректно
        return true;
    }

    // Диапазон всего вызова (для корректной локации маркера в диагностике семантики).
    const MapperRange callRange(buf[1]->m_mapperRange.begin, buf[close]->m_mapperRange.end);

    // Разбор аргументов из сырых токенов между '(' и ')' (разделитель - ',').
    std::vector<std::string> args = splitRawArgs(buf, 2, close);

    // Стереть сам вызов @__CHECK_AREA__(...) (токены 0..close включительно).
    buf.erase(buf.begin(), buf.begin() + close + 1);

    // Валидация области (1-й аргумент) и поведения (2-й, если задан). Ошибки строгие.
    if (args.empty() || args[0].empty()) {
        m_ctx.diag().report(Severity::Error, prange, "@__CHECK_AREA__: missing required area argument");
        return true;
    }
    if (!areaKindFromString(args[0]).has_value()) {
        m_ctx.diag().report(Severity::Error, prange, "@__CHECK_AREA__: unknown area '{}'", args[0]);
        return true;
    }
    if (args.size() >= 2) {
        const std::string& b = args[1];
        if (b != "default" && b != "ignore" && b != "warning" && b != "error") {
            m_ctx.diag().report(Severity::Error, prange, "@__CHECK_AREA__: second argument must be one of 'default|ignore|warning|error', got '{}'", b);
            return true;
        }
    }

    // Единый терм-маркер (TermID::MACRO_CONTEXT - statement-позиция уже допустима в грамматике
    // через rval_name): аргументы - дочерние NAME-термы [area, behavior?, attr...]. term_to_ast
    // по тексту "@__CHECK_AREA__" построит узел CheckAreaStmt.
    TermPtr marker = Term::Create(TermID::MACRO_CONTEXT, "@__CHECK_AREA__", callRange, parser::token_type::MACRO_CONTEXT);
    for (const auto& a : args) {
        marker->m_sequence.push_back(Term::Create(TermID::NAME, a, callRange, parser::token_type::NAME));
    }
    buf.insert(buf.begin(), std::move(marker));
    return true;
}

bool PragmaEvaluator::evalDebug(SequenceType& buf) {
    if (buf.empty()) {
        return false;
    }
    const std::string name = buf[0]->getText();
    // Уже сформированный нами терм-маркер (m_id = MACRO_CONTEXT) повторно не захватываем: иначе
    // после вставки маркер сам оказывается во главе буфера и разбор зацикливается/ломается.
    if ((name != "@__DEBUG__" && name != "@__DEBUG_SCOPE__") || buf[0]->m_id == TermID::MACRO_CONTEXT) {
        return false;
    }
    const bool isScope = (name == "@__DEBUG_SCOPE__");

    Context::addMacroDoc(contextMacroName(ContextMacroId::Debug), contextMacroDesc(ContextMacroId::Debug));
    Context::addMacroDoc(contextMacroName(ContextMacroId::DebugScope), contextMacroDesc(ContextMacroId::DebugScope));

    const trust::MapperRange prange = buf[0]->m_mapperRange;
    const char* expected = isScope ? "( [<name-mask>...] [, key=value...] )" : "( <masks> ) or ( )";

    // Аргумент обязан быть обрамлён скобками.
    if (buf.size() < 2 || buf[1]->getText() != "(") {
        m_ctx.diag().report(Severity::Error, prange, "{} expects arguments: {}", name, expected);
        buf.erase(buf.begin()); // съедаем имя, чтобы не сыпать вторичных ошибок грамматики
        return true;
    }
    size_t close = 0;
    try {
        close = Parser::SkipBrackets(buf, 1); // токены от '(' до ')' включительно; buf[close] = ')'
    } catch (const ParserError&) {
        m_ctx.diag().report(Severity::Error, prange, "{}: unbalanced '(' - closing ')' not found", name);
        buf.erase(buf.begin());
        return true;
    }
    if (close >= buf.size() || buf[close]->getText() != ")") {
        buf.erase(buf.begin()); // defensive: ')' не найден корректно
        return true;
    }
    const MapperRange callRange(buf[1]->m_mapperRange.begin, buf[close]->m_mapperRange.end);

#if TRUST_TRACE_ENABLED
    // Разбор аргументов из сырых токенов между '(' и ')' (разделитель - ','). Строковые
    // аргументы приходят единым токеном (кавычки внутри), поэтому ',' внутри строки не режет.
    std::vector<std::string> args = splitRawArgs(buf, 2, close);

    // Стереть сам вызов (токены 0..close включительно).
    buf.erase(buf.begin(), buf.begin() + close + 1);

    if (isScope) {
        // Позиционные аргументы - ФИЛЬТР ИМЁН (маски, можно несколько; пусто - полный дамп),
        // затем именованные опции `key=value` (значения без кавычек: level=all, types=on, max=10).
        // Имена/значения опций валидируются ЗДЕСЬ (на парсинге макроса); при ошибке выдаём полный
        // список поддерживаемых опций с допустимыми значениями.
        std::size_t namedCount = 0;
        for (const std::string& a : args) {
            if (a.find('=') == std::string::npos) { // позиционный: маска имени
                if (namedCount != 0) {
                    m_ctx.diag().report(Severity::Error, prange,
                                        "@__DEBUG_SCOPE__: name masks must precede named options (supported options: {})",
                                        trust::trace::scopeDumpOptionsUsage());
                    return true;
                }
                if (a.empty()) {
                    m_ctx.diag().report(Severity::Error, prange, "@__DEBUG_SCOPE__: empty name mask");
                    return true;
                }
                continue;
            }
            ++namedCount;
            trust::trace::ScopeDumpOptionId optId{};
            std::string_view value;
            std::string optErr;
            if (!trust::trace::parseScopeDumpOption(a, optId, value, optErr)) {
                m_ctx.diag().report(Severity::Error, prange, "@__DEBUG_SCOPE__: {} (supported options: {})", optErr,
                                    trust::trace::scopeDumpOptionsUsage());
                return true;
            }
        }
    } else if (args.size() > 1) {
        m_ctx.diag().report(Severity::Error, prange, "@__DEBUG__ expects arguments: {}", expected);
        return true;
    }

    TermPtr marker = Term::Create(TermID::MACRO_CONTEXT, name, callRange, parser::token_type::MACRO_CONTEXT);
    for (const auto& a : args) {
        marker->m_sequence.push_back(Term::Create(TermID::NAME, a, callRange, parser::token_type::NAME));
    }
    buf.insert(buf.begin(), std::move(marker));
    return true;
#else
    // Релизная сборка компилятора: отладочный вывод недоступен - вызов стирается, вместо вывода
    // выдаётся предупреждение (маркер/узел AST не создаётся).
    buf.erase(buf.begin(), buf.begin() + close + 1);
    m_ctx.diag().report(Severity::Warning, prange, "{}: debug output is not available in a release build", name);
    (void)callRange;
    return true;
#endif
}

} // namespace syntax
} // namespace trust
