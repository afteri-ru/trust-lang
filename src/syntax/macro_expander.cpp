#include "syntax/macro_split.hpp"

#include "syntax/term.h"
#include "syntax/types.h"
#include "utils/strings.hpp"

#include <utility>

using namespace trust;

namespace trust {
namespace syntax {

namespace {

std::string replaceAll(std::string str, const std::string& from, const std::string& to) {
    size_t start_pos = 0;
    while ((start_pos = str.find(from, start_pos)) != std::string::npos) {
        str.replace(start_pos, from.length(), to);
        start_pos += to.length();
    }
    return str;
}

} // namespace

SequenceType expandMacros(const TermPtr& macro, MacroArgsType& args, Parser& parser, MapperRange callRange) {
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
                parser.predef().expandPredefMacro(result.back());
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

std::string expandString(const TermPtr& macro, MacroArgsType& args) {
    ASSERT(macro);
    ASSERT(macro->m_right);
    if (macro->m_right->m_id != TermID::MACRO_STR) {
        throw ParserError("Fail convert term type %s as macros string!", toString(macro->m_right->m_id));
    }

    std::string body(macro->m_right->getText());

    // Двухпроходная замена: сначала ключи с сигнатурой `@` (например `@$name`, `@$...`),
    // затем bare-ключи (`$name`, `$...`). Иначе замена bare-ключа `$name` испортит
    // `@$name` (подстрока) в `@myfunc`, и ссылка на аргумент перестанет раскрываться.
    auto substitute = [&](char prefix) {
        for (auto& elem : args) {
            if (!elem.first.empty() && elem.first[0] != prefix) {
                continue;
            }
            std::string text;
            for (auto& lex : elem.second) {
                text += lex->toString();
                text += " ";
            }
            body = replaceAll(body, elem.first, text);
        }
    };
    substitute('@'); // сначала `@$name`, `@$...`, `@$1`, ...
    substitute('$'); // затем bare `$name`, `$...`

    return body;
}

TermPtr getMacroById(Macro& macro, const SequenceType block) {
    std::vector<std::string> list;
    for (auto& elem : block) {
        list.push_back(elem->getText());
    }
    return getMacro(macro, list);
}

TermPtr getMacro(Macro& macro, std::vector<std::string> list) {
    if (list.empty()) {
        return nullptr;
    }
    SequenceType* macro_list = macro.FindMacroList(macro.toMacroHashName(list[0]));
    if (!macro_list) {
        return nullptr;
    }

    for (SequenceType::iterator iter = macro_list->begin(); iter != macro_list->end(); ++iter) {

        SequenceType names = getMacroId(macro.m_ctx, *iter);

        if (names.size() != list.size()) {
            continue;
        }

        for (int pos = 0; pos < list.size(); pos++) {
            if (!compareMacroName(list[pos].c_str(), names[pos]->getText().c_str())) {
                goto skip_step;
            }
        }
        return *iter;
    skip_step:;
    }
    return nullptr;
}

} // namespace syntax
} // namespace trust
