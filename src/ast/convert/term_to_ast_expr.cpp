// Term->AST conversion (expression conversions).

#include "ast/term_to_ast.hpp"
#include "ast/convert/term_to_ast_internal.hpp"
#include "ast/ast_nodes.hpp"
#include "diag/diag.hpp"
#include "attrs/attr_builtin.hpp"
#include "ast/nodes/attr_parser.hpp"
#include "syntax/check_area.hpp"
#include "ast/ident_name.hpp"
#include "ast/token_base.hpp"
#include "ast/token_type.hpp"
#include "syntax/term/term.h"
#include "syntax/term/term_types.h"
#include "types/typekind.hpp"
#include "utils/error.hpp"
#include "utils/strings.hpp"

#include <memory>
#include <optional>
#include <type_traits>
#include <vector>

namespace trust {

AstNodePtr TermToAstConverter::visit_SWAP(const trust::TermPtr& term, ConvertServices& ctx) {
    // `x :=: y` - swap двух ссылок/переменных. Единый узел Binary(AssignOp) с оператором ":=":;
    // m_left/m_right строит терм-конструктор Binary из term->m_left/m_right (AppendLeft/Right в
    // грамматике assign_seq SWAP). text() узла = ":=:" (BinaryOp::Swap - m_op, используется семантикой/транспилятором).
    return std::make_shared<Binary>(ParserToken::Kind::AssignOp, term, &ctx);
}

AstNodePtr TermToAstConverter::visit_ALIAS_NAME(const trust::TermPtr& term, ConvertServices& ctx) {
    // `f :- <target>;` - АЛИАС ИМЕНИ: новое имя существующей сущности (набор перегрузок цели).
    // Новая сущность не создаётся: узел FuncDecl несёт только привязку (`aliasTarget()`),
    // C++-функция не эмитится. Сигил типа на LHS - это алиас ТИПА (`::-`), другой оператор.
    if (!term->m_left || !term->m_right) {
        ctx.diag().report(Severity::Error, term->m_mapperRange, "name alias requires a name on the left and a target on the right");
        return nullptr;
    }
    if (term->m_left->getTermID() == trust::TermID::TYPE) {
        ctx.diag().report(Severity::Error, term->m_left->m_mapperRange,
                          "type alias uses '::-': write ':A ::- T;' (the ':-' operator aliases a name, not a type)");
        return nullptr;
    }
    if (term->m_left->m_args.has_value()) {
        ctx.diag().report(Severity::Error, term->m_mapperRange,
                          "name alias must not repeat the signature; write 'name :- target;' (the target overload set is used)");
        return nullptr;
    }
    // Алиас (`f :- g;`, `f :- ns::g;`, `a :- ns::;`, `:A ::- T;`): LHS — NAME.
    // Вид различается ЦЕЛЬЮ: завершающий `::` в цели — алиас ОБЛАСТИ имён (`a :- ns::;` -> `a::X`),
    // иначе — алиас имени/значения.
    const bool areaTarget = term->m_right->getTermID() == trust::TermID::NAMESPACE;
    AstNodePtr target;
    if (areaTarget) {
        const std::string_view area = utils::namespaceAliasName(term->m_right->getText());
        auto ident = std::make_shared<IdentName>(ParserToken::Kind::Ident, term->m_right, &ctx);
        ident->set_text_interned(area);
        target = std::move(ident);
    } else {
        target = convert(term->m_right);
    }
    auto alias = std::make_shared<FuncDecl>(std::string(term->m_left->toString()));
    alias->setAlias(areaTarget ? AliasKind::Namespace : AliasKind::Name, std::move(target), term->m_mapperRange);
    return alias;
}

AstNodePtr TermToAstConverter::visit_ASSIGN(const trust::TermPtr& term, ConvertServices& ctx) {
    // `=` - ТОЛЬКО присваивание и реализация/доопределение объявленного типа. Объявление имён —
    // `:-` (алиас имени) и `::-` (алиас типа); прочие формы дают ЯВНУЮ диагностику, а не тихое
    // переосмысление (no silent fallback).
    // `... = ns::;` (using) - это НЕ присваивание и не алиас: LHS-многоточие оставляем общему
    // пути AssignOp (оператор разбирает семантика, `handleUsingImport`).
    const bool usingForm = term->m_left && term->m_left->getTermID() == trust::TermID::ELLIPSIS;
    if (!usingForm && term->m_right && !term->m_right->m_moduleIndices.empty() && !term->m_right->isCall()) {
        ctx.diag().report(Severity::Error, term->m_mapperRange,
                          "assignment from a module reference is not a declaration; use ':-' for a name alias: 'name :- target;'");
        return nullptr;
    }
    // `Name = <class_def>` - ОПРЕДЕЛЕНИЕ ранее объявленного типа, а не присваивание:
    // RHS-терм CLASS (`%cpp { ... }` / `:Base { ... }`) однозначно отличает форму от `x = expr`.
    if (term->m_right && term->m_right->getTermID() == trust::TermID::CLASS) {
        auto td = Term::Create(trust::TermID::TYPE_DEFINE, "=", term->m_mapperRange);
        td->m_left = term->m_left;
        td->m_right = term->m_right;
        td->m_template = term->m_template;
        td->m_attr = term->m_attr;
        td->m_sequence = term->m_sequence;
        return visit_TYPE_DEFINE(td, ctx);
    }
    // `:Name = <type>` больше НЕ объявляет второе имя типа: для этого есть `::-`.
    if (term->m_left && term->m_left->getTermID() == trust::TermID::TYPE && !term->m_left->m_args.has_value()) {
        ctx.diag().report(Severity::Error, term->m_mapperRange,
                          "a type cannot be assigned; use '::-' to declare a second name for an existing type: ':A ::- T;'");
        return nullptr;
    }
    // `=` - оператор присваивания. Многоимённый LHS (`a, b = ... source;`, assign_items: цепочка
    // m_left) + RHS (`... source` - spread-коллекция, или выражение-кортеж) → деструктуризация-
    // присваивание (DestructureDecl, m_isAssign=true): цели - уже существующие переменные, в них
    // записываются элементы источника. Одиночный `a = expr` - обычный AssignOp (generic-путь).
    const bool isDestructure = term->m_left && term->m_left->m_left && term->m_right;
    if (isDestructure) {
        return std::make_shared<DestructureDecl>(ParserToken::Kind::DestructureDecl, term, &ctx);
    }
    return convertForKind<ParserToken::Kind::AssignOp>(term, ctx);
}

AstNodePtr TermToAstConverter::visit_DICT(const trust::TermPtr& term, ConvertServices& ctx) {
    // Литерал словаря `(1, two=2, name=3,)` (возможно типизированный `(...):Type`) →
    // DictLiteralNode. Элементы нормализуются к ЕДИНОЙ форме Binary(AssignOp). Имя берём
    // из канонической пары (name, term) грамматики `args` (argName вычислен в parser.y).
    // Аннотация типа сохраняется механически (m_type) - её интерпретирует анализатор по
    // типу из реестра (Tuple → kind=Tuple и структурный кортеж; иначе конструкция/каст).
    auto node = std::make_shared<DictLiteralNode>(ParserToken::Kind::DictLiteral, term);
    node->m_type = term && term->m_type ? convertChild(ctx, term->m_type) : nullptr;
    appendDictElementsFromArgs(ctx, term, *node);
    return node;
}

AstNodePtr TermToAstConverter::visit_INDEX(const trust::TermPtr& term, ConvertServices& ctx) {
    // Индексация `a[i]` / `a[i,j]`: все индексы лежат в m_args (TermID::INDEX). Binary несёт
    // base (m_left) и первый индекс (m_right, для совместимости), полный список - m_indices.
    auto node = std::make_shared<Binary>(ParserToken::Kind::ArrayAccess, term, &ctx);
    if (term && term->m_args) {
        node->m_indices.reserve(term->m_args->size());
        for (const auto& [name, argTerm] : *term->m_args) {
            (void)name;
            if (!argTerm) {
                continue;
            }
            if (AstNodePtr idx = convertChild(ctx, argTerm); idx) {
                node->m_indices.push_back(std::move(idx));
            }
        }
    }
    if (!node->m_right && !node->m_indices.empty()) {
        node->m_right = node->m_indices.front();
    }
    return node;
}

AstNodePtr TermToAstConverter::visit_TENSOR(const trust::TermPtr& term, ConvertServices& ctx) {
    // Литерал массива `[1,2:Int8,3,]` (возможно типизированный `[...]:Int32`) → DictLiteralNode
    // с kind=ArrayInit (позиционные элементы; имя-метка у элемента - ""). Аннотация `]:Type`
    // сохраняется механически в m_type - интерпретирует анализатор (тип элемента массива).
    auto node = std::make_shared<DictLiteralNode>(ParserToken::Kind::ArrayInit, term);
    node->m_type = term && term->m_type ? convertChild(ctx, term->m_type) : nullptr;
    appendDictElementsFromArgs(ctx, term, *node);
    return node;
}

AstNodePtr TermToAstConverter::visit_OPERATOR_PTR(const trust::TermPtr& term, ConvertServices& ctx) {
    // `&` в позиции типа: RefMakeExpr; вид smart-ссылки (`&&`/`&*`/`&?`) переносим в поле
    // m_sigilKind. Для простого `&` - RefType::kValue.
    auto node = std::make_shared<RefMakeExpr>(ParserToken::Kind::RefMakeExpr, term, &ctx);
    node->m_sigilKind = refTypeFromTypeSigil(term ? term->getText() : std::string_view{}).value_or(RefType::kValue);
    return node;
}

AstNodePtr TermToAstConverter::visit_RANGE(const trust::TermPtr& term, ConvertServices& ctx) {
    // Литерал диапазона `start..stop[..step]` → RangeExpr. Generic-путь (Sequence-конструктор)
    // строит m_body из детей в m_args (порядок start/stop/step). Дополнительно переносим явные
    // аннотации типа операндов (`start:Type`/`stop:Type`, грамматика `digits_literal type_item`
    // кладёт их в m_type терма-операнда) в RangeExpr::operandTypes - параллельно m_body.
    auto node = std::make_shared<RangeExpr>(ParserToken::Kind::RangeExpr, term, &ctx);
    if (term && term->m_args) {
        node->operandTypes.reserve(term->m_args->size());
        for (const auto& [name, argTerm] : *term->m_args) {
            (void)name;
            node->operandTypes.push_back(argTerm && argTerm->m_type ? convertChild(ctx, argTerm->m_type) : nullptr);
        }
    }
    return node;
}

// Немедленный вызов лямбды `( lambda )(args)` (LAMBDA_CALL): callee - сама лямбда (m_left),
// аргументы вызова - в m_args. Generic-путь CallExpr строит callee=IdentName(term) - не годится.
AstNodePtr TermToAstConverter::visit_LAMBDA_CALL(const trust::TermPtr& term, ConvertServices& ctx) {
    auto call = std::make_shared<CallExpr>(ParserToken::Kind::CallExpr, term);
    if (term && term->m_left) {
        call->m_callee = convertChild(ctx, term->m_left);
    }
    if (term && term->m_args) {
        std::vector<AstNodePtr> args;
        for (const auto& [name, argTerm] : *term->m_args) {
            (void)name;
            if (!argTerm || argTerm->getTermID() == trust::TermID::END) {
                continue;
            }
            if (AstNodePtr an = convertChild(ctx, argTerm)) {
                args.push_back(std::move(an));
            }
        }
        if (!args.empty()) {
            call->m_args = std::move(args);
        }
    }
    return call;
}

AstNodePtr TermToAstConverter::visit_ARGUMENT(const trust::TermPtr& term, ConvertServices& ctx) {
    // ЕДИНЫЙ узел аргумента (name в m_left, тип в m_type, значение в m_right) → ArgNode.
    // Раскладка слотов - в ArgNode-конструкторе (ast_base.cpp).
    return std::make_shared<ArgNode>(ParserToken::Kind::ArgNode, term, &ctx);
}

AstNodePtr TermToAstConverter::visit_TYPE(const trust::TermPtr& term, ConvertServices& ctx) {
    // Нативный шаблон-тип `vector<Int32>` (грамматика `type_class LT template_args GT`): признак и
    // типовые аргументы - в m_template. Строим IdentType с типовыми аргументами (каждый - узел типа:
    // голое `Int32` → IdentName, `:Int32`/`:MyClass<:Int8>` → IdentType). Отличие от кортежных
    // параметров `:Type(...)` (m_args, ветка ниже) - изолированный механизм (см. MEMORY.md).
    if (term->m_template.has_value()) {
        auto tpl = std::make_shared<IdentType>(term, std::nullopt, std::nullopt, &ctx.attrs());
        std::vector<TemplateArg> args;
        args.reserve(term->m_template->size());
        for (const auto& [aname, argTerm] : *term->m_template) {
            (void)aname;
            if (argTerm) {
                if (AstNodePtr a = convertChild(ctx, argTerm)) {
                    args.push_back(TemplateArg{std::move(a), argTerm->m_isPack});
                }
            }
        }
        tpl->setTemplateArgs(std::move(args));
        // `:vector<Int32>(1, 2, 3)` - КОНСТРУКЦИЯ значения (type_call): есть call-аргументы в m_args.
        // Строим DictLiteralNode с шаблонным типом как контейнером (как `:Array(...)`).
        if (term->m_args.has_value()) {
            auto node = std::make_shared<DictLiteralNode>(ParserToken::Kind::DictLiteral, term);
            node->m_type = std::move(tpl);
            node->prefix = true; // `:Type(args)` - префиксная форма (конструкция)
            appendDictElementsFromArgs(ctx, term, *node);
            return node;
        }
        return tpl;
    }
    // `:Type(...)` - префиксная форма: аннотация типа (в позиции типа) или литерал/конструкция
    // (в позиции значения). Терм-слой делает ТОЛЬКО механическую раскладку, класс узла
    // (аннотация | кортеж | каст | конструктор) определяется ПОЗЖЕ анализатором по типу из
    // реестра (см. MEMORY.md). Никаких сравнений имён типов здесь нет.
    // `:Type(...)`: аргументы-ТИПЫ → параметризованная аннотация; аргументы-значения →
    // литерал/конструкция. Размерности `[dims]` (m_type == ARGS) сохраняются в ОБОИХ случаях
    // (`:Tensor[2,2](:Float32)` - тип; `:Tensor[2,2](1,2,3,4)` - конструкция).
    if (term->m_args) {
        // Параметризованная АННОТАЦИЯ типа `Tuple(:Rational, sum:Rational)` (все аргументы -
        // типы, `:Rational` / `name:Type`) → IdentType с параметрами (позиционные → IdentType,
        // именованные → ArgNode(name, type)), чтобы тип не терялся. Это решение чисто
        // механическое (аргументы - ТИПЫ), не зависит от имени типа.
        std::optional<std::vector<AstNodePtr>> dims;
        if (term->m_type && term->m_type->getTermID() == trust::TermID::ARGS && term->m_type->size() > 0) {
            dims.emplace();
            for (int64_t i = 0; i < term->m_type->size(); ++i) {
                if (AstNodePtr d = convertChild(ctx, term->m_type->at(i).second); d) {
                    dims->push_back(std::move(d));
                }
            }
        }
        // Аннотация с размерностями `Type[dims](args)` трактует голые имена в args
        // как типовые параметры (`Matrix[2,3](Float)`); без dims голые имена - value-форма.
        bool dimsArgsAreTypes = dims.has_value() && !dims->empty();
        if (dimsArgsAreTypes) {
            for (const auto& [aname, argTerm] : *term->m_args) {
                (void)aname;
                if (!argTerm) {
                    continue;
                }
                const bool ok = argIsTypeForm(argTerm) || argTerm->getTermID() == trust::TermID::NAME;
                if (!ok) {
                    dimsArgsAreTypes = false;
                    break;
                }
            }
        }
        if (isAllTupleTypeArgs(term) || dimsArgsAreTypes) {
            std::optional<std::vector<AstNodePtr>> params;
            params.emplace();
            for (const auto& [aname, argTerm] : *term->m_args) {
                if (!argTerm) {
                    continue;
                }
                if (argTerm->getTermID() == trust::TermID::TYPE) {
                    params->push_back(convertChild(ctx, argTerm)); // позиционный :Rational → IdentType
                } else if (argTerm->m_type) {
                    // именованный sum:Rational → ArgNode("sum", :Rational). Имя из самого терма
                    // (аргумент - NAME-терм с m_type; в ArgsList имя пустое, т.к. это не ARGUMENT).
                    params->push_back(std::make_shared<ArgNode>(std::string(argTerm->getText()), convertChild(ctx, argTerm->m_type), nullptr, &ctx.attrs()));
                } else if (argTerm->getTermID() == trust::TermID::NAME) {
                    params->push_back(convertChild(ctx, argTerm)); // голое имя типа `Matrix[..](Float)`
                } else if (argTerm->getTermID() == trust::TermID::ARGUMENT) {
                    // ARGUMENT-обёртка `sum:Rational`: имя в m_left, тип - в m_right/m_type.
                    const std::string pname = argTerm->m_left ? std::string(argTerm->m_left->getText()) : std::string();
                    const trust::TermPtr typeTerm = argTerm->m_type ? argTerm->m_type : argTerm->m_right;
                    if (typeTerm) {
                        params->push_back(std::make_shared<ArgNode>(pname, convertChild(ctx, typeTerm), nullptr, &ctx.attrs()));
                    }
                }
            }
            return std::make_shared<IdentType>(term, std::move(dims), std::move(params), &ctx.attrs());
        }
        // `:Type(args)` с НЕ-типовыми аргументами - литерал/конструкция/вызов (значения).
        // ЕДИНЫЙ узел DictLiteralNode с аннотацией типа (m_type = контейнер). Анализатор по
        // СЛОТУ (тип/значение) и типу из реестра решает: в value-слоте Tuple → kind=Tuple,
        // иначе - каст/конструктор (кодогенерация); в type-слоте аннотация - resolveTypeRef.
        // Константность `^` (`:Array^`) → attr::ReadOnly на контейнере (→ std::array).
        // Хвостовая элементная аннотация `:Type(...):Elem` (term->m_type) → arrayElementAnnotation.
        {
            auto node = std::make_shared<DictLiteralNode>(ParserToken::Kind::DictLiteral, term);
            // Контейнер типа: IdentType с СОХРАНЁННЫМИ размерностями `[dims]` (m_type терма -
            // ARGS-список dims; для value-конструкции `:Tensor[2,2](1,2,3,4)` его нельзя терять).
            std::optional<std::vector<AstNodePtr>> dims;
            if (term->m_type && term->m_type->getTermID() == trust::TermID::ARGS && term->m_type->size() > 0) {
                dims.emplace();
                for (int64_t i = 0; i < term->m_type->size(); ++i) {
                    if (AstNodePtr d = convertChild(ctx, term->m_type->at(i).second); d) {
                        dims->push_back(std::move(d));
                    }
                }
            }
            node->m_type = std::make_shared<IdentType>(term, std::move(dims), std::nullopt, &ctx.attrs());
            node->prefix = true; // `:Type(args)` - префиксная форма (type-call/конструкция)
            // Хвостовая элементная аннотация `:Type(...):Elem` (term->m_type - TYPE-терм).
            if (term->m_type && term->m_type->getTermID() != trust::TermID::ARGS) {
                node->arrayElementAnnotation = convertChild(ctx, term->m_type);
            }
            appendDictElementsFromArgs(ctx, term, *node);
            return node;
        }
    }
    return TermVisitorDefault::visit_TYPE(term, ctx);
}

AstNodePtr TermToAstConverter::visit_STRWIDE(const trust::TermPtr& term, ConvertServices& ctx) {
    if (term->m_args) {
        return buildStringFormatCall(ParserToken::Kind::StrWide, term, ctx);
    }
    return TermVisitorDefault::visit_STRWIDE(term, ctx);
}

AstNodePtr TermToAstConverter::visit_STRCHAR(const trust::TermPtr& term, ConvertServices& ctx) {
    if (term->m_args) {
        return buildStringFormatCall(ParserToken::Kind::StrChar, term, ctx);
    }
    return TermVisitorDefault::visit_STRCHAR(term, ctx);
}

} // namespace trust
