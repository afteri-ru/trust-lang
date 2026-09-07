#include "syntax/macro_split.hpp"

#include "syntax/term.h"
#include "syntax/types.h"
#include "utils/strings.hpp"

using namespace trust;

namespace trust {
namespace syntax {

// Нормализация ключа группы макросов: локальный шаблон ($...) -> "$", имя макроса (@foo) -> "foo".
std::string toMacroHashName(const std::string& str) {
    if (isLocalName(str)) {
        return "$";
    } else if (isMacroName(str)) {
        return str.substr(1);
    }
    return str;
}

bool compareMacroName(const std::string& term_name, const std::string& macro_name) {
    // Шаблон ($..., $name) матчит ЛЮБОЙ терм входного буфера (включая `:`/`{`/`}` как значения).
    if (isLocalName(macro_name)) {
        return true;
    }
    // Терм-разделители/маркеры сигнатуры (`:` - тип возврата, `{`/`}` - терминатор блока)
    // сопоставляются ДОСЛОВНО: иначе isLocalAnyName(':') отклонял бы их (см. ниже).
    if (term_name == ":" || term_name == "{" || term_name == "}") {
        return term_name == macro_name;
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

size_t matchMacro(trust::Context& ctx, const SequenceType& buffer, TermPtr& macro, MacroMismatch* mismatch) {

    if (!macro || !macro->isMacro()) {
        if (mismatch) {
            mismatch->matched_terms = 0;
            mismatch->expected = "@@";
            mismatch->found = buffer.empty() ? "" : buffer[0]->getText();
        }
        return 0;
    }

    const SequenceType id = getMacroId(ctx, macro);

    // Запись ПЕРВОГО рассинхрона для диагностики «ни один шаблон не подошёл».
    auto record = [&](const std::string& expected, const std::string& found, size_t matched, size_t bpos) {
        if (mismatch) {
            mismatch->matched_terms = matched;
            mismatch->buffer_pos = bpos;
            mismatch->expected = expected;
            mismatch->found = found;
        }
    };
    // Ожидаемый терм сигнатуры в позиции off: литерал/шаблон; для call-терма - '(' (скобочная
    // группа аргументов); за концом сигнатуры - маркер "@@".
    auto expectText = [&](size_t off) -> std::string {
        if (off >= id.size()) {
            return "@@";
        }
        return id[off]->isCall() ? "(" : id[off]->getText();
    };

    size_t buff_offset = 0;
    size_t macro_offset = 0;
    while (buff_offset < buffer.size() && macro_offset < id.size()) {

        if (buffer[buff_offset]->getTermID() == TermID::END) {
            record(expectText(macro_offset), "@@ (END)", macro_offset, buff_offset);
            return 0;
        }
        // Шаблон (`$name`, `$...`) как терм сигнатуры захватывает терм ЗНАЧЕНИЯ/ИМЕНИ, но НЕ
        // разделитель `;`/END. Иначе `@return;`/`@break;` ошибочно сопоставлялись бы с
        // `return $value`/`break $label` (пустой аргумент), а не с arity-1 макросом.
        if (isLocalName(id[macro_offset]->getText()) &&
            (buffer[buff_offset]->getTermID() == TermID::SEMICOLON || buffer[buff_offset]->getTermID() == TermID::END)) {
            record(expectText(macro_offset), buffer[buff_offset]->getText(), macro_offset, buff_offset);
            return 0;
        }

        // Вариатическая внескобочная группа (`: $...`): сопоставляется с несколькими токенами
        // до СЛЕДУЮЩЕГО терма сигнатуры (напр. `{`), пропуская вложенные скобки (тип Tuple(a,b)).
        if (id[macro_offset]->getText().compare("$...") == 0 && macro_offset + 1 < id.size()) {
            while (buff_offset < buffer.size() && buffer[buff_offset]->getTermID() != TermID::END) {
                if (compareMacroName(buffer[buff_offset]->getText(), id[macro_offset + 1]->getText())) {
                    break; // граница (следующий терм сигнатуры)
                }
                if (buffer[buff_offset]->getTermID() == TermID::LPAREN || buffer[buff_offset]->getTermID() == TermID::LBRACKET) {
                    size_t skip = Parser::SkipBrackets(buffer, buff_offset);
                    if (!skip) {
                        record(expectText(macro_offset + 1), "@@ (unclosed bracket)", macro_offset, buff_offset);
                        return 0;
                    }
                    buff_offset += skip;
                } else {
                    buff_offset++;
                }
            }
            macro_offset++;
            if (macro_offset == id.size()) {
                return buff_offset;
            }
            continue;
        }

        if (!compareMacroName(buffer[buff_offset]->getText(), id[macro_offset]->getText())) {
            record(expectText(macro_offset), buffer[buff_offset]->getText(), macro_offset, buff_offset);
            return 0;
        } else {

            buff_offset++;

            if (id[macro_offset]->isCall()) {
                size_t skip = Parser::SkipBrackets(buffer, buff_offset);
                if (!skip) {
                    // На позиции либо НЕ `(`, либо незакрытая скобка; показываем фактический токен.
                    const std::string fnd = (buff_offset < buffer.size()) ? buffer[buff_offset]->getText() : "@@ (END)";
                    record(expectText(macro_offset), fnd, macro_offset, buff_offset);
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
    // Буфер или сигнатура исчерпаны до полного сопоставления.
    record(expectText(macro_offset), (buff_offset < buffer.size()) ? buffer[buff_offset]->getText() : "@@ (END)", macro_offset, buff_offset);
    return 0;
}

bool identityMacro(trust::Context& ctx, const SequenceType& buffer, TermPtr& macro) {
    return matchMacro(ctx, buffer, macro, nullptr) != 0;
}

std::string toMacroHash(trust::Context& ctx, TermPtr& term) {
    if (term->isMacro()) {
        ASSERT(!getMacroId(ctx, term).empty());
        return toMacroHashName(getMacroId(ctx, term)[0]->getText());
    }
    return toMacroHashName(term->getText());
}

TermID markerToken(const Macro& macro, std::string_view key) {
    // Имя принимаем и с '@', и без: группа макросов хранится по ключу без '@'
    // (toMacroHashName срезает префикс). View без копирования (heterogeneous find).
    if (!key.empty() && key.front() == '@') {
        key.remove_prefix(1);
    }
    const SequenceType* macro_list = macro.FindMacroList(key);
    if (!macro_list) {
        return TermID::END;
    }
    for (const auto& m : *macro_list) {
        if (!m || m->getTermID() != TermID::MACRO_SEQ || !m->m_right) {
            continue;
        }
        const SequenceType& body = m->m_right->m_sequence;
        if (body.size() != 1 || !body[0]) {
            continue;
        }
        const TermID id = body[0]->getTermID();
        if (id == TermID::MACRO_LEXEME || id == TermID::MACRO_STR_LEXEME || id == TermID::MACRO_DEL_LEXEME) {
            return id;
        }
    }
    return TermID::END;
}

} // namespace syntax
} // namespace trust
