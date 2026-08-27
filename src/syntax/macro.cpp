#include "syntax/lexer.h"

#include "syntax/macro.h"
#include "syntax/macro_split.hpp"
#include "syntax/parser.h"
#include "diag/mapper.hpp"
#include "syntax/diag.hpp"
#include "utils/strings.hpp"

#include <set>
#include <vector>

using namespace trust;

namespace {

// Подавление -Wsigil для макроса, вызванного без '@'. Возвращает true, если bare-имя есть в
// значении флага "keywords" (cli-имя, запятая-разделение) как запись БЕЗ ведущего '@'
// (макрос «как ключевое слово»). Записи с '@' суппресс НЕ дают (односторонняя логика).
bool isKeywordSigilSuppressed(const trust::Context& ctx, const std::string& bareName) {
    const auto kw = ctx.opts().flagValueByName("keywords");
    if (!kw || kw->empty()) {
        return false;
    }
    std::string_view s = *kw;
    size_t i = 0;
    while (i < s.size()) {
        const size_t j = s.find(',', i);
        const size_t end = (j == std::string_view::npos) ? s.size() : j;
        const std::string_view entry = s.substr(i, end - i);
        if (!entry.empty() && entry[0] != '@' && entry == bareName) {
            return true;
        }
        i = (j == std::string_view::npos) ? s.size() : j + 1;
    }
    return false;
}

} // namespace

// Символы, запрещённые в имени/сигнатуре макроса. Допустимы как термы-разделители/маркеры:
// `:` - разделитель типа возврата (`@@ func $name ( ... ): $... @@`), `{`/`}` - терминатор
// сигнатуры/граница блока тела (`@@ func $name ( ... ): $... { @@`), `@` - префикс имени
// другого макроса в сигнатуре (`@@ @foo func $name ... @@`).

Macro::Macro(trust::Context& ctx)
: m_ctx(ctx) {
    // Базовый скоуп (например, для макросов встроенного dsl).
    m_scopes.emplace_back();
}

void Macro::PushScope() {
    m_scopes.emplace_back();
}

void Macro::PopScope() {
    if (m_scopes.size() <= 1) {
        FAULT("Cannot pop the base macro scope");
    }
    m_scopes.pop_back();
}

MacroScope* Macro::FindScope(std::string_view key) {
    for (auto iter = m_scopes.rbegin(); iter != m_scopes.rend(); ++iter) {
        if (iter->find(key) != iter->end()) {
            return &*iter;
        }
    }
    return nullptr;
}

const MacroScope* Macro::FindScope(std::string_view key) const {
    for (auto iter = m_scopes.rbegin(); iter != m_scopes.rend(); ++iter) {
        if (iter->find(key) != iter->end()) {
            return &*iter;
        }
    }
    return nullptr;
}

SequenceType* Macro::FindMacroList(std::string_view key) {
    MacroScope* scope = FindScope(key);
    return scope ? &scope->find(key)->second.defs : nullptr;
}

const SequenceType* Macro::FindMacroList(std::string_view key) const {
    const MacroScope* scope = FindScope(key);
    return scope ? &scope->find(key)->second.defs : nullptr;
}

std::string Macro::toMacroHash(TermPtr& term) {
    return syntax::toMacroHash(m_ctx, term);
}

TermID Macro::MarkerToken(std::string_view key) const {
    return syntax::markerToken(*this, key);
}

// replase

TermPtr trust::ProcessMacro(Parser& parser, TermPtr& term) {

    if (!parser.m_macro) {
        return term;
    }

    if (parser.m_macro) {
        return parser.m_macro->EvalOpMacros(term);
    }
    return term;
}

// replase

