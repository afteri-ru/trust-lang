#include "syntax/lexer.h"

#include "syntax/macro.h"
#include <set>
#include <vector>
#include "syntax/parser.h"
#include "diag/mapper.hpp"

using namespace trust;

const std::string Macro::deny_chars_from_macro(";@:&^#?!{}+-%*~`\"',/|\\()[]<>");

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

MacroScope* Macro::FindScope(const std::string& key) {
    for (auto iter = m_scopes.rbegin(); iter != m_scopes.rend(); ++iter) {
        if (iter->find(key) != iter->end()) {
            return &*iter;
        }
    }
    return nullptr;
}

const MacroScope* Macro::FindScope(const std::string& key) const {
    for (auto iter = m_scopes.rbegin(); iter != m_scopes.rend(); ++iter) {
        if (iter->find(key) != iter->end()) {
            return &*iter;
        }
    }
    return nullptr;
}

SequenceType* Macro::FindMacroList(const std::string& key) {
    MacroScope* scope = FindScope(key);
    return scope ? &scope->find(key)->second : nullptr;
}

const SequenceType* Macro::FindMacroList(const std::string& key) const {
    const MacroScope* scope = FindScope(key);
    return scope ? &scope->find(key)->second : nullptr;
}

std::string Macro::toMacroHash(TermPtr& term) {
    if (term->isMacro()) {
        ASSERT(!GetMacroId(term).empty());
        return toMacroHashName(GetMacroId(term)[0]->getText());
    }
    return toMacroHashName(term->getText());
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
            // Защита от бесконечной рекурсии при раскрытии макросов
            if (parser.m_macro_depth >= 100) {
                parser.m_ctx.diag().report(Severity::Fatal, macro_done->m_mapperRange, "Macro expansion '{}' stack overflow?", macro_done->toString());
            }

            // Новое дерево раскрытия макроса - сброс кэша гигиенических имён
            if (parser.m_macro_depth == 0) {
                parser.m_hygienic_names.clear();
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

            // Диапазон определения макроса: от начала имени (m_left) до конца тела.
            // Раньше бралось только тело (m_right), поэтому при переходе по ссылке
            // выделялась лишь часть макроса, а не весь макрос целиком.
            MapperRange def_range;
            if (macro_done->m_right->getTermID() == TermID::MACRO_SEQ && !macro_done->m_right->m_sequence.empty()) {
                def_range.end = macro_done->m_right->m_sequence.back()->m_mapperRange.end;
            } else {
                def_range.end = macro_done->m_right->m_mapperRange.end;
            }
            // Начало - имя макроса (m_left), но только если оно в том же файле, что и
            // конец тела (иначе получится кросс-файловый range → EXPECT). Для простых
            // алиасов, где имя в другом файле, оставляем начало тела (прежнее поведение).
            if (macro_done->m_left && !macro_done->m_left->m_mapperRange.begin.isInvalid() &&
                macro_done->m_left->m_mapperRange.begin.fileIdx() == def_range.end.fileIdx()) {
                def_range.begin = macro_done->m_left->m_mapperRange.begin;
            } else if (macro_done->m_right->getTermID() == TermID::MACRO_SEQ && !macro_done->m_right->m_sequence.empty()) {
                def_range.begin = macro_done->m_right->m_sequence.front()->m_mapperRange.begin;
            } else {
                def_range.begin = macro_done->m_right->m_mapperRange.begin;
            }

            // Реальный range замещаемого фрагмента (вызова макроса). Все вставленные лексемы
            // раскрытого тела должны получить именно его location - иначе клоны тела (из @dsl)
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

            parser.m_ctx.diag().report(Severity::Error, parser.m_macro_analisys_buff[0]->m_mapperRange,
                                       "Macro mapping '{}' not found!\nThe following macro mapping are available:\n{}",
                                       parser.m_macro_analisys_buff[0]->toString(),
                                       parser.m_macro->GetMacroMaping(parser.m_macro->toMacroHash(parser.m_macro_analisys_buff[0]), "\n"));
        }
    }
    return ExpandMacroResult::Break;
}

