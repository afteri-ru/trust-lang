#include "syntax/macro_split.hpp"

#include "syntax/term.h"
#include "syntax/types.h"
#include "utils/strings.hpp"

#include <cstdint>
#include <cstdlib>
#include <set>

using namespace trust;

namespace trust {
namespace syntax {

bool checkMacro(trust::Context& ctx, const TermPtr& term) {
    const int err_before = ctx.diag().errorCount(); // мягкие ошибки валидации не должны допускать регистрацию
    // Некорректный (повреждённый) терм макроса НЕ должен валить процесс (ASSERT/abort):
    // сообщаем диагностику и не регистрируем макрос (см. EvalOpMacros: `if (!checkMacro) return`).
    if (!term) {
        ctx.diag().report(Severity::Error, MapperRange{}, "Macro term is null (internal parser error)");
        return false;
    }
    if (!term->m_right) {
        ctx.diag().report(Severity::Error, term->m_mapperRange, "Macro definition has no body!");
        return false;
    }
    if (term->m_sequence.empty()) {
        ctx.diag().report(Severity::Error, term->m_mapperRange, "Macro sequence is empty!");
        return false;
    }

    TermPtr op_term;
    bool is_operator = false;
    TermPtr args;               // Arguments @macro(name)
    std::set<std::string> tmpl; // Templates $name
    // Число вариативных `$...` в сигнатуре (операторный на конце + `$...`/`...` в скобках).
    // Конфликт - когда их несколько (коллизия ссылок `@$...`).
    int ellipsis_count = 0;

    const SequenceType idseq = makeMacroId(ctx, term->m_sequence);
    const size_t nid = idseq.size();
    for (size_t idxi = 0; idxi < nid; ++idxi) {
        auto& elem = idseq[idxi];
        if (elem->isCall()) {
            if (args) {
                ctx.diag().report(Severity::Error, elem->m_mapperRange, "Only one term in a macro can have arguments");
            }
            args = elem;
            // Локальный шаблон с аргументами (`$name(...)`): ParseTerm сливает `$name` и
            // `( $... )` в один терм. Имя шаблона - часть текста ДО '('. Регистрируем его
            // в tmpl, чтобы на него можно было ссылаться в теле как @$name.
            if (isLocalName(elem->getText())) {
                const std::string tname = elem->getText().substr(0, elem->getText().find('('));
                if (tmpl.find(tname) != tmpl.end()) {
                    ctx.diag().report(Severity::Error, elem->m_mapperRange, "Reuse of argument name!");
                }
                tmpl.insert(tname);
            }
        } else if (isLocalName(elem->getText())) {
            // `$...` - вариативная группа. В НЕ-операторных позициях её можно использовать
            // несколько раз (напр. `func $... ( ... ): $... {`), разделяя якорями `( ... )`/`:`/`{`;
            // группы различаются позиционно (@$2..., @$3...), поэтому повторный `$...` НЕ считаем
            // дубликатом имени. Дубликаты именованных шаблонов ($name) - ошибка.
            if (elem->getText().compare("$...") != 0) {
                if (tmpl.find(elem->getText()) != tmpl.end()) {
                    ctx.diag().report(Severity::Error, elem->m_mapperRange, "Reuse of argument name!");
                }
                tmpl.insert(elem->getText());
            }
            if (elem->getText().compare("$...") == 0) {
                ellipsis_count++;
                // `$...` - операторный шаблон ТОЛЬКО если он последний в сигнатуре
                // (напр. `extern $name $...`). В позиции типа возврата (`: $... {`)
                // он НЕ последний - это обычная вариативная группа, не оператор.
                if (idxi + 1 == nid) {
                    if (is_operator) {
                        ctx.diag().report(Severity::Error, elem->m_mapperRange, "Statement pattern should be only one!");
                    }
                    op_term = elem;
                    is_operator = true;
                }
            }
        } else if (elem->m_id == TermID::NAME || elem->m_id == TermID::MACRO) {
            // NAME - обычное имя; MACRO (`@foo`) - имя другого макроса в сигнатуре
            // (напр. `@@ @foo func ... @@`). Оба - допустимые термы имени.
            if (isReservedName(elem->getText())) {
                ctx.diag().report(Severity::Error, elem->m_mapperRange, "Reserved term name used!");
            }
            // OK
        } else if (elem->m_id == TermID::COLON || elem->m_id == TermID::LBRACE) {
            // `:` - разделитель типа возврата, `{` - терминатор сигнатуры (граница блока тела)
            // в сигнатуре (`@@ func $name ( ... ): $... { @@`). Термы-маркеры, тело не использует их.
            // OK
        } else {
            ctx.diag().report(Severity::Error, elem->m_mapperRange, "Unexpected term in macro!");
        }
    }

    if (is_operator) {
        if (term->m_sequence.back()->getText().compare("$...") != 0) {
            ctx.diag().report(Severity::Error, op_term->m_mapperRange, "Statement pattern must be the last term!");
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
                ctx.diag().report(Severity::Error, args->at(i).second->m_mapperRange, "Reuse of argument name!");
            }
            args_name.insert(args->at(i).first);
        } else {
            if (args->at(i).second->m_id == TermID::ELLIPSIS) {
                if (i + 1 != args->size()) {
                    ctx.diag().report(Severity::Error, args->at(i).second->m_mapperRange, "The ellipsis can only be the last in the list of arguments!");
                }
                arg_count--;
                is_ellips = true;
                ellipsis_count++;
                break;
            }
            if (named) {
                ctx.diag().report(Severity::Error, args->at(i).second->m_mapperRange, "Positional arguments must come before named arguments!");
            }
        }
        // LOCAL pattern names inside parentheses (e.g. $result in return($result))
        // are matched in the macro body as @$name - add them to the template set.
        if (isLocalName(args->at(i).second->getText())) {
            tmpl.insert(args->at(i).second->getText());
            if (args->at(i).second->getText().compare("$...") == 0) {
                ellipsis_count++; // `$...` внутри скобок (вариативная группа аргументов)
            }
        }
        if (args_name.find(args->at(i).second->getText()) != args_name.end()) {
            ctx.diag().report(Severity::Error, args->at(i).second->m_mapperRange, "Reuse of argument name!");
        }
        args_name.insert(args->at(i).second->getText());
    }