ExpandMacroResult trust::ExpandTermMacro(Parser& parser) {

    if (parser.m_macro) {

        // Макрос должне начинаться всегда с термина
        if (!(parser.m_macro_analisys_buff[0]->getTermID() == TermID::MACRO || parser.m_macro_analisys_buff[0]->getTermID() == TermID::NAME)) {
            return ExpandMacroResult::Break;
        }

        TermPtr macro_done = nullptr;

        // Список макросов, один из которых может соответствовать текущему буферу (по первому термину буфера)
        SequenceType* macro_list = parser.m_macro->FindMacroList(parser.m_macro->toMacroHash(parser.m_macro_analisys_buff[0]));

        if (!macro_list) {
            return ExpandMacroResult::Break;
        }

        macro_done.reset();
        size_t macro_best = 0;
        // Перебрать все макросы группы (общий первый терм) и выбрать самый длинный
        // (longest-match по числу потреблённых термов буфера). Совпадение короткого макроса
        // (только первый терм) не отбрасывает более специфичный макрос той же группы.
        for (auto iter = macro_list->begin(); iter != macro_list->end(); ++iter) {

            const size_t matched = parser.m_macro->MatchMacro(parser.m_macro_analisys_buff, *iter);
            if (matched == 0) {
                continue;
            }

            if (matched > macro_best) {
                macro_done = *iter;
                macro_best = matched;
            } else if (matched == macro_best && *iter != macro_done) {
                // Два РАЗНЫХ макроса группы потребляют одинаковое число термов - настоящая
                // неоднозначность выбора (не путать с разными арностями одной группы).
                parser.m_ctx.diag().report(Severity::Error, macro_done->m_mapperRange, "Macro duplication '{}' and '{}'!", macro_done->toString(),
                                           (*iter)->toString());
            }
        }

        ASSERT(macro_list);

        if (macro_done) {
            // -Wsigil: макрос вызван БЕЗ '@' (bare NAME) и не подавлен флагом "keywords".
            // Подавление одностороннее: имя в keywords как запись без ведущего '@' супрессит
            // warning; запись с '@' супресс не даёт. Диагностика и fixit «добавить @».
            {
                const auto& call0 = parser.m_macro_analisys_buff[0];
                if (call0 && call0->getTermID() == TermID::NAME && !isKeywordSigilSuppressed(parser.m_ctx, call0->getText())) {
                    if (parser.m_ctx.opts().isRegisteredByName("sigil")) {
                        const auto sev = parser.m_ctx.opts().getByName("sigil");
                        if (sev.has_value()) {
                            auto* entry = parser.m_ctx.diag().report(*sev, call0->m_mapperRange, "macro '{}' is missing '@' sigil", call0->getText());
                            if (entry != nullptr && !call0->m_mapperRange.isInvalid()) {
                                parser.m_ctx.diag().fixit(entry, call0->m_mapperRange, "@" + call0->getText());
                            }
                        }
                    }
                }
            }
            // Защита от бесконечной рекурсии при раскрытии макросов:
            // - kMacroNestingLimit   - глубина вложенности в одной цепочке раскрытий (вложенная рекурсия);
            // - kMacroExpansionLimit - суммарное число раскрытий в текущем операторе (самовоспроизведение
            //   через границы чтений парсера, когда глубина не копится).
            // Обе ошибки - Severity::Error (мягкие, как прочие ошибки программы): макрос дальше НЕ раскрываем
            // (return Break - иначе бесконечный цикл), разбор продолжается на оставшихся токенах (bison
            // error-recovery). Отчёт - один раз за оператор (m_macro_recursion_reported). Локация - call site
            // текущего раскрытия, имя макроса - первый терм сигнатуры (а не определение в DSL).
            const auto& call0 = parser.m_macro_analisys_buff[0];
            const std::string mname = (!macro_done->m_sequence.empty()) ? std::string(macro_done->m_sequence.front()->getText()) : macro_done->toString();
            if (parser.m_macro_depth >= kMacroNestingLimit && !parser.m_macro_recursion_reported) {
                parser.m_macro_recursion_reported = true;
                parser.m_ctx.diag().report(Severity::Error, call0->m_mapperRange, "recursive macro '{}' (nesting too deep)", mname);
                return ExpandMacroResult::Break;
            }
            if (++parser.m_macro_expansion_total >= kMacroExpansionLimit) {
                if (!parser.m_macro_recursion_reported) {
                    parser.m_macro_recursion_reported = true;
                    parser.m_ctx.diag().report(Severity::Error, call0->m_mapperRange,
                                               "recursive macro '{}' (expansion did not terminate; possible self-recursion)", mname);
                }
                return ExpandMacroResult::Break;
            }

            // Новое дерево раскрытия макроса - сброс кэша гигиенических имён
            if (parser.m_macro_depth == 0) {
                parser.pragma().clearHygienicNames();
            }

            parser.m_macro_depth++;

            ASSERT(parser.m_macro_analisys_buff.size() >= macro_done->m_sequence.size());
            ASSERT(macro_done->m_right);

            MacroArgsType macro_args;
            size_t size_remove = parser.m_macro->ExtractArgs(parser.m_macro_analisys_buff, macro_done, macro_args);

            ASSERT(size_remove >= 1 && size_remove <= parser.m_macro_analisys_buff.size());

            // Диапазон вызова макроса: от начала первого токена до конца последнего
            // потреблённого токена (индекс size_remove-1). Токен на индексе size_remove
            // уже НЕ входит в вызов - использовать его нельзя (для операторных макросов
            // $... он может выйти за границы буфера).
            const auto& last_call_term = parser.m_macro_analisys_buff[size_remove - 1];

            // Диапазон определения макроса: от начала имени (m_sequence) до конца тела.
            // Раньше бралось только тело (m_right), поэтому при переходе по ссылке
            // выделялась лишь часть макроса, а не весь макрос целиком.
            MapperRange def_range;
            if (macro_done->m_right->getTermID() == TermID::MACRO_SEQ && !macro_done->m_right->m_sequence.empty()) {
                def_range.end = macro_done->m_right->m_sequence.back()->m_mapperRange.end;
            } else {
                def_range.end = macro_done->m_right->m_mapperRange.end;
            }
            // Начало - открывающий маркер `@@` определения макроса (сам терм macro_done),
            // но только если он в том же файле, что и конец тела (иначе кросс-файловый range → EXPECT).
            if (!macro_done->m_mapperRange.begin.isInvalid() && macro_done->m_mapperRange.begin.fileIdx() == def_range.end.fileIdx()) {
                def_range.begin = macro_done->m_mapperRange.begin;
            } else if (!macro_done->m_sequence.empty() && !macro_done->m_sequence.front()->m_mapperRange.begin.isInvalid() &&
                       macro_done->m_sequence.front()->m_mapperRange.begin.fileIdx() == def_range.end.fileIdx()) {
                def_range.begin = macro_done->m_sequence.front()->m_mapperRange.begin;
            } else if (macro_done->m_right->getTermID() == TermID::MACRO_SEQ && !macro_done->m_right->m_sequence.empty()) {
                def_range.begin = macro_done->m_right->m_sequence.front()->m_mapperRange.begin;
            } else {
                def_range.begin = macro_done->m_right->m_mapperRange.begin;
            }

            // Реальный range замещаемого фрагмента (вызова макроса). Все вставленные лексемы
            // раскрытого тела должны получить именно его location - иначе клоны тела (из @trust/dsl)
            // сохраняют range DSL-определения, и грамматика, комбинирующая range с call-site
            // токенами, даёт begin > end (разные файлы) → EXPECT(b <= e).
            MapperRange call_range{parser.m_macro_analisys_buff.front()->m_mapperRange.begin, last_call_term->m_mapperRange.end};

            // Токен уже лежит внутри call_range (реальный пользовательский аргумент макроса,
            // подставленный в тело) → сохраняем его точную позицию для диагностики; иначе это
            // токен шаблона DSL-определения → ремапим на call_range (source-map указывает на вызов).
            auto in_call_range = [&](const trust::TermPtr& t) -> bool {
                if (!t || t->m_mapperRange.begin.isInvalid() || t->m_mapperRange.end.isInvalid()) {
                    return false;
                }
                return t->m_mapperRange.begin.fileIdx() == call_range.begin.fileIdx() && t->m_mapperRange.begin >= call_range.begin &&
                       t->m_mapperRange.end <= call_range.end;
            };

            parser.m_ctx.source().addMacroMapping(call_range, {def_range.begin, def_range.end});

            parser.m_macro_analisys_buff.erase(parser.m_macro_analisys_buff.begin(), parser.m_macro_analisys_buff.begin() + size_remove);

            if (macro_done->m_right->getTermID() == TermID::MACRO_STR) {

                auto expanded_str = parser.m_macro->ExpandString(macro_done, macro_args);
                SequenceType macro_block = Scanner::ParseLexem(parser.lexer->m_ctx, expanded_str);
                for (auto& t : macro_block) {
                    if (t && !in_call_range(t)) {
                        t->m_mapperRange = call_range;
                    }
                }
                parser.m_macro_analisys_buff.insert(parser.m_macro_analisys_buff.begin(), macro_block.begin(), macro_block.end());

                parser.m_macro_depth--;

                return ExpandMacroResult::Continue;

            } else {

                ASSERT(macro_done->m_right);
                SequenceType macro_block = parser.m_macro->ExpandMacros(macro_done, macro_args, parser, call_range);
                for (auto& t : macro_block) {
                    if (t && !in_call_range(t)) {
                        t->m_mapperRange = call_range;
                    }
                }
                parser.m_macro_analisys_buff.insert(parser.m_macro_analisys_buff.begin(), macro_block.begin(), macro_block.end());
            }

            parser.m_macro_depth--;

            return ExpandMacroResult::Continue;

        } else {
            // Имя макроса присутствует в группе, но ни один шаблон не сопоставился с вызовом.
            // Находим «ближайший» кандидат (максимум совпавших термов сигнатуры до провала),
            // чтобы дать конкретную причину рассинхрона + список всех доступных паттернов.
            // Это ВСЕГДА диагностика макропроцессора (а не анализатора).
            MacroMismatch best;
            TermPtr bestMacro;
            for (auto iter = macro_list->begin(); iter != macro_list->end(); ++iter) {
                MacroMismatch mm;
                if (parser.m_macro->MatchMacro(parser.m_macro_analisys_buff, *iter, &mm) != 0) {
                    continue; // совпал - в эту ветку попадать не должен
                }
                if (!bestMacro || mm.matched_terms > best.matched_terms) {
                    best = mm;
                    bestMacro = *iter;
                }
            }

            const std::string call_name = parser.m_macro_analisys_buff[0]->toString();
            const std::string exp_str = (best.expected.empty() || best.expected == "@@") ? "end of pattern" : "'" + best.expected + "'";
            const std::string found_str = (best.found.empty() || best.found.rfind("@@", 0) == 0) ? "end of input" : "'" + best.found + "'";
            std::string msg = "macro '" + call_name +
                              "' does not match any pattern.\n"
                              "Closest pattern: expected " +
                              exp_str + " but found " + found_str +
                              ".\n"
                              "The following macro mapping are available:\n" +
                              parser.m_macro->GetMacroMaping(parser.m_macro->toMacroHash(parser.m_macro_analisys_buff[0]), "\n");
            auto* entry = parser.m_ctx.diag().report(Severity::Error, parser.m_macro_analisys_buff[0]->m_mapperRange, "{}", msg);

            // Fix-it: если рассинхрон на реальном токене буфера и ожидаемый терм - простой литерал
            // (не шаблон/EOF-маркер), предлагаем заменить найденный токен на ожидаемый.
            if (entry && bestMacro && best.buffer_pos < parser.m_macro_analisys_buff.size()) {
                const std::string& exp = best.expected;
                const bool literal = !exp.empty() && exp != "@@" && exp.find('$') == std::string::npos && exp.find('(') == std::string::npos &&
                                     !best.found.empty() && best.found.rfind("@@", 0) != 0;
                if (literal) {
                    parser.m_ctx.diag().fixit(entry, parser.m_macro_analisys_buff[best.buffer_pos]->m_mapperRange, exp);
                }
            }
        }
    }
    return ExpandMacroResult::Break;
}

