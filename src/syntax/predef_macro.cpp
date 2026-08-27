#include "syntax/predef_macro.hpp"

#include "syntax/pragma_evaluator.hpp"
#include "syntax/term.h"

#include "diag/context.hpp"
#include "diag/mapper.hpp"
#include "diag/severity.hpp"
#include "trust/version.h"
#include "utils/strings.hpp"

#include "syntax/warning_push.h"
#include <sys/stat.h>
#include "syntax/warning_pop.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <utility>

using namespace trust;

namespace trust {
namespace syntax {

namespace {

// Реестры (имя -> описание), строятся ОДИН раз из x-macro TRUST_VALUE_MACROS / TRUST_CONTEXT_MACROS;
// там же сидируются доки в Context::macroDocs. Все макросы известны на этапе компиляции -
// ручная регистрация не нужна. Разделение: значение-макросы (парсер) vs контекст-макросы
// (информация анализатора, разрешение в NameResolutionPass).
std::map<std::string, std::string>& valueRegistry() {
    static std::map<std::string, std::string> reg = [] {
        std::map<std::string, std::string> m;
        for (int i = 0; i < static_cast<int>(PredefMacroId::Count); ++i) {
            const auto id = static_cast<PredefMacroId>(i);
            m.emplace(predefMacroName(id), predefMacroDesc(id));
            Context::addMacroDoc(predefMacroName(id), predefMacroDesc(id));
        }
        return m;
    }();
    return reg;
}

std::map<std::string, std::string>& contextRegistry() {
    static std::map<std::string, std::string> reg = [] {
        std::map<std::string, std::string> m;
        for (int i = 0; i < static_cast<int>(ContextMacroId::Count); ++i) {
            const auto id = static_cast<ContextMacroId>(i);
            m.emplace(contextMacroName(id), contextMacroDesc(id));
            Context::addMacroDoc(contextMacroName(id), contextMacroDesc(id));
        }
        return m;
    }();
    return reg;
}

// Монотонный счётчик @__COUNTER__ (ранее - static Parser::m_counter).
int& counter() {
    static int c = 0;
    return c;
}

// Хелперы даты/времени (ранее - Parser::GetCurrentDate/GetCurrentTime/...).
std::string currentDate(time_t ts) {
    std::string buf("Jul 27 2012");
    strftime(buf.data(), buf.size(), "%b %e %Y", localtime(&ts));
    return buf;
}

std::string currentTime(time_t ts) {
    std::string buf("07:07:09");
    strftime(buf.data(), buf.size(), "%T", localtime(&ts));
    return buf;
}

std::string currentTimeStamp(time_t ts) {
    std::string result = asctime(localtime(&ts));
    result = result.substr(0, 24); // Remove \n on the end line
    return result;
}

std::string currentTimeStampISO(time_t ts) {
    std::string buf("2011-10-08T07:07:09Z");
    strftime(buf.data(), buf.size(), "%FT%TZ", localtime(&ts));
    return buf;
}

} // namespace

PredefMacroResolver::PredefMacroResolver(trust::Context& ctx)
: m_ctx(ctx) {
    char* source_date_epoch = std::getenv("SOURCE_DATE_EPOCH");
    if (source_date_epoch) {
        std::istringstream iss(source_date_epoch);
        iss >> m_timestamp;
        if (iss.fail() || !iss.eof()) {
            throw ParserError("Error: Cannot parse SOURCE_DATE_EPOCH (%s) as integer", source_date_epoch);
        }
    } else {
        m_timestamp = std::time(NULL);
    }
}
const std::vector<std::string>& PredefMacroResolver::pragmaMacroNames() {
    static const std::vector<std::string> names = [] {
        std::vector<std::string> n;
        for (int i = 0; i < static_cast<int>(PragmaMacroId::Count); ++i) {
            n.emplace_back(pragmaMacroName(static_cast<PragmaMacroId>(i)));
        }
        return n;
    }();
    return names;
}

std::vector<std::string> PredefMacroResolver::predefMacroNames() {
    std::vector<std::string> out;
    const auto& vreg = valueRegistry();
    const auto& creg = contextRegistry();
    out.reserve(vreg.size() + creg.size() + pragmaMacroNames().size());
    for (const auto& [k, v] : vreg) {
        (void)v;
        out.push_back(k);
    }
    for (const auto& [k, v] : creg) {
        (void)v;
        out.push_back(k);
    }
    for (const auto& name : pragmaMacroNames()) {
        out.push_back(name);
    }
    return out;
}

bool PredefMacroResolver::checkPredefMacro(const TermPtr& term) {
    if (term->m_id != TermID::NAME) {
        return false;
    }

    std::string_view text = term->getText();
    if (text.find("@") == 0) {
        text.remove_prefix(1);
    }

    return valueRegistry().find("@" + std::string(text)) != valueRegistry().end();
}

bool PredefMacroResolver::checkContextMacro(const TermPtr& term) {
    if (!term) {
        return false;
    }
    return contextRegistry().find(std::string(term->getText())) != contextRegistry().end();
}

void PredefMacroResolver::expandEmbedPredefMacros(std::string& text, const MapperRange& range) {
    if (range.begin.isInvalid()) {
        return;
    }
    const MapperLocation base = range.begin;
    const size_t n = text.size();
    std::string out;
    out.reserve(n);
    size_t i = 0;
    while (i < n) {
        // Ищем "@__<NAME>__" (значение-макрос), как в обычном коде. Имя после "@__" читается
        // единой утилитой utils::extract_name (вкл. юникод и '::').
        if (i + 3 <= n && text.compare(i, 3, "@__") == 0) {
            const std::string_view name = utils::extract_name(text, i + 3);
            // extract_name расширяется влево и включает ведущий "__" макро-имени: для
            // "@__LINE__" → name = "__LINE__". Имя корректно закрыто "__".
            if (name.size() >= 2 && name[name.size() - 1] == '_' && name[name.size() - 2] == '_') {
                const std::string cand = "@" + std::string(name); // "@__LINE__" и т.п.
                const MapperLocation occ = MapperLocation::makeLoc(base.fileIdx(), base.offset() + i);
                const MapperRange occRange(occ, occ);
                if (valueRegistry().find(cand) != valueRegistry().end()) {
                    // Значение-макрос: строковые (@__FILE__, @__DATE__) → C++-строковый
                    // литерал, целочисленные/имена - как есть.
                    TermPtr t = Term::Create(TermID::MACRO, cand, occRange, parser::token_type::MACRO);
                    expandValueMacro(t);
                    if (t->m_id == TermID::INTEGER || t->m_id == TermID::STRCHAR || t->m_id == TermID::NAME) {
                        if (t->m_id == TermID::STRCHAR) {
                            out += '"';
                            const std::string& v = t->getText();
                            for (const char ch : v) {
                                if (ch == '"' || ch == '\\') {
                                    out += '\\';
                                }
                                out += ch;
                            }
                            out += '"';
                        } else {
                            out += t->getText();
                        }
                        i += cand.size();
                        continue;
                    }
                    m_ctx.diag().report(Severity::Error, occRange, "predefined macro '{}' cannot be expanded inside {{% %}} (context/not-implemented)", cand);
                } else if (contextRegistry().find(cand) != contextRegistry().end()) {
                    // Контекст-макрос: статически вычислимые (@__MODULE_NAME__ → NAME) раскрываются
                    // на парсере; требующие контекста функции/неймспейса (@__FUNCTION__ и др.) в
                    // {% %} не транслируются в C++ - диагностика вместо тихого fallback.
                    TermPtr t = Term::Create(TermID::MACRO, cand, occRange, parser::token_type::MACRO);
                    stampContextMacro(t);
                    if (t->m_id == TermID::INTEGER || t->m_id == TermID::STRCHAR || t->m_id == TermID::NAME) {
                        out += t->getText();
                        i += cand.size();
                        continue;
                    }
                    m_ctx.diag().report(Severity::Error, occRange, "context macro '{}' cannot be expanded inside {{% %}} (requires analyzer context)", cand);
                } else if (std::find(pragmaMacroNames().begin(), pragmaMacroNames().end(), cand) != pragmaMacroNames().end()) {
                    // Прагма-макрос внутри {% %} не раскрывается - диагностика вместо тихого
                    // «ухода в сырой C++».
                    m_ctx.diag().report(Severity::Error, occRange, "pragma macro '{}' cannot be used inside {{% %}}", cand);
                }
            }
        }
        out += text[i];
        ++i;
    }
    text = std::move(out);
}

// Единая точка входа раскрытия предопределённых макросов в потоке токенов (GetNextToken) и
// конкатенации (macro_expander). Диспетчер по реестрам: значение-макрос подставляет значение
// (expandValueMacro), контекст-макрос штампует транзитный маркер для анализатора
// (stampContextMacro), прагму оставляет нетронутой (её ведёт PragmaEvaluator).
parser::token_type PredefMacroResolver::expandPredefMacro(TermPtr& term) {
    if (!term) {
        throw ParserError("Environment variable not defined!");
    }
    if (term->m_id != TermID::MACRO) {
        return term->m_lexer_type;
    }

    const std::string_view text = term->getText();
    if (valueRegistry().find(std::string(text)) != valueRegistry().end()) {
        return expandValueMacro(term);
    }
    if (contextRegistry().find(std::string(text)) != contextRegistry().end()) {
        return stampContextMacro(term);
    }
    // Прагма (@__OPTION__ и т.п.) обрабатывается PragmaEvaluator ниже в GetNextToken - без ошибки.
    if (pragmaMacroId(text).has_value()) {
        return term->m_lexer_type;
    }
    // Неизвестный предопределённый макрос (токен вида @__...__, но не определён ни как
    // значение-макрос, ни как контекст-макрос, ни как прагма) - диагностика вместо тихого пропуска.
    if (looksLikePredefMacro(text)) {
        m_ctx.diag().report(Severity::Error, term->m_mapperRange, "Predef macro '{}' not implemented!", term->toString());
    }
    return term->m_lexer_type;
}

// Раскрывает значение-макрос (фиксированное значение, вычислимое на парсере).
parser::token_type PredefMacroResolver::expandValueMacro(TermPtr& term) {
    const std::string_view text = term->getText();
    const auto idOpt = predefMacroId(text);
    if (!idOpt.has_value()) {
        return term->m_lexer_type;
    }
    const PredefMacroId id = *idOpt;

    const TermID str_type = TermID::STRCHAR;
    const parser::token_type str_token = parser::token_type::STRCHAR;

    switch (id) {
    case PredefMacroId::Counter:
        term->m_id = TermID::INTEGER;
        term->getText() = std::to_string(counter());
        counter()++;
        term->m_lexer_type = parser::token_type::INTEGER;
        return term->m_lexer_type;

    case PredefMacroId::TrustVersionMajor:
    case PredefMacroId::TrustVersionMinor:
    case PredefMacroId::TrustVersionPatch: {
        const int v = id == PredefMacroId::TrustVersionMajor   ? TRUST_VERSION_MAJOR
                      : id == PredefMacroId::TrustVersionMinor ? TRUST_VERSION_MINOR
                                                               : TRUST_VERSION_PATCH;
        term->m_id = TermID::INTEGER;
        term->getText() = std::to_string(v);
        term->m_lexer_type = parser::token_type::INTEGER;
        return term->m_lexer_type;
    }

    case PredefMacroId::TrustVersion:
        term->getText() = TRUST_VERSION;
        term->m_id = str_type;
        term->m_lexer_type = str_token;
        return term->m_lexer_type;

    case PredefMacroId::TrustGitHash:
        term->getText() = TRUST_GIT_HASH;
        term->m_id = str_type;
        term->m_lexer_type = str_token;
        return term->m_lexer_type;

    case PredefMacroId::TrustVersionFull:
        term->getText() = TRUST_VERSION_FULL;
        term->m_id = str_type;
        term->m_lexer_type = str_token;
        return term->m_lexer_type;

    case PredefMacroId::TrustDateBuild:
        term->getText() = TRUST_DATE_BUILD;
        term->m_id = str_type;
        term->m_lexer_type = str_token;
        return term->m_lexer_type;

    case PredefMacroId::Line:
    case PredefMacroId::FileLine:
        EXPECT(!term->m_mapperRange.begin.isInvalid() && "@__LINE__/@__FILE_LINE__: invalid location");
        EXPECT(term->m_mapperRange.begin.offset() > 0 && "@__LINE__/@__FILE_LINE__: zero offset (not mapped)");
        term->m_id = TermID::INTEGER;
        term->getText() = std::to_string(sourceLocation(m_ctx.source(), term->m_mapperRange).line);
        term->m_lexer_type = parser::token_type::INTEGER;
        return term->m_lexer_type;

    case PredefMacroId::File:
    case PredefMacroId::FileName:
        EXPECT(!term->m_mapperRange.begin.isInvalid() && "@__FILE__/@__FILE_NAME__: invalid location");
        EXPECT(!term->m_mapperRange.begin.fileIdx().isInvalid() && "@__FILE__/@__FILE_NAME__: invalid file index");
        term->m_id = str_type;
        term->getText() = sourceLocation(m_ctx.source(), term->m_mapperRange).file;
        term->m_lexer_type = str_token;
        return term->m_lexer_type;

    case PredefMacroId::FileTimestamp:
        term->m_id = str_type;
        {
            std::string fname = std::string(m_ctx.source().filename(term->m_mapperRange.begin));
            struct stat st;
            if (stat(fname.c_str(), &st) == 0) {
                char time_str[64];
                struct tm* timeinfo = localtime(&st.st_mtime);
                strftime(time_str, sizeof(time_str), "%a %b %d %H:%M:%S %Y", timeinfo);
                term->getText() = std::string(time_str, 24);
            } else {
                term->getText() = "??? ??? ?? ??:??:?? ????";
            }
        }
        term->m_lexer_type = str_token;
        return term->m_lexer_type;

    case PredefMacroId::FileMd5:
        term->m_id = str_type;
        {
            auto fileIdx = term->m_mapperRange.begin.fileIdx();
            if (!fileIdx.isInvalid()) {
                uint64_t hash = m_ctx.source().getFileHash(fileIdx);
                char hex[17];
                snprintf(hex, sizeof(hex), "%016llx", static_cast<unsigned long long>(hash));
                term->getText() = std::string(hex);
            } else {
                term->getText() = "?????????????????????????????????";
            }
        }
        term->m_lexer_type = str_token;
        return term->m_lexer_type;

    case PredefMacroId::Date:
        term->getText() = currentDate(m_timestamp);
        term->m_id = str_type;
        term->m_lexer_type = str_token;
        return term->m_lexer_type;

    case PredefMacroId::Time:
        term->getText() = currentTime(m_timestamp);
        term->m_id = str_type;
        term->m_lexer_type = str_token;
        return term->m_lexer_type;

    case PredefMacroId::Timestamp:
        term->getText() = currentTimeStamp(m_timestamp);
        term->getText() = term->getText().substr(0, 24); // Remove trailing newline
        term->m_id = str_type;
        term->m_lexer_type = str_token;
        return term->m_lexer_type;

    case PredefMacroId::TimestampISO:
        term->getText() = currentTimeStampISO(m_timestamp);
        term->m_id = str_type;
        term->m_lexer_type = str_token;
        return term->m_lexer_type;

    case PredefMacroId::RootDir:
        term->m_id = TermID::NAME;
        term->getText() = std::string(m_ctx.source().filename(term->m_mapperRange.begin));
        term->m_lexer_type = parser::token_type::NAME;
        return term->m_lexer_type;

    case PredefMacroId::Count:
        break;
    }

    m_ctx.diag().report(Severity::Error, term->m_mapperRange, "Predef macro '{}' not implemented!", term->toString());
    return term->m_lexer_type;
}

// Штампует контекст-макрос транзитным маркером; значение статически вычислимых (@__MODULE_NAME__)
// подставляет на парсере, остальные разрешает анализатор (ContextMacroExpander, NameResolutionPass).
parser::token_type PredefMacroResolver::stampContextMacro(TermPtr& term) {
    const std::string_view text = term->getText();
    const auto idOpt = contextMacroId(text);
    if (!idOpt.has_value()) {
        return term->m_lexer_type;
    }
    switch (*idOpt) {
    case ContextMacroId::Namespace:
    case ContextMacroId::NamespaceFull:
    case ContextMacroId::Function:
    case ContextMacroId::FuncDName:
    case ContextMacroId::FuncSig:
        term->m_id = TermID::MACRO_CONTEXT;
        term->m_lexer_type = parser::token_type::MACRO_CONTEXT;
        return term->m_lexer_type;

    case ContextMacroId::Class:
    case ContextMacroId::BareNamespace: // @$$ - «внешний блок или функция»
        term->m_id = TermID::NAMESPACE;
        term->m_lexer_type = parser::token_type::NAMESPACE;
        return term->m_lexer_type;

    case ContextMacroId::ModuleName:
        EXPECT(!term->m_mapperRange.begin.isInvalid() && "@__MODULE_NAME__: invalid location");
        EXPECT(!term->m_mapperRange.begin.fileIdx().isInvalid() && "@__MODULE_NAME__: invalid file index");
        term->m_id = TermID::NAME;
        term->getText() = m_ctx.source().moduleName(term->m_mapperRange.begin.fileIdx());
        term->m_lexer_type = parser::token_type::NAME;
        return term->m_lexer_type;

    case ContextMacroId::ModuleFull: // $\
        term->getText() = "";
        term->m_id = TermID::MODULE;
        term->m_lexer_type = parser::token_type::MODULE;
        return term->m_lexer_type;

    case ContextMacroId::Count:
        break;
    }
    return term->m_lexer_type;
}

} // namespace syntax
} // namespace trust