    // НЕотключаемые ошибки (макрос принципиально нераскрываем, `-Wno-...` нет):
    // 1) операторный `$...` (последний) + call-группа (один `$...`) - оператор поглощает аргументы вызова;
    // 2) несколько `$...` в сигнатуре (хвостовой операторный + `$...` в скобках) - коллизия `@$...`.
    // Для случая (2) подсказываем: после последнего `$...` добавить терм-маркер (напр. `{`), чтобы
    // `$...` стал вариативной группой с групповой адресацией вместо операторного шаблона.
    if (is_operator && args && ellipsis_count < 2) {
        ctx.diag().report(Severity::Error, args->m_mapperRange, "The statement macro cannot be a function call!");
    }
    if (is_operator && ellipsis_count >= 2) {
        ctx.diag().report(Severity::Error, op_term->m_mapperRange,
                          "Macro contains several '$...' (trailing operator '$...' plus another '$...' in brackets) - "
                          "they collide as '@$...' and cannot be disambiguated. Add a terminator lexeme "
                          "(e.g. '{{') right after the trailing '$...' so it becomes a variadic group "
                          "with group addressing (@$1.../@$2...) instead of a statement operator");
    }

    if (term->m_right->getTermID() == TermID::MACRO_SEQ) {

        //"@$"{name}      YY_TOKEN(MACRO_ARGNAME);

        //"@$..."         YY_TOKEN(MACRO_ARGUMENT);
        //"@$*"           YY_TOKEN(MACRO_ARGUMENT); - All OK

        //"@$"[0-9]+      YY_TOKEN(MACRO_ARGNUM);
        //"@$#"           YY_TOKEN(MACRO_ARGCOUNT); - All OK

        for (auto& elem : term->m_right->m_sequence) {
            if (elem->m_id == TermID::MACRO_ARGUMENT && elem->getText().compare("@$...") == 0) {
                // `@$...` в теле допустим при ЛЮБОЙ вариативной группе в сигнатуре (операторный
                // `$...`, скобочный эллипсис `( ... )` или внескобочная группа `$...` в НЕ-операторной
                // позиции, напр. `func $... {`): `@$...` резолвится в args или первую внескобочную
                // группу. Ошибка только когда `$...` в сигнатуре нет вовсе (фиксированная арность) —
                // тогда `@$...` некуда резолвить и макрос нераскрываем (fail-fast на определении).
                if (ellipsis_count == 0) {
                    ctx.diag().report(Severity::Error, elem->m_mapperRange, "The macro has a fixed number of arguments, ellipsis cannot be used!");
                }
            } else if (elem->m_id == TermID::MACRO_ARGPOS) {
                const std::string t = elem->getText(); // "@$N" или "@$G.N"
                const size_t dot = t.find('.');
                if (dot == std::string::npos) {
                    // @$N - позиционный аргумент группы 1 (N >= 1).
                    const int64_t num = std::strtol(t.c_str() + 2, nullptr, 10);
                    if (num <= 0 || num > arg_count) {
                        ctx.diag().report(Severity::Error, elem->m_mapperRange, "Invalid argument number!");
                    }
                } else {
                    // @$G.N - элемент N группы G.
                    const int64_t g = std::strtol(t.c_str() + 2, nullptr, 10);
                    const int64_t n = std::strtol(t.c_str() + static_cast<ptrdiff_t>(dot) + 1, nullptr, 10);
                    // Для фиксированной группы 1 (без эллипсиса) элемент не может превышать число
                    // фиксированных аргументов; для вариативной группы 1 (`( ... )`) и внескобочных
                    // групп (g>=2, размер неизвестен) допускается любой N>=1 (проверится при раскрытии).
                    if (n <= 0 || (g == 1 && !is_ellips && n > arg_count)) {
                        ctx.diag().report(Severity::Error, elem->m_mapperRange, "Invalid argument number!");
                    }
                }
            } else if (elem->m_id == TermID::MACRO_ARGNAME) {
                if (args_name.find(elem->getText().substr(2)) == args_name.end() && tmpl.find(elem->getText().substr(1)) == tmpl.end()) {
                    ctx.diag().report(Severity::Error, elem->m_mapperRange, "Macro argument name not found!");
                }
            }
        }
    }

