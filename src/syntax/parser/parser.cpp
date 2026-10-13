#include "syntax/parser.h"
#include "syntax/lexer.h"
#include "syntax/macro.h"
#include "session/context.hpp"
#include "session/module_resolver.hpp"
#include "location/module_name.hpp"
#include "location/module_path.hpp"

#include "syntax/util/warning_push.h"
#include <sys/stat.h>
#include "syntax/util/warning_pop.h"

#include "syntax/term/term.h"
#include "trust/version.h"
#include "diag/registry.hpp"
#include "syntax/util/diag.hpp"
#include "utils/strings.hpp"

#include <algorithm>
#include <optional>
#include <utility>

namespace trust {

// -- Пер-компонентная регистрация диагностик синтаксиса (см. diag/registry.hpp) --
// Регистрирует на static-init severity-диагностику, которой владеет компонент syntax.
namespace {
struct SyntaxDiagnosticsRegistrar {
    SyntaxDiagnosticsRegistrar() {
        registerDiagnostics([](Options& opts) {
            opts.add(syntax::DiagId::MacroRedefined);
            opts.add(syntax::DiagId::DslMacroRedefined);
        });
    }
};
const SyntaxDiagnosticsRegistrar kSyntaxDiagnostics;
} // namespace

// @__PRAGMA_TRY_CLOSE__ уже резолвит закрывающий в parser::token_type (TRY_*_END) в pragmaEval;
// здесь отдельного маппинга не нужно.

// -- Хелперы грамматики для документирующих комментариев (doc_list / DOCUMENT_INLINE) --
// Forward-объявлены в прологе parser.y; вызываются из действий грамматики
// (правила doc_list/expression/sequence).

// Распаковка doc_list: сам док + его m_sequence (если он был бандлом из нескольких доков),
// порядок сохраняется. Для одиночного DOCUMENT m_sequence пуст, поэтому возвращается [док].
std::vector<TermPtr> flattenDocs(TermPtr docs) {
    std::vector<TermPtr> out;
    out.reserve(1 + docs->m_sequence.size());
    out.push_back(docs);
    out.insert(out.end(), docs->m_sequence.begin(), docs->m_sequence.end());
    docs->m_sequence.clear();
    return out;
}

// SEQUENCE из доков + опционального statement (valid m_mapperRange: [первый док, stmt/последний док]).
TermPtr makeDocBundle(TermPtr docs, TermPtr stmt) {
    auto all = flattenDocs(docs);
    if (stmt) {
        all.push_back(stmt);
    }
    auto seq = Term::Create(TermID::SEQUENCE, "");
    seq->m_sequence = std::move(all);
    MapperLocation end = stmt ? stmt->m_mapperRange.end : (seq->m_sequence.empty() ? docs->m_mapperRange.end : seq->m_sequence.back()->m_mapperRange.end);
    seq->m_mapperRange = MapperRange{docs->m_mapperRange.begin, end};
    return seq;
}

// Добавляет доки в конец m_sequence контейнера (последовательности).
void appendDocs(TermPtr seq, TermPtr docs) {
    auto all = flattenDocs(docs);
    seq->m_sequence.insert(seq->m_sequence.end(), all.begin(), all.end());
}

// Привязывает trust-контракты (trust_pre/trust_post/@{}), собранные trust_cond_seq, к терму-имени
// объявления (в его m_sequence) - ЕДИНАЯ точка для функций, методов и операторов (assign_seq,
// type_def_seq, class_member, free-оператор). conds == nullptr (контрактов нет) - ничего не делаем;
// голова-конд приходит как $2, остальные - в его m_sequence.
void appendTrustConds(TermPtr name, TermPtr conds) {
    if (!name || !conds) {
        return;
    }
    auto& seq = name->m_sequence;
    seq.push_back(conds);
    seq.insert(seq.end(), conds->m_sequence.begin(), conds->m_sequence.end());
}

TermPtr appendTypeSetMember(TermPtr set, std::string_view sign, TermPtr type) {
    EXPECT(type && "appendTypeSetMember: null member type");
    TermPtr result = set;
    if (!result || result->getTermID() != TermID::TYPE_SET) {
        // Первый оператор: исходный тип становится началом первой ветки (знак '+').
        result = Term::Create(TermID::TYPE_SET, "", type->m_mapperRange);
        TermPtr first = Term::Create(TermID::ARGUMENT, "", set ? set->m_mapperRange : type->m_mapperRange);
        first->m_left = Term::CreateSymbol('+');
        first->m_type = set;
        result->m_sequence.push_back(first);
    }
    TermPtr member = Term::Create(TermID::ARGUMENT, "", type->m_mapperRange);
    member->m_left = Term::CreateSymbol(sign == "+" ? '+' : '-');
    member->m_type = type;
    result->m_sequence.push_back(member);
    if (!type->m_mapperRange.isInvalid()) {
        result->m_mapperRange.end = type->m_mapperRange.end;
    }
    return result;
}

// Истина, если терм - объявление (для привязки документирующего комментария к самому терму).
bool isDeclTerm(const Term& t) {
    switch (t.m_id) {
    case TermID::CREATE_NAME: // := → VarDecl (или FuncDecl по сигнатуре в m_left)
        // Деструктуризация `a, b := source` - НЕ одиночное объявление: док остаётся sibling-узлом.
        return !(t.m_left && t.m_left->m_left);
    case TermID::CREATE_TYPE: // ::= → TypeDecl
    case TermID::TYPE_DEFINE: // =   → TypeDecl (определение ранее объявленного типа)
    case TermID::FUNCTION:    // FuncDecl
    case TermID::LAMBDA:      // FuncDecl (лямбда-выражение)
    case TermID::ITERATOR:    // FuncDecl
    case TermID::ARGS:        // ArgNode
    case TermID::ARGUMENT:    // ArgNode
        return true;
    default:
        return false;
    }
}

// Хвостовой док (`sequence DOCUMENT_INLINE`): если последний statement - объявление,
// пишем док прямо в терм-идентификатор (m_docs); иначе - отдельным sibling-узлом.
// Для одиночного statement терм sequence равен самому терму (не SEQUENCE-обёртке), поэтому
// last = сам терм; для multi-statement sequence - последний элемент m_sequence.
void attachTrailingDoc(TermPtr seq, TermPtr docs) {
    TermPtr last = seq;
    if (last && last->m_id == TermID::SEQUENCE && !last->m_sequence.empty()) {
        last = last->m_sequence.back();
    }
    if (last && isDeclTerm(*last)) {
        last->m_docs.push_back(std::move(docs));
    } else {
        appendDocs(seq, docs);
    }
}

// Ведущий док (`doc_list expression`): если выражение - объявление, цепляем док на терм
// (m_docs); иначе возвращаем bundle (отдельный sibling-узел). Возвращает результат выражения.
// Многострочные ведущие доки редуцируются вложенно (`doc_list expression`): внешний док
// приходит ПОЗЖЕ, поэтому ведущие вставляем В НАЧАЛО m_docs, сохраняя порядок исходника.
TermPtr attachLeadingDoc(TermPtr docs, TermPtr stmt) {
    if (stmt && isDeclTerm(*stmt)) {
        auto all = flattenDocs(docs);
        stmt->m_docs.insert(stmt->m_docs.begin(), all.begin(), all.end());
        return stmt;
    }
    return makeDocBundle(docs, std::move(stmt));
}

} // namespace trust