SequenceType Macro::MakeMacroId(const SequenceType& seq) {
    return syntax::makeMacroId(m_ctx, seq);
}

SequenceType Macro::GetMacroId(TermPtr& term) {
    return syntax::getMacroId(m_ctx, term);
}

std::string Macro::GetMacroMaping(const std::string str, const char* separator) {

    std::string result;

    SequenceType* macro_list = FindMacroList(str);
    if (!macro_list) {
        return result;
    }

    for (auto iter = macro_list->begin(); iter != macro_list->end(); ++iter) {

        if (!result.empty() && separator) {
            result += separator;
        }

        result += (*iter)->toString();
    }
    return result;
}

bool Macro::CheckMacro(const TermPtr& term) {
    return syntax::checkMacro(m_ctx, term);
}

TermPtr Macro::EvalOpMacros(TermPtr& term) {

    ASSERT(term);

    if (term->getTermID() == TermID::MACRO_DEL) {
        if (!RemoveMacro(term)) {
            // Явная диагностика (не тихий stderr): удаление несуществующего макроса.
            m_ctx.diag().report(Severity::Error, term->m_mapperRange, "Macro '{}' not found for removal!", toMacroHash(term));
        }
        return term;
    }

    if (term->getTermID() != TermID::MACRO_SEQ || !term->m_right) {
        m_ctx.diag().report(Severity::Error, term->m_mapperRange, "Operand '{}' not a macros!", term->toString());
        return term; // не продолжать EvalOpMacros с не-макросом
    }

    if (!CheckMacro(term)) {
        return term; // макрос некорректен (мягкие ошибки) - не регистрируем
    }

    // Имя макроса для диагностики (из сигнатуры m_sequence).
    auto macro_name = [&](TermPtr t) -> std::string {
        SequenceType mid = GetMacroId(t);
        std::string s;
        for (auto& e : mid) {
            if (!s.empty()) {
                s += " ";
            }
            s += e->getText();
        }
        return s;
    };

    TermPtr macro = GetMacroById(GetMacroId(term));

    if (macro) {
        // Макрос с такой сигнатурой уже существует -> переопределение тела.
        // Уровень диагностики управляется опцией "macro-redefined".
        m_ctx.report(term->m_mapperRange, syntax::DiagId::MacroRedefined, "Macro duplication '{}' and '{}'!", macro_name(term), macro->toString());

        macro->m_right = term->m_right;

    } else {

        // Новый макрос - регистрируем в текущем скоупе.
        macro = term;

        SequenceType* macro_list = FindMacroList(toMacroHash(macro));
        if (macro_list) {
            macro_list->push_back(macro);
        } else {
            // Создание нового макроса - в верхнем (текущем) скоупе.
            m_scopes.back()[toMacroHash(macro)].defs.push_back(macro);
        }
    }

    // Реестр имён макросов для LSP: определения не теряются после PopScope модуля.
    // Плюс классификация макроса для форматтера (no-paren/contract) хранится вместе с записью
    // группы (MacroEntry::kind) и коллбек в реальном времени.
    if (macro) {
        try {
            SequenceType mid = GetMacroId(macro);
            if (!mid.empty() && mid[0]) {
                m_ctx.recordMacro(mid[0]->getText(), macro->m_mapperRange);
                const MacroKind k = classifyMacro(macro);
                const std::string name = toMacroHashName(mid[0]->getText());
                // Группа могла существовать и раньше (в любом скоупе) - обновляем класс у её записи.
                if (MacroScope* sc = FindScope(name)) {
                    const auto it = sc->find(name);
                    if (it != sc->end()) {
                        it->second.kind = k;
                    }
                }
                if (on_macro_kind) {
                    on_macro_kind(name, k, /*removed=*/false, macro->m_mapperRange.begin.offset());
                }
            }
        } catch (...) {
        }
    }
    return macro;
}