    return ctx.diag().errorCount() == err_before;
}

bool bodyHasContract(const Macro& macro, const TermPtr& t) {
    if (!t) {
        return false;
    }
    if (t->getTermID() == TermID::TRUST_BEGIN) {
        return true;
    }
    // Ссылка на уже-зарегистрированный Contract-макрос.
    const std::string bare = std::string(trust::utils::strip_macro_sigil(t->getText()));
    if (const MacroScope* sc = macro.FindScope(bare)) {
        const auto it = sc->find(bare);
        if (it != sc->end() && hasKind(it->second.kind, MacroKind::Contract)) {
            return true;
        }
    }
    for (const auto& c : t->m_sequence) {
        if (bodyHasContract(macro, c)) {
            return true;
        }
    }
    if (bodyHasContract(macro, t->m_right)) {
        return true;
    }
    for (const auto& a : t->m_attr) {
        if (bodyHasContract(macro, a)) {
            return true;
        }
    }
    return false;
}

// Вычисляет класс макроса по его определению: m_sequence (сигнатура) и m_right (тело).
MacroKind classifyMacro(const Macro& macro, const TermPtr& macro_term) {
    MacroKind k = MacroKind::None;
    if (!macro_term) {
        return k;
    }
    // no-paren: в сигнатуре нет '('. (return/break — без скобок; if(..)/func(..) — со скобками.)
    bool hasParen = false;
    for (const auto& t : macro_term->m_sequence) {
        if (t && t->getTermID() == TermID::LPAREN) {
            hasParen = true;
            break;
        }
    }
    if (!hasParen) {
        k = static_cast<MacroKind>(static_cast<int>(k) | static_cast<int>(MacroKind::NoParen));
    }
    // contract: тело содержит @{...@} или ссылку на уже-зарегистрированный Contract-макрос.
    if (macro_term->m_right && bodyHasContract(macro, macro_term->m_right)) {
        k = static_cast<MacroKind>(static_cast<int>(k) | static_cast<int>(MacroKind::Contract));
    }
    return k;
}

MacroKind macroKind(const Macro& macro, std::string_view name) {
    const std::string bare = std::string(trust::utils::strip_macro_sigil(name));
    if (const MacroScope* sc = macro.FindScope(bare)) {
        const auto it = sc->find(bare);
        if (it != sc->end()) {
            return it->second.kind;
        }
    }
    return MacroKind::None;
}

bool isNoParenMacro(const Macro& macro, std::string_view name) {
    return hasKind(macroKind(macro, name), MacroKind::NoParen);
}

bool isContractMacro(const Macro& macro, std::string_view name) {
    return hasKind(macroKind(macro, name), MacroKind::Contract);
}

std::unordered_map<std::string, MacroKind> macroKinds(const Macro& macro) {
    std::unordered_map<std::string, MacroKind> result;
    for (const auto& scope : macro.scopes()) {
        for (const auto& [name, entry] : scope) {
            result[name] = entry.kind;
        }
    }
    return result;
}

} // namespace syntax
} // namespace trust