SequenceType Macro::MakeMacroId(const SequenceType& seq) {

    SequenceType result;
    size_t pos = 0;
    TermPtr term;
    size_t done;

    while (pos < seq.size()) {

        if (deny_chars_from_macro.find(seq[pos]->getText()[0]) != std::string::npos) {
            m_ctx.diag().report(Severity::Error, seq[pos]->m_mapperRange, "Symbol '{}' in lexem sequence not allowed!", seq[pos]->getText()[0]);
        }

        done = Parser::ParseTerm(term, seq, m_ctx, pos, true, /*macro_expand=*/false);
        if (done) {
            result.push_back(term);
            pos = done;
        } else {
            m_ctx.diag().report(Severity::Error, seq[pos]->m_mapperRange, "Fail convert {}", seq[pos]->toString());
            break; // не продвигаться вперёд нельзя (иначе бесконечный цикл); ошибка уже записана
        }
    }
    return result;
}

SequenceType Macro::GetMacroId(TermPtr& term) {
    if (!term->isMacro()) {
        m_ctx.diag().report(Severity::Error, term->m_mapperRange, "Term '{}' as {} not a macro!", term->toString(), trust::toString(term->m_id));
        return {}; // не продолжать обработку не-макроса (иначе ASSERT/UB)
    }

    if (term->m_id == TermID::MACRO_DEL) {
        ASSERT(!term->m_sequence.empty());
        return MakeMacroId(term->m_sequence);
    }

    ASSERT(term->isCreate());
    ASSERT(term->m_left);
    ASSERT(term->m_left->m_id == TermID::MACRO_SEQ);
    ASSERT(term->m_left->m_sequence.size() && "Macro sequence is empty!");

    return MakeMacroId(term->m_left->m_sequence);
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
    const int err_before = m_ctx.diag().errorCount(); // мягкие ошибки валидации не должны допускать регистрацию
    if (!term) {
        ASSERT(term);
    }
    ASSERT(term->m_left);
    ASSERT(term->m_right);

    TermPtr op_term;
    bool is_operator = false;
    TermPtr args;               // Arguments @macro(name)
    std::set<std::string> tmpl; // Templates $name

    ASSERT(!term->m_left->m_sequence.empty());
    for (auto& elem : MakeMacroId(term->m_left->m_sequence)) {
        if (elem->isCall()) {
            if (args) {
                m_ctx.diag().report(Severity::Error, elem->m_mapperRange, "Only one term in a macro can have arguments");
            }
            args = elem;
        } else if (isLocalName(elem->getText())) {
            if (tmpl.find(elem->getText()) != tmpl.end()) {
                m_ctx.diag().report(Severity::Error, elem->m_mapperRange, "Reuse of argument name!");
            }
            if (elem->getText().compare("$...") == 0) {
                if (is_operator) {
                    m_ctx.diag().report(Severity::Error, elem->m_mapperRange, "Statement pattern should be only one!");
                }
                op_term = elem;
                is_operator = true;
            }
            tmpl.insert(elem->getText());
        } else if (elem->m_id == TermID::NAME) {
            if (isReservedName(elem->getText())) {
                m_ctx.diag().report(Severity::Error, elem->m_mapperRange, "Reserved term name used!");
            }
            // OK
        } else {
            m_ctx.diag().report(Severity::Error, elem->m_mapperRange, "Unexpected term in macro!");
        }
    }

    if (is_operator) {
        if (term->m_left->m_sequence.back()->getText().compare("$...") != 0) {
            m_ctx.diag().report(Severity::Error, op_term->m_mapperRange, "Statement pattern must be the last term!");
        }
        if (args) {
            m_ctx.diag().report(Severity::Error, args->m_mapperRange, "The statement macro cannot be a function call!");
        }
    }

    std::set<std::string> args_name;

    int arg_count = args ? args->size() : 0;
    bool named = false;
    bool is_ellips = false;
    for (int i = 0; args && i < args->size(); i++) {
        if (!args->at(i).first.empty()) {
            named = true;
            if (args_name.find(args->at(i).first) != args_name.end()) {
                m_ctx.diag().report(Severity::Error, args->at(i).second->m_mapperRange, "Reuse of argument name!");
            }
            args_name.insert(args->at(i).first);
        } else {
            if (args->at(i).second->m_id == TermID::ELLIPSIS) {
                if (i + 1 != args->size()) {
                    m_ctx.diag().report(Severity::Error, args->at(i).second->m_mapperRange, "The ellipsis can only be the last in the list of arguments!");
                }
                arg_count--;
                is_ellips = true;
                break;
            }
            if (named) {
                m_ctx.diag().report(Severity::Error, args->at(i).second->m_mapperRange, "Positional arguments must come before named arguments!");
            }
        }
        // LOCAL pattern names inside parentheses (e.g. $result in return($result))
        // are matched in the macro body as @$name - add them to the template set.
        if (isLocalName(args->at(i).second->getText())) {
            tmpl.insert(args->at(i).second->getText());
        }
        if (args_name.find(args->at(i).second->getText()) != args_name.end()) {
            m_ctx.diag().report(Severity::Error, args->at(i).second->m_mapperRange, "Reuse of argument name!");
        }
        args_name.insert(args->at(i).second->getText());
    }

    if (term->m_right->getTermID() == TermID::MACRO_SEQ) {

        //"@$"{name}      YY_TOKEN(MACRO_ARGNAME);

        //"@$..."         YY_TOKEN(MACRO_ARGUMENT);
        //"@$*"           YY_TOKEN(MACRO_ARGUMENT); - All OK

        //"@$"[0-9]+      YY_TOKEN(MACRO_ARGNUM);
        //"@$#"           YY_TOKEN(MACRO_ARGCOUNT); - All OK

        for (auto& elem : term->m_right->m_sequence) {
            if (elem->m_id == TermID::MACRO_ARGUMENT && elem->getText().compare("@$...") == 0) {
                if (!is_ellips && !is_operator) {
                    m_ctx.diag().report(Severity::Error, elem->m_mapperRange, "The macro has a fixed number of arguments, ellipsis cannot be used!");
                }
            } else if (elem->m_id == TermID::MACRO_ARGPOS) {
                int64_t num = std::strtol(elem->getText().c_str() + 2, nullptr, 10);
                if (num >= arg_count) {
                    m_ctx.diag().report(Severity::Error, elem->m_mapperRange, "Invalid argument number!");
                }
            } else if (elem->m_id == TermID::MACRO_ARGNAME) {
                if (args_name.find(elem->getText().substr(2)) == args_name.end() && tmpl.find(elem->getText().substr(1)) == tmpl.end()) {
                    m_ctx.diag().report(Severity::Error, elem->m_mapperRange, "Macro argument name not found!");
                }
            }
        }
    }

    return m_ctx.diag().errorCount() == err_before;
}