bool Macro::RemoveMacro(TermPtr& term) {

    SequenceType list = GetMacroId(term);
    ASSERT(!list.empty());

    if (term->m_id == TermID::MACRO_DEL && list.size() == 1 && list[0]->getText().compare("_") == 0) {
        // Удаление всех макросов - очищаем все скоупы.
        if (on_macro_kind) {
            for (const auto& scope : m_scopes) {
                for (const auto& [name, entry] : scope) {
                    on_macro_kind(name, entry.kind, /*removed=*/true, term->m_mapperRange.begin.offset());
                }
            }
        }
        for (auto& scope : m_scopes) {
            scope.clear();
        }
        return true;
    }

    MacroScope* scope = FindScope(toMacroHash(term));
    if (!scope) {
        return false;
    }

    auto found = scope->find(toMacroHash(term));
    for (SequenceType::iterator iter = found->second.defs.begin(); iter != found->second.defs.end(); ++iter) {

        SequenceType names = GetMacroId(*iter);

        //        for (auto &elem : names) {
        //
        //        }

        if (names.size() != list.size()) {
            continue;
        }

        for (int pos = 0; pos < list.size(); pos++) {
            if (!CompareMacroName(list[pos]->getText().c_str(), names[pos]->getText().c_str())) {
                goto skip_remove;
            }
        }

        found->second.defs.erase(iter);

        // Если группа (имя) стала пустой - запись целиком удаляется, класс пропадает вместе с ней.
        // Если в группе остались другие арности, имя макроса всё ещё живо -> класс сохраняется
        // (раньше он ошибочно терялся при удалении одной из арностей группы).
        if (found->second.defs.empty()) {
            const MacroKind groupKind = found->second.kind;
            scope->erase(found);
            if (on_macro_kind) {
                on_macro_kind(toMacroHashName(list[0]->getText()), groupKind, /*removed=*/true, term->m_mapperRange.begin.offset());
            }
        }

        return true;

    skip_remove:;
    }
    return false;
}

