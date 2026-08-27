#include "syntax/macro_split.hpp"

#include "syntax/term.h"
#include "syntax/types.h"
#include "utils/strings.hpp"

#include <set>
#include <utility>

using namespace trust;

namespace trust {
namespace syntax {

// Запрещённые символы в сигнатуре макроса (ранее - static Macro::deny_chars_from_macro).
// Содержит `, " ' , / | \ ( ) [ ] < > (backtick и кавычки - обычные символы).
namespace {
const std::string kDenyChars = ";"
                               "&^#?!+-%*~"
                               "\x60"
                               "\"',/|"
                               "\\"
                               "()[]<>";
} // namespace

SequenceType makeMacroId(trust::Context& ctx, const SequenceType& seq) {

    SequenceType result;
    size_t pos = 0;

    while (pos < seq.size()) {

        if (kDenyChars.find(seq[pos]->getText()[0]) != std::string::npos) {
            ctx.diag().report(Severity::Error, seq[pos]->m_mapperRange, "Symbol '{}' in lexem sequence not allowed!", seq[pos]->getText()[0]);
        }

        // Разделитель типа возврата (`@@ ... ): $ret @@`) и терминатор сигнатуры
        // (`@@ ... { @@`) - отдельные термы-маркеры (тип в элемент имени НЕ входит).
        if (seq[pos]->m_id == TermID::COLON || seq[pos]->m_id == TermID::LBRACE) {
            result.push_back(seq[pos]);
            pos++;
            continue;
        }

        // Прямая сборка элемента сигнатуры из лексем (без Parser::ParseTerm / ParseText /
        // add_source - новых источников не создаём, location берём из реальных лексем).
        // Группируем ТОЛЬКО скобочную группу, сразу следующую за именем (call-элемент):
        //   имя ( ... )  /  имя [ ... ]  ->  call-элемент: текст = имя (как в исходнике),
        //   аргументы группы кладём в m_args (isCall()=true). Голое имя (в т.ч. `$name`,
        //   `$...`, `@func`) - сам лексем-терм как есть.
        TermPtr term = seq[pos];
        size_t next = pos + 1;

        if (next < seq.size() && (seq[next]->getText() == "(" || seq[next]->getText() == "[")) {
            const size_t group = Parser::SkipBrackets(seq, next); // токенов, включая открывающую скобку
            const size_t innerEnd = next + group - 1;             // позиция закрывающей скобки

            TermPtr call = term->Clone();
            ArgsList args;
            size_t inner = next + 1;
            while (inner < innerEnd) {
                std::string argErr;
                SequenceType argSeq = symbolSeparateArg(seq, inner, {",", ")"}, argErr);
                if (!argSeq.empty()) {
                    // Аргумент сигнатуры. Поддерживаем:
                    //   - одиночный терм (шаблонное имя `$name`, `$...`, `...`);
                    //   - именованный с значением `name = value` (в т.ч. default): name - в .first,
                    //     value - первый терм части после '='.
                    std::string aName;
                    size_t vStart = 0;
                    for (size_t i = 0; i < argSeq.size(); ++i) {
                        if (argSeq[i]->getText() == "=") {
                            // собрать имя из токенов до '='
                            aName = argSeq[0]->getText();
                            for (size_t j = 1; j < i; ++j) {
                                aName += argSeq[j]->getText();
                            }
                            vStart = i + 1;
                            break;
                        }
                    }
                    TermPtr argTerm = argSeq.front();
                    if (vStart < argSeq.size()) {
                        argTerm = argSeq[vStart];
                    }
                    args.emplace_back(aName, argTerm);
                }
                inner += argSeq.size();
                if (inner < innerEnd && seq[inner]->getText() == ",") {
                    inner++; // пропустить разделитель
                }
            }
            call->m_args = std::move(args);
            call->m_mapperRange = MapperRange(seq[pos]->m_mapperRange.begin, seq[next + group - 1]->m_mapperRange.end);

            term = call;
            next = next + group;
        }

        result.push_back(term);
        pos = next;
    }
    return result;
}

SequenceType getMacroId(trust::Context& ctx, TermPtr& term) {
    if (!term->isMacro()) {
        ctx.diag().report(Severity::Error, term->m_mapperRange, "Term '{}' as {} not a macro!", term->toString(), trust::toString(term->m_id));
        return {}; // не продолжать обработку не-макроса (иначе ASSERT/UB)
    }

    // Новый формат: терм MACRO_SEQ с сигнатурой (именем) в m_sequence и телом в m_right
    // (собирается в Parser::GetNextToken, оператора `:=` нет).
    if (term->m_id == TermID::MACRO_DEL) {
        ASSERT(!term->m_sequence.empty());
        return makeMacroId(ctx, term->m_sequence);
    }

    ASSERT(term->m_id == TermID::MACRO_SEQ);
    ASSERT(term->m_right && "Macro definition has no body!");
    ASSERT(!term->m_sequence.empty() && "Macro sequence is empty!");

    return makeMacroId(ctx, term->m_sequence);
}

void insertArg(MacroArgsType& args, std::string name, SequenceType& buffer, size_t pos) {

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

SequenceType symbolSeparateArg(const SequenceType& buffer, size_t pos, std::vector<std::string> sym, std::string& error) {
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

size_t extractArgs(trust::Context& ctx, SequenceType& buffer, TermPtr& term, MacroArgsType& args) {

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
    // Внескобочные вариативные группы (`: $...`, `$...`): каждая - отдельный список токенов.
    // Группа 1 = скобочные аргументы (args_exta); группы 2..N = extraGroups[0..N-2].
    std::vector<SequenceType> extraGroups;
    // Элементы группы 1 (скобочные аргументы) по порядку - для ключей @$1.N (добавляются
    // только при наличии внескобочных групп, чтобы не менять подсчёт аргументов обычных макросов).
    std::vector<SequenceType> group1_elems;

    size_t arg_count = 0;
    size_t arg_offset = 0;

    bool arg_ellipsys = false;
    bool all_args_done = false;

    // Сигнатура макроса (термы имени). Кэшируется один раз - getMacroId строится из
    // m_sequence и стабилен на время extractArgs.
    const SequenceType id = getMacroId(ctx, term);

    // Извлечение скобочной группы аргументов (эллипсис/именованные/позиционные).
    // Вход: pos_buf указывает НА открывающую скобку '(' (имя уже потреблено).
    // Выход: pos_buf указывает НА закрывающую скобку ')' (внешний инкремент в цикле
    // продвинет его за неё). Используется и для обычного call-терма (`if(...)`), и для
    // локального шаблона с аргументами (`$name(...)`).
    auto extractParenGroup = [&]() {
        if (all_args_done) {
            throw ParserError("Support single term call only!");
        }
        all_args_done = true;

        if (pos_buf >= buffer.size() || buffer[pos_buf]->getTermID() != TermID::LPAREN) {
            throw trust::ParserError("Expected '('!");
        }
        pos_buf++;

        std::string error_str;
        SequenceType arg_seq;
        while (true) {
            arg_seq = symbolSeparateArg(buffer, pos_buf, {")", ","}, error_str);

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
                insertArg(args, arg_name, arg_seq);
                group1_elems.push_back(arg_seq); // для @$1.N (добавляются при наличии внескобочных групп)

                if (arg_count - 1 < id[pos_id]->size()) {

                    arg_name = id[pos_id]->at(arg_count - 1).second->getText();

                    if (arg_name.compare("...") == 0) {

                        if (arg_ellipsys) {
                            throw ParserError("Fail ellipsys args in prototype '%s'!", id[pos_id]->toString().c_str());
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
                        insertArg(args, arg_name, arg_seq);
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
    };

    while (pos_id < static_cast<int>(id.size())) {

        if (id[pos_id]->getText().compare("$...") == 0) {

            // Вариативная внескобочная группа (`: $...`, `$...`). Граница захвата:
            //   - если $... НЕ последний терм сигнатуры - до СЛЕДУЮЩЕГО терма (напр. `{`),
            //     пропуская вложенные скобки (тип Tuple(a,b));
            //   - если последний - до ';' / END (операторный макрос, как раньше).
            size_t stmt_start = pos_buf;
            if (pos_id + 1 < static_cast<int>(id.size())) {
                while (pos_buf < buffer.size() && buffer[pos_buf]->getTermID() != TermID::END) {
                    if (compareMacroName(buffer[pos_buf]->getText(), id[pos_id + 1]->getText())) {
                        break; // граница: следующий терм сигнатуры (например `{`)
                    }
                    if (buffer[pos_buf]->getTermID() == TermID::LPAREN || buffer[pos_buf]->getTermID() == TermID::LBRACKET) {
                        size_t skip = Parser::SkipBrackets(buffer, pos_buf);
                        if (!skip) {
                            throw trust::ParserError("Closed bracket not found!");
                        }
                        pos_buf += skip;
                    } else {
                        pos_buf++;
                    }
                }
            } else {
                while (pos_buf < buffer.size() && buffer[pos_buf]->getTermID() != TermID::END && buffer[pos_buf]->getText().compare(";") != 0) {
                    pos_buf++;
                }
            }
            extraGroups.emplace_back(buffer.begin() + static_cast<ptrdiff_t>(stmt_start), buffer.begin() + static_cast<ptrdiff_t>(pos_buf));

            if (pos_id + 1 >= static_cast<int>(id.size())) {
                pos_id = static_cast<int>(id.size());
                break; // $... - последний терм сигнатуры
            }
            // $... не последний: pos_buf указывает НА границу (следующий терм, напр. `{`).
            // Продвигаем pos_id мимо `$...` и `continue`, чтобы НЕ выполнять инкремент в конце
            // цикла (иначе pos_buf ушёл бы за границу, и следующий терм съел бы лишний токен).
            pos_id++;
            continue;

        } else if (isLocalName(id[pos_id]->getText())) {

            // Локальный шаблон ($name). Если за ним идут скобки (терм $name(...)), то имя
            // шаблона - часть текста ДО '(' (ParseTerm сливает `$name` и `( $... )` в один
            // терм). В этом случае шаблон фиксирует имя (один токен), а скобочная группа
            // обрабатывается как аргументы макроса.
            const bool with_paren = id[pos_id]->isCall();
            std::string tmpl_name = id[pos_id]->getText();
            if (with_paren) {
                tmpl_name = tmpl_name.substr(0, tmpl_name.find('('));
            }
            insertArg(args, tmpl_name, buffer, pos_buf);

            // Шаблон $name доступен в теле макроса также как @$name
            if (tmpl_name.compare("$...") != 0) {
                insertArg(args, "@" + tmpl_name, buffer, pos_buf);
            }

            if (with_paren) {
                // Потреблён токен значения шаблона (имя), pos_buf теперь указывает на '(' -
                // обработать скобочную группу. Для шаблона без скобок позицию продвигает
                // внешний инкремент в конце цикла.
                pos_buf++;
                extractParenGroup();
            }

        } else if (id[pos_id]->isCall()) {

            // pos_buf указывает на имя вызова - пропустить его, затем скобочная группа.
            pos_buf++;
            extractParenGroup();
        }

        pos_buf++;
        pos_id++;
    }

    SequenceType cnt{Term::Create(TermID::INTEGER, std::to_string(arg_count), {}, parser::token_type::INTEGER)};
    arg_name = "@$#";
    insertArg(args, arg_name, cnt);

    // `@$...` = скобочные аргументы, если они есть; иначе первая внескобочная группа
    // (операторный макрос `extern $name $...` - `@$...` = всё после имени).
    if (!args_exta.empty() || extraGroups.empty()) {
        insertArg(args, "@$...", args_exta);
    } else {
        insertArg(args, "@$...", extraGroups[0]);
    }

    // Групповая адресация: @$1... = все скобочные аргументы; @$k... = все элементы
    // внескобочной группы k (тип возврата и т.п.). Позволяет в теле различать
    // `( ... )` и `: $...` (которые оба называют себя `@$...`). Добавляем только когда
    // есть внескобочные группы (иначе @$... уже покрывает все аргументы и лишний ключ
    // меняет подсчёт аргументов для обычных макросов).
    if (!extraGroups.empty()) {
        insertArg(args, "@$1...", args_exta);
        // Количество ПЕРЕДАННЫХ аргументов группы 1 (@$#1) - для перебора по @$1.N.
        // (arg_count - число ФИКСИРОВАННЫХ аргументов; для `( ... )` оно 0, а переданных - group1_elems.)
        SequenceType cnt1{Term::Create(TermID::INTEGER, std::to_string(group1_elems.size()), {}, parser::token_type::INTEGER)};
        insertArg(args, "@$#1", cnt1);
        insertArg(args, "@$1.#", cnt1); // канон. форма: @$1.# = кол-во группы 1 (симм. @$1.N)
        // Элементы группы 1 по номеру (@$1.1, @$1.2, ...).
        for (size_t j = 0; j < group1_elems.size(); j++) {
            insertArg(args, "@$1." + std::to_string(j + 1), group1_elems[j]);
        }
        for (size_t g = 0; g < extraGroups.size(); g++) {
            std::string gkey = "@$" + std::to_string(g + 2) + "...";
            insertArg(args, gkey, extraGroups[g]);
            // Количество элементов внескобочной группы k (@$#k) - для перебора элементов группы.
            SequenceType cntg{Term::Create(TermID::INTEGER, std::to_string(extraGroups[g].size()), {}, parser::token_type::INTEGER)};
            insertArg(args, "@$#" + std::to_string(g + 2), cntg);
            insertArg(args, "@$" + std::to_string(g + 2) + ".#", cntg); // канон. форма: @$k.# = кол-во группы k
            // Элементы внескобочной группы по номеру: @$k.1 .. @$k.N (N - номер токена в группе).
            for (size_t j = 0; j < extraGroups[g].size(); j++) {
                SequenceType one{extraGroups[g][j]};
                std::string ekey = "@$" + std::to_string(g + 2) + "." + std::to_string(j + 1);
                insertArg(args, ekey, one);
            }
        }
    }

    args_dict.insert(args_dict.begin(), Term::CreateSymbol('('));
    args_dict.push_back(Term::CreateSymbol(','));
    args_dict.push_back(Term::CreateSymbol(')'));

    insertArg(args, "@$*", args_dict);

    if (((pos_id == static_cast<int>(getMacroId(ctx, term).size())) ||
         (term->getTermID() == TermID::MACRO_SEQ && pos_id == static_cast<int>(getMacroId(ctx, term).size()))) &&
        pos_buf + arg_offset <= buffer.size()) {
        ASSERT(pos_buf + arg_offset <= buffer.size());

        return pos_buf + arg_offset;
    }

    throw ParserError("Input buffer empty for extract args macros %s (%d+%d)=%d!", term->toString().c_str(), static_cast<int>(pos_buf),
                      static_cast<int>(arg_offset), static_cast<int>(buffer.size()));
}

} // namespace syntax
} // namespace trust