TermPtr Macro::EvalOpMacros(TermPtr& term) {

    ASSERT(term);

    if (term->getTermID() == TermID::MACRO_DEL) {
        if (!RemoveMacro(term)) {
            fprintf(stderr, "W: Macro '%s' not found!\n", toMacroHash(term).c_str());
        }
        return term;
    }

    if (!term->m_left) {
        ASSERT(term->m_left);
    }
    if (term->m_left->getTermID() != TermID::MACRO_SEQ) {
        m_ctx.diag().report(Severity::Error, term->m_mapperRange, "Operand '{}' not a macros!", term->m_left->toString());
        return term; // не продолжать EvalOpMacros с не-макросом
    }

    if (term->m_right->getTermID() == TermID::MACRO_DEL || term->m_right->getTermID() == TermID::END) {
        m_ctx.diag().report(Severity::Error, term->m_mapperRange, "For remove macro use operator @@@@ {} @@@@;)", term->m_left->getText());
        return term;
    }

    if (!CheckMacro(term)) {
        return term; // макрос некорректен (мягкие ошибки) - не регистрируем
    }

    TermPtr macro = GetMacroById(GetMacroId(term));

    if (macro) {

        if (term->getTermID() == TermID::CREATE_TYPE) {
            m_ctx.report(term->m_mapperRange, OptKind::MacroRedefined, "Macros '{}' already exists!", term->m_left->toString());
        }

        // Список макросов, один из которых может соответствовать текущему буферу (по первому термину буфера)
        SequenceType* macro_list = FindMacroList(toMacroHash(term));
        if (macro_list) {
            for (auto iter = macro_list->begin(); iter != macro_list->end(); ++iter) {
                // Разная арность (число термов сигнатуры) - это РАЗНЫЕ макросы одной группы
                // (общий первый терм). Дубликат - только при полном совпадении сигнатуры.
                if (GetMacroId(*iter).size() != GetMacroId(term).size()) {
                    continue;
                }
                if (IdentityMacro(GetMacroId(term), *iter) || IdentityMacro(GetMacroId(*iter), term)) {

                    if (term->getTermID() == TermID::CREATE_TYPE || (iter->get() != macro.get())) {
                        m_ctx.report(term->m_mapperRange, OptKind::MacroRedefined, "Macro duplication '{}' and '{}'!", term->m_left->toString(),
                                     (*iter)->toString());
                    }
                }
            }
        }

        macro->m_right = term->m_right;

    } else {

        if (term->getTermID() == TermID::ASSIGN) {
            m_ctx.diag().report(Severity::Error, term->m_mapperRange, "Macros '{}' not exists!", term->m_left->toString());
        }

        TermPtr temp = term->m_left;
        // Список макросов, один из которых может соответствовать текущему буферу (по первому термину буфера)
        SequenceType* existing_list = FindMacroList(toMacroHash(temp));
        if (existing_list) {
            for (auto iter = existing_list->begin(); iter != existing_list->end(); ++iter) {
                TermPtr temp2 = *iter;
                // Разная арность - это РАЗНЫЕ макросы одной группы, не дубликаты.
                if (GetMacroId(*iter).size() != GetMacroId(term).size()) {
                    continue;
                }
                if (IdentityMacro(GetMacroId(term), *iter) || IdentityMacro(GetMacroId(temp2), term)) {
                    m_ctx.report(term->m_mapperRange, OptKind::MacroRedefined, "Macro duplication '{}' and '{}'!", term->m_left->toString(),
                                 (*iter)->toString());
                }
            }
        }

        //@todo Сделать два типа макросов, явные и не явные (::- :- или ::= :=), что позволит
        // контролировать обязательность указания признака макроса @ для явных макросов,
        // а не явные применять всегда (как сейчас)

        macro = term;

        SequenceType* macro_list = FindMacroList(toMacroHash(macro));
        if (macro_list) {
            macro_list->push_back(macro);
        } else {
            // Создание нового макроса - в верхнем (текущем) скоупе.
            m_scopes.back()[toMacroHash(macro)].push_back(macro);
        }
    }

    // Реестр имён макросов для LSP: определения не теряются после PopScope модуля.
    if (macro) {
        try {
            SequenceType mid = GetMacroId(macro);
            if (!mid.empty() && mid[0]) {
                m_ctx.recordMacro(mid[0]->getText(), macro->m_mapperRange);
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
    for (SequenceType::iterator iter = found->second.begin(); iter != found->second.end(); ++iter) {

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

        found->second.erase(iter);

        if (found->second.empty()) {
            scope->erase(list[0]->getText().c_str());
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
    for (const auto& [key, list] : m_scopes[scopeIdx]) {
        count += list.size();
    }
    return count;
}

std::vector<std::string> Macro::MacroNames() const {
    std::set<std::string> names;
    for (const auto& scope : m_scopes) {
        for (const auto& [key, list] : scope) {
            (void)list;
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

            for (int pos = 0; pos < iter->second.size(); pos++) {

                std::string str;
                for (auto& elem : GetMacroId(iter->second[pos])) {
                    if (!str.empty()) {
                        str += " ";
                    }
                    str += elem->getText();
                    if (iter->second[pos]->isCall()) {
                        str += "(";
                    }
                }
                result += iter->first + "->'" + str + "'";
                if (pos + 1 < iter->second.size()) {

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
    if (isLocalName(macro_name)) {
        // Шаблон соответствует любому термину входного буфера
        return true;
    }
    if (isMacroName(term_name)) {
        // Если термин в буфере - имя макроса
        if (isMacroName(macro_name)) {
            return macro_name.compare(term_name) == 0;
        }
        // Префикс макроса не учавтствует в сравнении
        return macro_name.compare(&term_name.c_str()[1]) == 0;

    } else if (isMacroName(macro_name)) {
        // Если термин в буфере - имя макроса
        if (isMacroName(term_name)) {
            return macro_name.compare(term_name) == 0;
        }
        // Префикс макроса не учавтствует в сравнении
        return term_name.compare(&macro_name.c_str()[1]) == 0;

    } else if (isLocalAnyName(term_name.c_str())) {
        // Любой другой термин не подходит

        return false;
    }
    // Без префиксов оба термина
    return term_name.compare(macro_name) == 0;
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
 * @@ override @@ := @@ @__ANNOTATION_SET__(override, 1) @__PRAGMA_UNEXPECTED__( (, <, [, {, {+, {-, {*) @@
 * @@ nothrow @@ := @@ @__ANNOTATION_SET__(nothrow, 1) @__PRAGMA_UNEXPECTED__( (, <, [, {, {+, {-, {*) @@
 * @@ func $name (...) @@ := @@ @$name ( @$* )  @__ANNOTATION_IIF__(override, =, ::=)
 *                                              @__ANNOTATION_IF_EXPECTED__(nothrow, {*)
 *                                              @__ANNOTATION_ELSE_EXPECTED__(nothrow, {, {+, {-, {*)  @@
 *
 * @@ __name__ @@ ::= @@ . @__PRAGMA_NO_MACRO__()  __name__ @@ # -> \.__name__
 *
 * @__PRAGMA_INDENT_BLOCK__( +} )
 *
 *
 * func name() { # ->   name() ::= {
 * };
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
size_t Macro::MatchMacro(const SequenceType& buffer, TermPtr& macro) {

    if (!macro || !macro->isMacro()) {
        return 0;
    }

    const SequenceType id = GetMacroId(macro);

    size_t buff_offset = 0;
    size_t macro_offset = 0;
    while (buff_offset < buffer.size() && macro_offset < id.size()) {

        if (buffer[buff_offset]->getTermID() == TermID::END) {
            return 0;
        }
        // Шаблон (`$name`, `$...`) как терм сигнатуры захватывает терм ЗНАЧЕНИЯ/ИМЕНИ, но НЕ
        // разделитель `;`/END. Иначе `@return;`/`@break;` ошибочно сопоставлялись бы с
        // `return $value`/`break $label` (пустой аргумент), а не с arity-1 макросом.
        if (isLocalName(id[macro_offset]->getText()) &&
            (buffer[buff_offset]->getTermID() == TermID::SEMICOLON || buffer[buff_offset]->getTermID() == TermID::END)) {
            return 0;
        }

        if (!CompareMacroName(buffer[buff_offset]->getText(), id[macro_offset]->getText())) {
            return 0;
        } else {

            buff_offset++;

            if (id[macro_offset]->isCall()) {
                size_t skip = Parser::SkipBrackets(buffer, buff_offset);
                if (!skip) {
                    return 0;
                }
                buff_offset += skip;
            }
        }

        macro_offset++;

        if (macro_offset == id.size()) {
            // Сигнатура макроса сопоставлена полностью (возможно, буфер потреблён не целиком).
            // Возвращаем число потреблённых термов буфера - метрика longest-match при выборе
            // макроса из группы (все макросы с одним первым именем, но разной арностью).
            return buff_offset;
        }
    }
    return 0;
}

bool Macro::IdentityMacro(const SequenceType& buffer, TermPtr& macro) {
    return MatchMacro(buffer, macro) != 0;
}

void Macro::InsertArg_(MacroArgsType& args, std::string name, SequenceType& buffer, size_t pos) {

    if (args.find(name) != args.end()) {
        throw ParserError("Duplicate arg %s!", name.c_str());
    }

    SequenceType vect;
    if (pos == static_cast<size_t>(-1)) {
        for (auto& elem : buffer) {
            vect.push_back(elem);
        }
    } else {
        if (pos >= buffer.size()) {
            throw ParserError("No data for input buffer! Pos %d for size %d!", static_cast<int>(pos), static_cast<int>(buffer.size()));
        }
        vect.push_back(buffer[pos]);
    }
    args.insert({name, std::move(vect)});
}

SequenceType Macro::SymbolSeparateArg_(const SequenceType& buffer, size_t pos, std::vector<std::string> sym, std::string& error) {
    error.clear();
    SequenceType result;
    size_t skip;
    while (pos < buffer.size()) {
        for (auto& elem : sym) {
            if (buffer[pos]->getTermID() == Term::symbolToID(elem[0])) {
                return result;
            }
        }

        skip = Parser::SkipBrackets(buffer, pos);
        for (int i = 0; skip && i < static_cast<int>(skip) - 1; i++) {
            result.push_back(buffer[pos]->Clone());
            pos++;
        }

        result.push_back(buffer[pos]->Clone());
        pos++;
    }

    for (auto& elem : sym) {
        if (!error.empty()) {
            error += " or ";
        }
        error += "'";
        error += elem;
        error += "'";
    }

    error.insert(0, "Expected symbol ");
    error += "!";

    return result;
}

size_t Macro::ExtractArgs(SequenceType& buffer, TermPtr& term, MacroArgsType& args) {

    ASSERT(term);

    if (!term->isMacro()) {
        throw ParserError("Term is not a macro! '%s'", term->toString().c_str());
    }

    args.clear();

    size_t pos_buf = 0;
    int pos_id = 0;

    std::string arg_name;
    SequenceType args_dict;
    SequenceType args_exta;

    size_t arg_count = 0;
    size_t arg_offset = 0;

    bool arg_ellipsys = false;
    bool all_args_done = false;

    while (pos_id < GetMacroId(term).size()) {

        if (GetMacroId(term)[pos_id]->getText().compare("$...") == 0) {

            size_t stmt_start = pos_buf;
            for (int i = pos_buf; i < buffer.size(); i++) {
                if (buffer[pos_buf]->getTermID() == TermID::END || buffer[pos_buf]->getText().compare(";") == 0) {
                    break;
                }
                pos_buf++;
            }

            args_exta.insert(args_exta.end(), buffer.begin() + stmt_start, buffer.begin() + pos_buf);
            pos_id = GetMacroId(term).size();
            break;

        } else if (isLocalName(GetMacroId(term)[pos_id]->getText())) {

            std::string tmpl_name = GetMacroId(term)[pos_id]->getText();
            InsertArg_(args, tmpl_name, buffer, pos_buf);

            // Шаблон $name доступен в теле макроса также как @$name
            if (tmpl_name.compare("$...") != 0) {
                InsertArg_(args, "@" + tmpl_name, buffer, pos_buf);
            }

        } else if (GetMacroId(term)[pos_id]->isCall()) {

            if (all_args_done) {
                throw ParserError("Support single term call only!");
            }
            all_args_done = true;

            pos_buf++;
            if (pos_buf >= buffer.size() || buffer[pos_buf]->getTermID() != TermID::LPAREN) {
                throw trust::ParserError("Expected '('!");
            }
            pos_buf++;

            std::string error_str;
            SequenceType arg_seq;
            while (1) {
                arg_seq = SymbolSeparateArg_(buffer, pos_buf, {")", ","}, error_str);

                if (!error_str.empty()) {
                    throw trust::ParserError(error_str);
                }

                if (!arg_seq.empty()) {

                    args_dict.insert(args_dict.end(), arg_seq.begin(), arg_seq.end());

                    pos_buf += arg_seq.size();

                    ASSERT(pos_buf < buffer.size());

                    arg_count++;

                    arg_name = "@$";
                    arg_name += std::to_string(arg_count);
                    InsertArg_(args, arg_name, arg_seq);

                    if (arg_count - 1 < GetMacroId(term)[pos_id]->size()) {

                        arg_name = GetMacroId(term)[pos_id]->at(arg_count - 1).second->getText();

                        if (arg_name.compare("...") == 0) {

                            if (arg_ellipsys) {
                                throw ParserError("Fail ellipsys args in prototype '%s'!", GetMacroId(term)[pos_id]->toString().c_str());
                            }
                            arg_ellipsys = true;

                        } else {

                            // Имя аргумента из прототипа может быть записано как с ведущим '$'
                            // ($name), так и без него (name). Тело макроса всегда ссылается
                            // на аргумент как @$name, поэтому ключ в args нормализуется:
                            //   $a  -> @$a
                            //   arg -> @$arg
                            if (!arg_name.empty() && arg_name[0] == '$') {
                                arg_name.insert(0, "@");
                            } else {
                                arg_name.insert(0, "@$");
                            }
                            InsertArg_(args, arg_name, arg_seq);
                        }
                    }

                    if (arg_ellipsys) {
                        if (!args_exta.empty()) {
                            args_exta.insert(args_exta.end(), Term::CreateSymbol(','));
                        }
                        args_exta.insert(args_exta.end(), arg_seq.begin(), arg_seq.end());
                    }

                } else if (buffer[pos_buf]->getText() == ",") {
                    args_dict.push_back(buffer[pos_buf]);
                    pos_buf++;
                } else if (buffer[pos_buf]->getText() == ")") {
                    break;
                } else {
                    throw trust::ParserError(std::string("Unexpected symbol ") + buffer[pos_buf]->getText());
                }
            }
        }

        pos_buf++;
        pos_id++;
    }

    SequenceType cnt{Term::Create(TermID::INTEGER, std::to_string(arg_count), {}, parser::token_type::INTEGER)};
    arg_name = "@$#";
    InsertArg_(args, arg_name, cnt);

    InsertArg_(args, "@$...", args_exta);

    args_dict.insert(args_dict.begin(), Term::CreateSymbol('('));
    args_dict.push_back(Term::CreateSymbol(','));
    args_dict.push_back(Term::CreateSymbol(')'));

    InsertArg_(args, "@$*", args_dict);

    // std::string ttt;
    // for (size_t j = 0; j < GetMacroId(term).size(); j++) {
    //     if (j) {
    //         ttt += " ";
    //     }
    //     ttt += Macro::toMacroHashName(GetMacroId(term)[j]->getText());
    // }

    if (((pos_id == GetMacroId(term).size()) || (term->getTermID() == TermID::MACRO_SEQ && pos_id == GetMacroId(term).size())) &&
        pos_buf + arg_offset <= buffer.size()) {
        ASSERT(pos_buf + arg_offset <= buffer.size());

        return pos_buf + arg_offset;
    }

    throw ParserError("Input buffer empty for extract args macros %s (%d+%d)=%d!", term->toString().c_str(), static_cast<int>(pos_buf),
                      static_cast<int>(arg_offset), static_cast<int>(buffer.size()));
}

SequenceType Macro::ExpandMacros(const TermPtr& macro, MacroArgsType& args, Parser& parser, MapperRange callRange) {
    ASSERT(macro);
    ASSERT(macro->m_right);

    SequenceType result;

    SequenceType seq = macro->m_right->m_sequence;

    if (macro->m_right->getTermID() != TermID::MACRO_SEQ) {
        ASSERT(seq.empty());
        seq.push_back(macro->m_right);
    }

    for (int i = 0; i < seq.size(); i++) {

        if (seq[i]->getTermID() == TermID::COMMA_LEXEME) {
            // Лексема @\, (COMMA_LEXEME) в теле макроса при раскрытии превращается
            // в обычную запятую (TermID::COMMA), чтобы можно было вставить ',' там,
            // где запятая не должна разделять аргументы самого макроса.
            TermPtr comma = Term::CreateSymbol(',');
            comma->m_mapperRange = callRange;
            result.push_back(comma);
            continue;
        }

        if (seq[i]->getTermID() == TermID::MACRO_TOSTR) {

            if (i + 1 >= seq.size()) {
                throw ParserError("Next element to string not found!");
            }
            result.insert(result.end(), seq[i + 1]->Clone());

            if ((*result.rbegin())->getText().find("@$") == 0) {
                auto iter = args.find((*result.rbegin())->getText());
                if (iter == args.end()) {
                    throw ParserError("Argument name '%s' not found!", seq[i]->getText().c_str());
                }
                std::string text;
                for (auto& elem : iter->second) {
                    text += std::string(elem->getText());
                }
                (*result.rbegin())->getText() = text;
            }

            if (seq[i]->getText().compare("@#\"") == 0) {
                (*result.rbegin())->m_id = TermID::STRWIDE;
            } else if (seq[i]->getText().compare("@#'") == 0) {
                (*result.rbegin())->m_id = TermID::STRCHAR;
            } else {
                ASSERT(seq[i]->getText().compare("@#") == 0);
                // bare @# - стрингификация по умолчанию: узкая строка (StrChar),
                (*result.rbegin())->m_id = TermID::STRCHAR;
            }
            i++;

        } else if (seq[i]->getTermID() == TermID::MACRO_CONCAT) {
            ASSERT(seq[i]->getText().compare("@##") == 0);

            if (result.empty() || i + 1 >= seq.size()) {
                throw ParserError("Concat elements not exist!");
            }

            // Если предыдущий элемент - предопределённый макрос (например @__MODULE_NAME__),
            // раскрыть его до склейки с локацией сайта вызова, иначе конкатенация склеит
            // сырое имя макроса (напр. "@__MODULE_NAME__" + "__main__") и результат не раскроется.
            if (result.back()->getTermID() == TermID::MACRO && result.back()->getText().find("@") == 0) {
                result.back()->m_mapperRange = callRange;
                parser.ExpandPredefMacro(result.back());
            }

            std::string append_text;
            if (seq[i + 1]->getText().find("@$") == 0) {
                auto iter = args.find(seq[i + 1]->getText());
                if (iter == args.end()) {
                    throw ParserError("Argument name '%s' not found!", seq[i + 1]->getText().c_str());
                }
                for (auto& elem : iter->second) {
                    append_text += std::string(elem->getText());
                }
            } else {
                append_text = std::string(seq[i + 1]->getText());
            }
            (*result.rbegin())->getText().append(append_text);
            i++;

        } else if (seq[i]->getText().find("@$") == 0) {

            auto iter = args.find(seq[i]->getText());
            if (iter == args.end()) {
                throw ParserError("Argument name '%s' not found!", seq[i]->getText().c_str());
            }

            if (seq[i]->getText().compare("@$...") == 0) {

                if (iter->second.empty() && result.rbegin() != result.rend() && std::string((*result.rbegin())->getText()) == ",") {
                    result.erase(std::prev(result.end()));
                }
            }

            for (auto& elem : iter->second) {
                result.push_back(elem);
            }

        } else {

            result.insert(result.end(), seq[i]->Clone());
        }
    }
    return result;
}

std::string ReplaceAll(std::string str, const std::string& from, const std::string& to) {
    size_t start_pos = 0;
    while ((start_pos = str.find(from, start_pos)) != std::string::npos) {

        str.replace(start_pos, from.length(), to);
        start_pos += to.length();
    }
    return str;
}

std::string Macro::ExpandString(const TermPtr& macro, MacroArgsType& args) {
    ASSERT(macro);
    ASSERT(macro->m_right);
    if (macro->m_right->m_id != TermID::MACRO_STR) {
        throw ParserError("Fail convert term type %s as macros string!", toString(macro->m_right->m_id));
    }

    std::string body(macro->m_right->getText());

    for (auto& elem : args) {

        std::string text;
        for (auto& lex : elem.second) {

            text += lex->toString();
            text += " ";
        }

        body = ReplaceAll(body, elem.first, text);
    }

    return body;
}

TermPtr Macro::GetMacroById(const SequenceType block) {
    std::vector<std::string> list;
    for (auto& elem : block) {

        list.push_back(elem->getText());
    }
    return GetMacro(list);
}

TermPtr Macro::GetMacro(std::vector<std::string> list) {
    if (list.empty()) {
        return nullptr;
    }
    SequenceType* macro_list = FindMacroList(toMacroHashName(list[0]));
    if (!macro_list) {
        return nullptr;
    }

    for (SequenceType::iterator iter = macro_list->begin(); iter != macro_list->end(); ++iter) {

        SequenceType names = GetMacroId(*iter);

        if (names.size() != list.size()) {
            continue;
        }

        for (int pos = 0; pos < list.size(); pos++) {
            if (!CompareMacroName(list[pos].c_str(), names[pos]->getText().c_str())) {

                goto skip_step;
            }
        }
        return *iter;
    skip_step:;
    }
    return nullptr;
}