bool Macro::isEmpty() const {
    for (const auto& scope : m_scopes) {
        if (!scope.empty()) {
            return false;
        }
    }
    return true;
}

size_t Macro::CountInScope(size_t scopeIdx) const {
    EXPECT(scopeIdx < m_scopes.size() && "Macro scope index out of range");
    size_t count = 0;
    for (const auto& [key, entry] : m_scopes[scopeIdx]) {
        count += entry.defs.size();
    }
    return count;
}

std::vector<std::string> Macro::MacroNames() const {
    std::set<std::string> names;
    for (const auto& scope : m_scopes) {
        for (const auto& [key, entry] : scope) {
            (void)entry;
            names.insert(key);
        }
    }
    return std::vector<std::string>(names.begin(), names.end());
}

std::string Macro::Dump() {
    std::string result;
    // Дамп по всем скоупам стека (снизу вверх).
    for (auto& scope : m_scopes) {
        for (auto iter = scope.begin(); iter != scope.end(); ++iter) {
            if (!result.empty()) {
                result += ", ";
            }

            for (int pos = 0; pos < iter->second.defs.size(); pos++) {

                std::string str;
                for (auto& elem : GetMacroId(iter->second.defs[pos])) {
                    if (!str.empty()) {
                        str += " ";
                    }
                    str += elem->getText();
                    if (iter->second.defs[pos]->isCall()) {
                        str += "(";
                    }
                }
                result += iter->first + "->'" + str + "'";
                if (pos + 1 < iter->second.defs.size()) {

                    result += "; ";
                }
            }
        }
    }
    return result;
}