namespace trust {

Parser::Parser(trust::Context& ctx, PostLexerType* postlex, bool pragma_enable, bool macro_expand)
: m_ctx(ctx)
, m_predef(ctx)
, m_pragma(m_ctx, m_predef) {
    m_is_lexer_complete = false;

    m_macro = macro_expand ? ctx.macro() : nullptr;
    m_postlex = postlex;
    m_no_macro = false;
    m_enable_pragma = pragma_enable;
    //    m_name_module = "\\\\__main__";

    m_ast = nullptr;
}

TermPtr Parser::ParseText(std::string_view text, std::string_view sourceName, bool expand_module) {
    // Парсит «голый» текстовый фрагмент под фиктивным именем источника (например,
    // DSL-макросы под именем "@stdlib/dsl"). Фиктивные имена помечаются префиксом '@'
    // (in-memory, файла на диске нет). Для реальных файлов/модулей используйте
    // ParseWithSource(зарегистрированный MapperFile).
    // Явное std::string(...) обязательно: неявное string_view -> string не
    // компилируется в GCC 14 / C++23 для by-value параметров add_source.
    trust::MapperFile src = m_ctx.source().add_source(std::string(sourceName), std::string(text));
    return ParseWithSource(src, expand_module);
}

// Парсит из уже зарегистрированного source-файла (не создавая псевдо-источник
// с префиксом '@' / in-memory). Используется модульным загрузчиком для главного
// файла/модуля, чтобы маппинги (mapStart/mapStop) привязывались к реальному
// файлу, а не к новому одноимённому входу - иначе source map невозможно
// сопоставить с исходным .src.
TermPtr Parser::ParseWithSource(trust::MapperFile src, bool expand_module, std::size_t skipPrefix, bool fileInterface) {
    m_expand_module = expand_module;
    m_ast = Term::Create(TermID::END, "");
    //    m_ast->SetSource(std::make_shared<std::string>(input));

    // Режим блока интерфейса: раскрытие макросов модуля запрещено до конца первого top-level
    // `block` (снимает грамматика, leaveFileInterfaceIfAt). `skipPrefix` 0-based, диапазоны
    // термов 1-based.
    // Режим интерфейса: первая значимая конструкция — его блок (снимается грамматикой).
    m_inFileInterface = fileInterface;
    m_fileInterfaceBegin = fileInterface ? skipPrefix + 1 : 0;
    // Отложенная диагностика запрета макроса: выдаётся только если блок интерфейса разобран.
    m_pendingInterfaceMacro.reset();

    // Сброс отслеживания токенов между разборами (Parser переиспользуется).
    m_recent.reset();
    // Сброс состояния восстановления после синтаксических ошибок.
    m_lastSyntaxErrorRange.reset();

    Scanner scanner(m_ctx, src, skipPrefix);

    lexer = &scanner;

    parser parser(*this);
    if (parser.parse() != 0) {
        lexer = nullptr;
        m_inFileInterface = false;
        return m_ast;
    }

    // Исходники требуются для вывода информации ошибках во время анализа типов
    lexer = nullptr;
    m_inFileInterface = false;
    return m_ast;
}

bool Parser::forbidFileInterfaceMacro(const TermPtr& callTerm) {
    if (!m_inFileInterface) {
        return false;
    }
    EXPECT(callTerm != nullptr && "forbidFileInterfaceMacro: null macro call term");
    EXPECT(m_macro != nullptr && "forbidFileInterfaceMacro: macro registry is not available");
    // Макрос базового скоупа (DSL) определён вне модулей - его раскрытие не зависит от импортёра.
    // Базовый скоуп неизменяем из файлов (`Macro::isDslScopeGroup`), поэтому проверка по ключу
    // группы точна: тело базового макроса всегда пришло из DSL/`--dsl`.
    TermPtr call = callTerm;
    if (m_macro->isBaseScopeGroup(m_macro->toMacroHash(call))) {
        return false;
    }
    // Диагностика ОТЛОЖЕНА до успешного разбора блока: при незакрытом интерфейсе блок не свёрнут,
    // интерфейса нет — иначе ошибки в ТЕЛЕ файла получали бы ложный запрет (каскад).
    if (!m_pendingInterfaceMacro.has_value()) {
        m_pendingInterfaceMacro = PendingInterfaceMacro{callTerm->m_mapperRange, std::string(callTerm->getText())};
    }
    return true;
}

void Parser::reportPendingInterfaceMacro() {
    if (!m_pendingInterfaceMacro.has_value()) {
        return;
    }
    const PendingInterfaceMacro pending = std::move(*m_pendingInterfaceMacro);
    m_pendingInterfaceMacro.reset();
    m_ctx.diag().report(Severity::Error, pending.range, "macro '{}' cannot be used in the file interface: it is defined in a module", pending.name);
}

trust::MapperRange RecentTokens::syntaxErrorRange(const trust::Context& ctx) const {
    trust::MapperRange r;
    if (!prev.end.isInvalid() && !cur.begin.isInvalid() && cur.begin.offset() >= prev.end.offset()) {
        // Пропущенный разделитель (';', ',' и т.п.) - в промежутке [конец последнего
        // корректного токена, начало неожиданного]. Подчёркиваем промежуток только если
        // он в пределах одной строки; иначе (напр. пропущенный ';' перед декларацией с
        // ведущим ##-комментарием) он растянулся бы на несколько строк - указываем точкой
        // на место пропущенного разделителя (конец последнего токена).
        const auto sameLine = ctx.source().line_column(prev.end).line == ctx.source().line_column(cur.begin).line;
        if (sameLine) {
            r = trust::MapperRange(prev.end, cur.begin);
        } else {
            r = trust::MapperRange(prev.end, prev.end);
        }
    } else if (!cur.begin.isInvalid()) {
        r = trust::MapperRange(cur.begin, cur.begin);
    }
    // cur невалиден - не должно происходить (error() вызывается, когда bison уже получил
    // lookahead через GetNextToken). Возвращаем невалидный range; вызывающий не выведет.
    return r;
}

void Parser::error(const std::string& m) {
    // Позиция ошибки - из токенов, реально потреблённых bison (RecentTokens), а не из
    // flex-курсора, который при упреждающей буферизации «убегает» вперёд. Невалидный
    // диапазон не отбрасывает диагностику: она выводится без локации.
    const auto r = m_recent.syntaxErrorRange(m_ctx);
    // Каскад в одной точке (Bison может вызвать error несколько раз до восстановления) -
    // не дублируем диагностику с тем же диапазоном подряд.
    if (m_lastSyntaxErrorRange && *m_lastSyntaxErrorRange == r) {
        return;
    }
    m_lastSyntaxErrorRange = r;
    // Лимит и итоговое сообщение ("too many errors ...") — обязанность DiagnosticEngine:
    // после достижения лимита он сам подавляет дальнейшие не-Fatal диагностики.
    m_ctx.diag().report(Severity::Error, r, "{}", m);
}

bool Parser::syntaxErrorLimitReached() const {
    return m_ctx.diag().errorLimitReached();
}

void trust::parser::error(const std::string& msg) {
    driver.error(msg);
}

TermPtr Parser::GetAst() {
    return m_ast;
}

void Parser::AstAddTerm(TermPtr term) {
    ASSERT(m_ast);
    if (m_ast->m_id == TermID::END) {
        m_ast = term;
    } else {
        if (m_ast->m_id != TermID::SEQUENCE) {
            TermPtr temp = Term::Create(TermID::SEQUENCE, "", m_ast->m_mapperRange);
            m_ast.swap(temp);
            m_ast->m_sequence.push_back(temp);
        }
        if (term->m_id == TermID::SEQUENCE) {
            m_ast->m_sequence.insert(m_ast->m_sequence.end(), term->m_sequence.begin(), term->m_sequence.end());
        } else {
            m_ast->m_sequence.push_back(term);
        }
    }
}

__attribute__((weak)) TermPtr ProcessMacro(Parser&, TermPtr& term) {
    return term;
}

__attribute__((weak)) ExpandMacroResult ExpandTermMacro(Parser& parser) {
    return ExpandMacroResult::Break;
}

TermPtr Parser::ParseTerm(const char* proto, trust::Context& ctx, bool pragma_enable, bool macro_expand) {
    try {
        // Термин или термин + тип парсятся без ошибок
        Parser p(ctx, nullptr, pragma_enable, macro_expand);
        return p.ParseText(proto);
    } catch (std::exception&) {
        std::string func(proto);
        try {
            func += ":={}";
            Parser p(ctx, nullptr, pragma_enable, macro_expand);
            return p.ParseText(func)->m_left;
        } catch (std::exception& e) {
            throw ParserError("Fail parsing prototype '%s' as '%s'!", func.c_str(), e.what());
        }
    }
}

size_t Parser::SkipBrackets(const SequenceType& buffer, const size_t offset) {

    if (offset >= buffer.size()) {
        return 0;
    }

    std::string br_end;
    if (buffer[offset]->getText().compare("(") == 0) {
        br_end = ")";
    } else if (buffer[offset]->getText().compare("[") == 0) {
        br_end = "]";
    } else {
        return 0;
    }

    size_t shift = 1;
    int count = 1;
    while (offset + shift < buffer.size()) {
        if (buffer[offset]->getText().compare(buffer[offset + shift]->getText()) == 0) {
            count++; // Next level bracket
        } else if (br_end.compare(buffer[offset + shift]->getText()) == 0) {
            count--; // // Leave level bracket
            if (count == 0) {
                return shift + 1;
            }
        }
        shift++;
    }
    throw ParserError("Closed bracket '%s' not found!", br_end.c_str());
}

size_t Parser::ParseTerm(TermPtr& result, const SequenceType& buffer, trust::Context& ctx, size_t offset, bool pragma_enable, bool macro_expand) {

    if (offset >= buffer.size()) {
        throw ParserError("Fail skip count %d or buffer size %d!", static_cast<int>(offset), static_cast<int>(buffer.size()));
    }

    /* term
     * func()
     *
     * term: type
     * func(): type
     *
     * term: type[]
     * func(): type[]
     *
     */

    std::string source = buffer[offset]->toString();
    offset++;
    size_t skip = SkipBrackets(buffer, offset);

    if (skip) {
        /*
         * term
         * func()
         */
        for (size_t i = 0; i < skip; i++) {
            if (buffer[offset + i]) {
                source += buffer[offset + i]->toString();
            }
        }
        offset += skip;
    }

    if (offset + 1 < buffer.size() && buffer[offset + 1]->getText().compare(":") == 0) {
        offset++;
        source += buffer[offset]->toString();

        /*
         * term: type
         * func(): type
         */

        if (offset + 1 >= buffer.size()) {
            throw ParserError("Typename missing!");
        }

        offset++;
        source += buffer[offset]->toString();

        skip = SkipBrackets(buffer, offset + 1);
        if (skip) {
            /*
             * term: type[]
             * func(): type[]
             *
             */
            for (size_t i = 0; i < skip; i++) {
                source += buffer[offset + i]->toString();
            }

            offset += (skip + 1);
        }
    }
    result = ParseTerm(source.c_str(), ctx, pragma_enable, macro_expand);
    return offset;
}

TermPtr Parser::CheckModuleTerm(const TermPtr& term) {
    if (!utils::isModuleName(term->getText())) {
        return term;
    }
    // Ссылочная форма `\path::name` (модуль + `::`-часть) обрабатывается правилом `name`
    // (ResolveModuleReference): она НЕ является оператором загрузки.
    if (term->m_right) {
        return term;
    }
    const std::optional<ModulePath> parsedPath = ModulePath::parse(term->getText());
    if (!parsedPath.has_value() || (!parsedPath->isAnchored() && !isValidModuleLogicalName(term->getText()))) {
        m_ctx.diag().report(Severity::Error, term->m_mapperRange, "Module name - backslash, underscore, lowercase English letters or number!");
        return term; // не обрабатываем невалидное имя модуля (ошибка уже записана)
    }
    if (!m_expand_module) {
        return term;
    }
    if (term->isCall()) {
        // Загрузка модуля: \module(func) - рекурсивный вызов парсера. Индекс загруженного
        // модуля переносится в MODULE-терм: AST модуля конвертируется ОДИН раз, Term-тело
        // сайту импорта не нужно. Зависимость парсера - только узкий ModuleResolver.
        ModuleResolver* resolver = m_ctx.moduleResolver();
        EXPECT(resolver != nullptr && "parser: module resolver is not set (Pipeline must inject ModuleLoader)");
        const std::vector<ModuleIndex> indices = resolver->resolveModules(term->getText(), term->m_mapperRange);
        if (indices.empty()) {
            m_ctx.diag().report(Severity::Error, term->m_mapperRange, "Module '{}' resolved to no files", term->getText());
        }
        term->m_moduleIndices = std::move(indices);
        term->m_sequence.clear();
    } else {
        // Обращение \module::var - не грузим, а проверяем факт загрузки модуля.
        ModuleResolver* resolver = m_ctx.moduleResolver();
        EXPECT(resolver != nullptr && "parser: module resolver is not set (Pipeline must inject ModuleLoader)");
        if (!resolver->isModuleLoaded(term->getText())) {
            reportModuleNotLoaded(m_ctx, term->m_mapperRange, term->getText());
        }
    }
    return term;
}

// Ссылочная форма `\path::name`: модуль НЕ загружается. Индекс загруженного модуля
// материализуется в терме (m_moduleIndices), чтобы конвертер (loader-free) построил AST-факт.
TermPtr Parser::ResolveModuleReference(const TermPtr& term) {
    EXPECT(term && "ResolveModuleReference: null term");
    const std::optional<ModulePath> parsedPath = ModulePath::parse(term->getText());
    if (!parsedPath.has_value() || (!parsedPath->isAnchored() && !isValidModuleLogicalName(term->getText()))) {
        m_ctx.diag().report(Severity::Error, term->m_mapperRange, "Module name - backslash, underscore, lowercase English letters or number!");
        return term;
    }
    if (!m_expand_module) {
        return term;
    }
    ModuleResolver* resolver = m_ctx.moduleResolver();
    EXPECT(resolver != nullptr && "parser: module resolver is not set (Pipeline must inject ModuleLoader)");
    if (const auto index = resolver->findLoaded(term->getText())) {
        term->m_moduleIndices = {*index};
        return term;
    }
    // Spelling загружен, но адресует НЕ ОДИН файл (групповой импорт `\\group`): ссылка обязана
    // называть файл, иначе имя неоднозначно.
    if (resolver->isModuleLoaded(term->getText())) {
        m_ctx.diag().report(Severity::Error, term->m_mapperRange,
                            "Module reference '{}' must name one file: a group import cannot be used in a qualified name",
                            term->getText());
        return term;
    }
    reportModuleNotLoaded(m_ctx, term->m_mapperRange, term->getText());
    return term;
}

void Parser::RegisterModuleAlias(const TermPtr& alias, const TermPtr& target) {
    if (!alias || !target || target->m_moduleIndices.empty()) {
        return;
    }
    ModuleResolver* resolver = m_ctx.moduleResolver();
    EXPECT(resolver != nullptr && "parser: module resolver is not set (Pipeline must inject ModuleLoader)");
    (void)resolver->registerModuleAlias(alias->getText(), target->m_moduleIndices.front(), resolver->currentModuleIndex());
}

parser::token_type Parser::GetNextToken(TermPtr* yylval) {

    parser::token_type result;

    ASSERT(yylval);

    /*
     * Новая логика работы парсера.
     *
     * Термины считываются из лексера до символа ';' или до конца файла.
     * Каждый термин буфера проверяется на макрос и если находится - заменяется.
     * Для обычного макроса просто заменяются токены и тела макроса,
     * а для текстового макроса используется отдельный буфер до завершения работы лексичсекского анализатора,
     * после чего этот буфер вставляется на место макроса в основонм бефере данных.
     * После обработки всего буфера его элеменеты передаются в парсер для обработки.
     */

    parser::token_type type;
    bool lexer_complete;

go_parse_string:

    TermPtr term;
    bool is_escape = false;

    if (m_macro_analisys_buff.empty()) {

        // Граница оператора: буфер анализа пуст - сбрасываем счётчик суммарных раскрытий макросов
        // и флаг отчёта о рекурсии, чтобы самовоспроизведение в пределах одного оператора не
        // накапливалось между операторами.
        m_macro_expansion_total = 0;
        m_macro_recursion_reported = false;

        lexer_complete = false;

        while (!m_is_lexer_complete) {
        next_escape_token:

            term = Term::Create(TermID::END, "");
            {
                type = lexer->lex(&term);
            }

            // Коллбек для внешнего потребителя (напр. форматтера): сырой терм до раскрытия
            // макроса. Вызывается для каждого raw-токена, читаемого лексером (в т.ч. внутри
            // макроопределений и буферных); парсер продолжает работу как обычно.
            if (on_token) {
                on_token(*term);
            }

            ASSERT(type == term->m_lexer_type);

            if (is_escape) {

                if (type == parser::token_type::END) {
                    m_ctx.diag().report(Severity::Error, term->m_mapperRange, "Unexpected end of file '{}'", term->toString());
                }
                is_escape = false;
                term->m_id = TermID::ESCAPE;
                term->m_lexer_type = parser::token_type::ESCAPE;
                type = parser::token_type::ESCAPE;

            } else if (type == parser::token_type::ESCAPE) {

                is_escape = true;
                goto next_escape_token;
            }

            // Распознавание маркерных макросов по имени (тело = ровно одна framing-лексема).
            // Выполняем закреплённое за токеном действие (переключение состояния лексера),
            // эмитированный MACRO_SEQ/MACRO_DEL отдаём обычным веткам ниже; если токен не
            // нужен (например, вход в текстовое тело) - продолжаем лексинг.
            // Голой NAME-маркер распознаём везде, КРОМЕ первого терма фазы сбора имени нового
            // определения (`@@ имя @@ …`): первый терм - имя самого макроса (маркером не считается),
            // иначе маркер с совпадающим именем ломает счётчик @@ (ложная "Nested '@@'"),
            // а собранный терм макроса остаётся без имени (см. CheckMacro). Но как только сигнатура
            // уже содержит термы (не-первый терм), голой NAME-маркер допустим: так
            // `@macro assert( $cond ) macro_body` открывает тело через `macro_body`/`macro_end`
            // БЕЗ '@' (макрос раскрывается и с сигнатурой @, и без неё).
            const bool in_macro_name_phase = lexer->m_macro_body && !lexer->m_macro_body->m_right;
            const bool bare_marker_ok = !in_macro_name_phase || (lexer->m_macro_body && !lexer->m_macro_body->m_sequence.empty());
            if (m_macro && (term->getTermID() == TermID::MACRO || (term->getTermID() == TermID::NAME && bare_marker_ok))) {
                const TermID marker = m_macro->MarkerToken(term->getText());
                if (marker != TermID::END) {
                    TermPtr out;
                    bool emitted = false;
                    if (marker == TermID::MACRO_LEXEME) {
                        emitted = lexer->macroSeqAction(out);
                    } else if (marker == TermID::MACRO_STR_LEXEME) {
                        emitted = lexer->macroStrAction(out);
                    } else if (marker == TermID::MACRO_DEL_LEXEME) {
                        emitted = lexer->macroDelAction(out);
                    }
                    if (emitted && out) {
                        term = std::move(out);
                        // Важно: тип токена тоже меняется (MACRO -> MACRO_SEQ/MACRO_DEL),
                        // иначе read-цикл не попадёт в ветки обработки маркеров и просто
                        // положит открывающий токен в буфер.
                        type = term->m_lexer_type;
                    } else {
                        continue;
                    }
                }
            }

            // Раскрытие предопределённых макросов внутри C++-вставки {% ... %} (наравне с
            // переменными): содержимое вставки - единый EMBED-токен, поэтому `@__...__` внутри
            // него не проходит обычный поток токенов. Раскрываем прямо в тексте вставки.
            if (term->getTermID() == TermID::EMBED) {
                std::string embedText(term->getText());
                m_predef.expandEmbedPredefMacros(embedText, term->m_mapperRange);
                term->setTextOwned(std::move(embedText));
            }

            if (type == parser::token_type::END) {
                m_is_lexer_complete = true;
            } else if (lexer->m_macro_body) {
                // Внутри определения макроса (открыто через `@@`).
                if (lexer->m_macro_body->m_right) {
                    // Фаза seq-тела: собираем тело.
                    if (type == parser::token_type::MACRO_DEL) {
                        // Универсальный терминатор `@@@@` - финализируем макрос.
                        lexer->m_macro_body->m_right->m_mapperRange.end = term->m_mapperRange.begin;
                        term.swap(lexer->m_macro_body);
                        lexer->m_macro_body = nullptr;
                    } else {
                        lexer->m_macro_body->m_right->m_sequence.push_back(term);
                        continue;
                    }
                } else {
                    // Фаза имени: собираем сигнатуру (последовательность термов).
                    if (type == parser::token_type::MACRO_SEQ) {
                        // Закрытие имени -> открытие seq-тела.
                        lexer->m_macro_body->m_right = Term::Create(TermID::MACRO_SEQ, "@@", term->m_mapperRange);
                        // Совместимость: имя также в m_left (MACRO_SEQ со сигнатурой в m_sequence).
                        lexer->m_macro_body->m_left = Term::Create(TermID::MACRO_SEQ, "@@", term->m_mapperRange);
                        lexer->m_macro_body->m_left->m_sequence = lexer->m_macro_body->m_sequence;
                        continue;
                    } else if (type == parser::token_type::MACRO_STR) {
                        // Текстовое тело готово: m_right = MACRO_STR, финализируем макрос.
                        lexer->m_macro_body->m_right = term;
                        // Совместимость: имя также в m_left (MACRO_SEQ со сигнатурой в m_sequence).
                        lexer->m_macro_body->m_left = Term::Create(TermID::MACRO_SEQ, "@@", term->m_mapperRange);
                        lexer->m_macro_body->m_left->m_sequence = lexer->m_macro_body->m_sequence;
                        term.swap(lexer->m_macro_body);
                        lexer->m_macro_body = nullptr;
                    } else if (type == parser::token_type::MACRO_DEL) {
                        // Удаление макроса: `@@ имя @@@@`. Оформляем собранное имя в MACRO_DEL-терм.
                        if (lexer->m_macro_body->m_sequence.empty()) {
                            m_ctx.diag().report(Severity::Error, term->m_mapperRange, "Empty macro name in delete `@@ @@@@` is not allowed!");
                            lexer->m_macro_body = nullptr;
                            continue;
                        }
                        term->m_sequence.swap(lexer->m_macro_body->m_sequence);
                        term->m_mapperRange.begin = lexer->m_macro_body->m_mapperRange.begin;
                        lexer->m_macro_body = nullptr;
                        // term (MACRO_DEL с именем в m_sequence) уходит в буфер -> RemoveMacro.
                    } else {
                        lexer->m_macro_body->m_sequence.push_back(term);
                        continue;
                    }
                }
            } else if (type == parser::token_type::MACRO_SEQ) {
                // Открывающий @@ - начало определения макроса.
                lexer->m_macro_body = term;
                continue;
            }

            m_macro_analisys_buff.push_back(term);
            if (term->getText().compare(";") == 0) {
                lexer_complete = true;
                break;
            }
        }

        if (lexer->m_macro_del || lexer->m_macro_body || lexer->m_macro_count) {
            lexer->m_macro_del = 0;
            lexer->m_macro_count = 0;
            lexer->m_inMacroSeq = false;

            TermPtr bag_position;
            bag_position = (m_macro_analisys_buff.size() > 1) ? m_macro_analisys_buff[m_macro_analisys_buff.size() - 2] : term;
            if (bag_position) {
                if (term) {
                    m_ctx.diag().report(Severity::Error, bag_position->m_mapperRange, "Incomplete syntax near '{}' in file {}!", bag_position->getText(),
                                        m_ctx.source().filename(term->m_mapperRange.begin));
                } else {
                    m_ctx.diag().report(Severity::Error, bag_position->m_mapperRange, "Incomplete syntax near '{}'!", bag_position->getText());
                }
            } else {
                // Нет терма для позиции (мягкая обработка ошибок): сообщение без локации.
                m_ctx.diag().report(Severity::Error, MapperRange{}, "Incomplete syntax (macro buffer)");
            }
            lexer->m_macro_body = nullptr;
        }
    }

    TermPtr pragma;
    while (lexer->m_macro_count == 0 && !m_macro_analisys_buff.empty()) {

        // Внутри скобок атрибута `@[ ... @]` макро-имена НЕ раскрываются: содержимое атрибута
        // (имя + параметры-литералы) - это не код, макросы в нём запрещены. Имя встроенного
        // атрибута может совпадать с именем keyword-макроса (напр. func_const) - раскрытие такого
        // имени дало бы бесконечную рекурсию. Явный `@`-макрос внутри атрибута - ошибка.
        if (m_in_attr) {
            if (m_macro_analisys_buff[0]->getTermID() == TermID::MACRO) {
                const auto& t = m_macro_analisys_buff[0];
                m_ctx.diag().report(Severity::Error, t->m_mapperRange, "macro '{}' is not allowed inside an attribute '@[...@]'", t->getText());
            }
            break;
        }

        if (m_enable_pragma) {

            // @__CHECK_AREA__(...) - встроенный контекст-макрос-маркер ограничения области:
            // захватываем аргументы ДО раскрытия/прагм (иначе expandPredefMacro заштамповал бы
            // его как контекст-макрос, а прагма-ветка съела бы как «unknown pragma»).
            if (m_pragma.evalCheckArea(m_macro_analisys_buff)) {
                continue;
            }

            // @__DEBUG__/@__DEBUG_SCOPE__(...) - встроенные системные макросы отладочного вывода:
            // захватываем аргументы ДО раскрытия (иначе expandPredefMacro заштамповал бы их как
            // контекст-макросы). В release-сборке компилятора вызов стирается + предупреждение.
            if (m_pragma.evalDebug(m_macro_analisys_buff)) {
                continue;
            }

            m_predef.expandPredefMacro(m_macro_analisys_buff[0]);

            // Обработка команд парсера @__PRAGMA ... __
            if (m_pragma.pragmaCheck(m_macro_analisys_buff[0])) {

                // @__OPTION_TRUE__/@__OPTION_FALSE__: «сырое» содержимое без пре-парсинга.
                if (m_pragma.evalOptionTrueFalseRaw(m_macro_analisys_buff)) {
                    continue;
                }

                size_t size;
                size = Parser::ParseTerm(pragma, m_macro_analisys_buff, m_ctx, 0, false);

                ASSERT(size);
                ASSERT(pragma);

                m_macro_analisys_buff.erase(m_macro_analisys_buff.begin(), m_macro_analisys_buff.begin() + size);

                m_pragma.pragmaEval(pragma, m_macro_analisys_buff);
                // @__PRAGMA_TRY_CLOSE__: финальная '}' try-блока станет закрывающим (TRY_*_END).
                // Открытие вложенного try: просто пушим новый кадр (стек LIFO). Глубину внешних
                // кадров увеличивает сам '{'/'{+'/'{*'/'{-' try-begin (см. обработку ниже).
                if (m_pragma.tryClose() != parser::token_type::END) {
                    m_pendingTryClose.push_back({m_pragma.tryClose(), 0});
                    m_pragma.clearTryClose();
                }
                continue;
            }
        }

        // Обработка команды проверки следующего токена @__PRAGMA_EXPECTED__: следующий токен
        // должен быть одним из перечисленных строковых представлений. Одноразовая проверка -
        // после первой (совпадение или нет) список очищается, чтобы не дублировать диагностику.
        if (!m_pragma.expected().empty()) {
            const std::string_view found = m_macro_analisys_buff[0]->getText();
            const auto& expected = m_pragma.expected();
            bool matched = false;
            for (const auto& exp : expected) {
                if (exp == found) {
                    matched = true;
                    break;
                }
            }
            if (!matched) {
                std::string exp_str;
                for (size_t i = 0; i < expected.size(); ++i) {
                    if (i) {
                        exp_str += ", ";
                    }
                    exp_str += "'" + expected[i] + "'";
                }
                m_ctx.diag().report(Severity::Error, m_macro_analisys_buff[0]->m_mapperRange, "expected one of: {} but found '{}'", exp_str, found);
            }
            m_pragma.clearExpected();
        }

        if (m_no_macro) {
            m_no_macro = false;
            break;
        }

        switch (ExpandTermMacro(*this)) {
        case ExpandMacroResult::Continue:;
            continue;
        case ExpandMacroResult::Goto:;
            goto go_parse_string;
        default:
            break;
        };

        break;
    }

    if (!m_macro_analisys_buff.empty()) {

        TermPtr current = m_macro_analisys_buff.at(0);

        // @__PRAGMA_TRY_CLOSE__: финальная '}' try-блока (глубина вложенности 0) → TRY_*_END.
        // Вложенные '{'/'{+'/'{*'/'{-' открывают вложенный блок (увеличивают глубину ВСЕХ
        // активных кадров), соответствующие '}' её уменьшают; финальная '}' верхнего кадра
        // (depth==0) конвертируется в его endToken и снимает кадр, попутно закрывая try-begin
        // ({*) во внешних кадрах (компенсируя им -1).
        if (!m_pendingTryClose.empty()) {
            const auto tt = current->m_lexer_type;
            if (tt == parser::token_type::LBRACE || tt == parser::token_type::TRY_ALL_BEGIN || tt == parser::token_type::TRY_PLUS_BEGIN ||
                tt == parser::token_type::TRY_MINUS_BEGIN) {
                for (auto& f : m_pendingTryClose) {
                    ++f.depth;
                }
            } else if (tt == parser::token_type::RBRACE || tt == parser::token_type::TRY_ALL_END || tt == parser::token_type::TRY_PLUS_END ||
                       tt == parser::token_type::TRY_MINUS_END) {
                if (m_pendingTryClose.back().depth > 0) {
                    --m_pendingTryClose.back().depth;
                } else {
                    current->m_lexer_type = m_pendingTryClose.back().endToken;
                    m_pendingTryClose.pop_back();
                    // Эта '}' закрывает try-begin ({*) вложенного try, который увеличил глубину
                    // всех оставшихся (внешних) кадров на 1 - компенсируем им -1.
                    for (auto& f : m_pendingTryClose) {
                        --f.depth;
                    }
                }
            }
        }

        result = current->m_lexer_type;
        m_recent.set(current->m_mapperRange);

        if (m_postlex) {

            // Раскрыть последовательность токенов, т.к. они собираются в термин в лексере, а не парсере.
            // Для определения макроса выводим полную новую лексику:
            //   seq:  @@ <имя> @@ <тело> @@@@
            //   text: @@ <имя> @@ <строка> @@@@   (открытие текста нормализуется в @@)
            //   del:  @@ <имя> @@@@
            if (current->getTermID() == TermID::MACRO_DEL) {
                m_postlex->push_back("@@");
                for (int i = 0; i < current->m_sequence.size(); i++) {
                    m_postlex->push_back(std::string(current->m_sequence[i]->getText()));
                }
                m_postlex->push_back(std::string(current->getText())); // @@@@
            } else {
                m_postlex->push_back(std::string(current->getText()));
                if (current->getTermID() == TermID::MACRO_SEQ) {
                    for (int i = 0; i < current->m_sequence.size(); i++) {
                        m_postlex->push_back(std::string(current->m_sequence[i]->getText()));
                    }
                    m_postlex->push_back(std::string(current->getText()));
                    if (current->m_right) {
                        if (current->m_right->getTermID() == TermID::MACRO_STR) {
                            m_postlex->push_back(std::string(current->m_right->getText()));
                        } else {
                            for (size_t i = 0; i < current->m_right->m_sequence.size(); i++) {
                                m_postlex->push_back(std::string(current->m_right->m_sequence[i]->getText()));
                            }
                        }
                    }
                    m_postlex->push_back("@@@@");
                }
            }
        }

        // Отслеживаем вход/выход из атрибута `@[ ... @]`, чтобы не раскрывать макросы внутри.
        // Детекция по TermID: `@[` -> ATTRIBUTE, `@]` -> ATTR_COMPLETE (см. lexer.l YY_TOKEN).
        if (current->getTermID() == TermID::ATTRIBUTE) {
            m_in_attr = true;
        } else if (current->getTermID() == TermID::ATTR_COMPLETE) {
            m_in_attr = false;
        }

        *yylval = std::move(current);
        m_macro_analisys_buff.erase(m_macro_analisys_buff.begin());
        return result;
    }

    m_recent.setEndOfLast();

    *yylval = nullptr;

    return parser::token_type::END;
}

} // namespace trust