std::string Macro::Dump(const MacroArgsType& var) {
    std::string result;
    auto iter = var.begin();
    while (iter != var.end()) {
        if (!result.empty()) {
            result += ", ";
        }

        std::string str;
        for (int pos = 0; pos < iter->second.size(); pos++) {
            if (!str.empty()) {
                str += " ";
            }
            str += iter->second[pos]->toString();
        }

        result += iter->first + "->'" + str + "'";
        iter++;
    }
    return result;
}

std::string Macro::Dump(const SequenceType& arr) {
    std::string result;
    auto iter = arr.begin();
    while (iter != arr.end()) {
        if (!result.empty()) {

            result += " ";
        }
        result += (*iter)->toString();
        iter++;
    }
    return result;
}

std::string Macro::DumpText(const SequenceType& arr) {
    std::string result;
    auto iter = arr.begin();
    while (iter != arr.end()) {
        if (!result.empty()) {

            result += " ";
        }
        result += (*iter)->getText();
        iter++;
    }
    return result;
}

bool Macro::CompareMacroName(const std::string& term_name, const std::string& macro_name) {
    return syntax::compareMacroName(term_name, macro_name);
}

/*
 * < first second >
 * first != first second
 * first second == first second
 * first second ( ) == first second
 *
 * < first second ( ) >
 * first != first second ( )
 * first second != first second ( ) <- Exception
 * first second ( ) == first second  ( )
 *
 * func name()
 * virtual func name()
 * override func name()
 * final func name()
 *
 * override virtual func name()
 * virtual override func name()
 * override final func name()
 * virtual final func name()
 * final override virtual func name()
 * final virtual override func name()
 *
 *
 * * func name()
 * virtual func name()
 * override func name()
 * final func name()
 *
 *
 * Классификация аннотаций Java
 * Аннотации можно классифицировать по количеству передаваемых в них параметров: без параметров, с одним параметром и с несколькими параметрами.
 * Маркерные аннотации - Маркерные аннотации не содержат никаких членов или данных. Для определения наличия аннотации можно использовать метод
 * isAnnotationPresent(). Аннотации с одним значением содержат только один атрибут, который принято называть value. Полные аннотации (из нескольких пар
 * "имя-значение". Например, Company(name = "ABC", city = "XYZ"). )
 *
 * Аннотации в NewLang всегда сожержат только один атрибут, маркертные имитируются установкой значения 1,
 * а полные аннотации не поддериватся (по крайнемй мера пока)
 *
 * Прагмы обработываются и доступны только во время компиляции,
 * а Аннотации доступна и во время компиляции и во время выполнения в виде системых?????? свойств объектов.
 *
 * Прагмы применяются для всего текущего окружения (экземпляра компилятора),
 * а аннотации только к одному создаваемому объекту (к следующей операции создания объекта/присвоения значения);
 *
 *
 *
 * @override
 * func name() { # ->   name() = {
 * };
 *
 * @override
 * \\nothrow
 * func name() { # ->   name() = {*
 * *};
 *
 *
 *
 *
 * @@ property(name, value) @@ := @@ @__ANNOTATION_SET__(@$name, @$value) @@
 *
 * @property(name, "ABC")
 * @property(city, "XYZ")
 *
 * @override -> @__ANNOTATION_SET__(override, 1)
 * @virtual  -> @__ANNOTATION_SET__(virtual, 1)  @__PRAGMA_PROP_SET__(virtual)
 * @final    -> @__ANNOTATION_SET__(final, 1)    @__PRAGMA_PROP_SET__(final)
 * @const    -> @__ANNOTATION_SET__(const, 1)    @__PRAGMA_PROP_SET__(const)
 * @data(1,2,3)    -> @__ANNOTATION_SET__(data, 1,2,3)
 * @data([1,2,3,])    -> @__ANNOTATION_SET__(data, [1,2,3,])
 * @data((1,2,3,))    -> @__ANNOTATION_SET__(data, (1,2,3,))
 *
 * @Company( name="ABC", city="XYZ" ) -> name="ABC"; city="XYZ";
 *
 * func name()
 *
 * @__ANNOTATION_IIF__()
 * @__PRAGMA_PROP_CHECK__(override, = , ::=)
 * @__PRAGMA_PROP_CHECK__(const, ^)
 * @__PRAGMA_PROP_TEST__(const, ^)
 * @__PRAGMA_PROP_TEST__(const, ^)
 *
 *
 * Запускать тесты
 * Определять тесты
 * Выполнять утверждения в тестах
 * @Test("Group", "Name", timeout = 100) {
 *
 * };
 *
 * @@ TEST_FATAL @@ ::= 1;
 * @@ TEST_NOT_FATAL @@ ::= 0;
 *
 * @@ TEST (...) @@ ::= @@ @__PRAGMA_TEST__(@$*)__;  @__PRAGMA_EXPECTED__( { ) @@
 *
 * @@ ASSERT_TRUE ( exp ) @@ ::= @@ @__PRAGMA_TEST_BOOL__(@$exp, @# @$exp, @TEST_FATAL, @__FILE_NAME__, @__FILE_LINE__) @@
 * @@ EXPECT_TRUE ( exp ) @@ ::= @@ @__PRAGMA_TEST_BOOL__(@$exp, @# @$exp, @TEST_NOT_FATAL, @__FILE_NAME__, @__FILE_LINE__) @@
 * @@ ASSERT_FALSE ( exp ) @@ ::= @@ @__PRAGMA_TEST_BOOL__(! @$exp, @# @$exp, @TEST_FATAL, @__FILE_NAME__, @__FILE_LINE__) @@
 * @@ EXPECT_FALSE ( exp ) @@ ::= @@ @__PRAGMA_TEST_BOOL__(! @$exp, @# @$exp, @TEST_NOT_FATAL, @__FILE_NAME__, @__FILE_LINE__) @@
 *
 * @@@@ TEST_FATAL @@@@;
 * @@@@ TEST_NOT_FATAL @@@@;
 *
 *
 */
size_t Macro::MatchMacro(const SequenceType& buffer, TermPtr& macro, MacroMismatch* mismatch) {
    return syntax::matchMacro(m_ctx, buffer, macro, mismatch);
}

bool Macro::IdentityMacro(const SequenceType& buffer, TermPtr& macro) {
    return syntax::identityMacro(m_ctx, buffer, macro);
}

void Macro::InsertArg_(MacroArgsType& args, std::string name, SequenceType& buffer, size_t pos) {
    syntax::insertArg(args, name, buffer, pos);
}

SequenceType Macro::SymbolSeparateArg_(const SequenceType& buffer, size_t pos, std::vector<std::string> sym, std::string& error) {
    return syntax::symbolSeparateArg(buffer, pos, sym, error);
}

size_t Macro::ExtractArgs(SequenceType& buffer, TermPtr& term, MacroArgsType& args) {
    return syntax::extractArgs(m_ctx, buffer, term, args);
}

SequenceType Macro::ExpandMacros(const TermPtr& macro, MacroArgsType& args, Parser& parser, MapperRange callRange) {
    return syntax::expandMacros(macro, args, parser, callRange);
}

std::string Macro::ExpandString(const TermPtr& macro, MacroArgsType& args) {
    return syntax::expandString(macro, args);
}

TermPtr Macro::GetMacroById(const SequenceType block) {
    return syntax::getMacroById(*this, block);
}

TermPtr Macro::GetMacro(std::vector<std::string> list) {
    return syntax::getMacro(*this, list);
}

// ════════════════════════════════════════════════════════════════
//  Классификация макросов для форматтера (MacroKind)
// ════════════════════════════════════════════════════════════════

// Рекурсивно проверяет, является ли терм (или его поддерево) контрактом:
// содержит raw-маркер @{ (TRUST_BEGIN) или ссылку на макрос, УЖЕ зарегистрированный как
// Contract (транзитивно — без хардкода имени: trust_contract → trust_pre → trust_assert...).
bool Macro::bodyHasContract(const TermPtr& t) const {
    return syntax::bodyHasContract(*this, t);
}

// Вычисляет класс макроса по его определению: m_sequence (сигнатура) и m_right (тело).
MacroKind Macro::classifyMacro(const TermPtr& macro) const {
    return syntax::classifyMacro(*this, macro);
}

MacroKind Macro::macroKind(std::string_view name) const {
    return syntax::macroKind(*this, name);
}

bool Macro::isNoParenMacro(std::string_view name) const {
    return syntax::isNoParenMacro(*this, name);
}

bool Macro::isContractMacro(std::string_view name) const {
    return syntax::isContractMacro(*this, name);
}

std::unordered_map<std::string, MacroKind> Macro::macroKinds() const {
    return syntax::macroKinds(*this);
}
