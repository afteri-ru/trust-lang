// term_to_ast.cpp - TermID-visitor конвертации Term -> AstNode.
//
// ЕДИНЫЙ источник TermID->Kind - X-макрос TERMS в syntax/term_types.h
// (записи _(NAME, Kind)). Записи _(NAME) без Kind в интерфейс TermVisitor НЕ попадают:
// если такой TermID реально доходит до конвертации (convert) - это ошибка логики
// (узел без Kind не должен строиться), поэтому dispatchTerm default = FAULT.
// TermID::END обрабатывается в convert (возврат nullptr) и dispatchTerm (FAULT).
//
// «Типовые» visit-методы генерируются из x-macro автоматически: каждый visit_<NAME> делегирует
// в convertForKind<Kind>, который делает ТОЛЬКО класс-селекцию (Ident→CallExpr|IdentName,
// control-flow→+expandControlFlowRange) либо generic-путь `make_shared<node_type_for_kind_t<Kind>>(K, term, &ctx)`
// (класс из PARSER_TOKEN_KINDS). Раскладку детей строят сами терм-конструкторы узлов.
// Ручной копипаст однотипных visit-методов отсутствует.

#include "ast/term_to_ast.hpp"

#include "ast/ast_nodes.hpp"
#include "ast/attr_builtin.hpp"
#include "ast/attr_parser.hpp"
#include "ast/check_area.hpp"
#include "ast/ident_name.hpp"
#include "ast/token_base.hpp"
#include "ast/token_type.hpp"
#include "syntax/term.h"
#include "syntax/term_types.h"
#include "types/typekind.hpp"
#include "utils/error.hpp"

#include <memory>
#include <optional>
#include <type_traits>
#include <vector>

namespace trust {

// -- Диспетчер TermID-visitor (из TERMS) --

AstNodePtr dispatchTerm(const trust::TermPtr& term, TermVisitor& visitor, Context& ctx) {
    switch (term->getTermID()) {
#define TRUST_TD_NOCASE(name)
#define TRUST_TD_GENCASE(name, kind) \
    case trust::TermID::name:        \
        return visitor.visit_##name(term, ctx);
#define TRUST_TD_GENCASE3(name, kind, T) \
    case trust::TermID::name:            \
        return visitor.visit_##name(term, ctx);
#define TRUST_TD_GET(_1, _2, _3, NAME, ...) NAME
#define TRUST_TD_DISPATCH(...) TRUST_TD_GET(__VA_ARGS__, TRUST_TD_GENCASE3, TRUST_TD_GENCASE, TRUST_TD_NOCASE)(__VA_ARGS__)
#define TRUST_TD_CASE(...) TRUST_TD_DISPATCH(__VA_ARGS__)
        TERMS(TRUST_TD_CASE)
#undef TRUST_TD_CASE
#undef TRUST_TD_DISPATCH
#undef TRUST_TD_GET
#undef TRUST_TD_GENCASE3
#undef TRUST_TD_GENCASE
#undef TRUST_TD_NOCASE
    case trust::TermID::END:
        FAULT("dispatchTerm: TermID::END");
    default:
        // TermID без Kind (AWAIT/YIELD/WHEN_ALL/WHEN_ANY/FILLING и т.п.): синтаксис
        // распознан лексером/грамматикой, но конвертация в AstNode не реализована.
        // Это НЕ внутренняя ошибка - пользовательская конструкция, требующая диагностики
        // с позицией в исходном файле. Возвращаем nullptr: convert/convertSeq его
        // безопасно пропускают, а конвейер (pipeline) не запускает semantic/transpile
        // при наличии ошибок (см. runPipeline: проверка errorCount после ParseAST).
        ctx.diag().report(Severity::Error, term->m_mapperRange, "Конструкция '{}' не реализована", trust::toString(term->getTermID()));
        return nullptr;
    }
}

// -- Вспомогательные функции (файловая область) --

static void convertAttrsToNode(const trust::TermPtr& term, AstNodePtr& node, Context& ctx) {
    if (!node) {
        return;
    }
    if (auto* attrNode = node->as_attr()) {
        // Для объявлений (VarDecl/FuncDecl) признак имени ('^' и @[ ... @]) живёт на терме-имени
        // (m_left оператора `:=`/`::=`), а не на самом операторном терме - берём атрибуты оттуда.
        // Для остальных узлов источник атрибутов - сам терм.
        const trust::TermPtr source = (node->kind() == ParserToken::Kind::VarDecl || node->kind() == ParserToken::Kind::FuncDecl) ? term->m_left : term;
        if (source) {
            // Пользовательские атрибуты @[ ... @], собранные парсером в source->m_attr.
            for (const auto& attrTerm : source->m_attr) {
                if (!attrTerm) {
                    continue;
                }
                std::vector<std::string_view> params;
                if (attrTerm->m_args) {
                    for (const auto& [name, argTerm] : *attrTerm->m_args) {
                        (void)name;
                        if (argTerm) {
                            params.push_back(argTerm->getText());
                        }
                    }
                }
                std::optional<std::vector<std::string_view>> optParams;
                if (!params.empty()) {
                    optParams = std::move(params); // params переносится → далее читаем optParams
                }
                if (auto id = parse_attr(ctx, attrTerm->m_mapperRange, attrTerm->getText(), optParams)) {
                    attrNode->add_attr(*id);
                    // Аргументы атрибута (напр. @[link("m")] → ["m"]). Сохраняем полный
                    // список (params уже перемещён в optParams), конвертируя в std::string,
                    // чтобы потребитель (транспилятор) прочитал их через attr_args(*id).
                    if (optParams && !optParams->empty()) {
                        std::vector<std::string> args;
                        args.reserve(optParams->size());
                        for (const auto& p : *optParams) {
                            args.emplace_back(p);
                        }
                        attrNode->set_attr_args(*id, std::move(args));
                    }
                }
            }
            // Иммутабельность ('^' в имени) → attr::ReadOnly с ручным признаком.
            // Единый хелпер (applyReadonlyFromCaret): та же логика, что в IdentName.
            // '^' при этом не срезается здесь - это делает normalizeTermText конструкторов.
            applyReadonlyFromCaret(*attrNode, source->getText(), &ctx.attrs());
        }
    }
}

/// Структурный предикат «есть конвертируемые дети» для Ident-терма (выбор CallExpr vs IdentName).
/// Предикат `m_args || m_sequence || m_left || m_right`: наличие m_args (даже пустого - `f()`) -
/// это вызов → CallExpr; m_sequence/m_left/m_right - составные дети (не-null, не-END).
/// (Для Ident m_left всегда обходится - в отличие от INT_* в convertSeq.)
/// Trust-контракты (TRUST_CONTRACT) в m_sequence НЕ считаются конвертируемыми детьми -
/// иначе имя с контрактами после себя (`x @{ ... @} := ...`) стало бы CallExpr.
static bool hasConvertibleChildren(const trust::TermPtr& term) {
    if (term->isCall()) {
        return true; // f() / f(a) - вызов, даже без аргументов
    }
    for (const auto& child : term->m_sequence) {
        if (child && child->getTermID() != trust::TermID::TRUST_CONTRACT && child->getTermID() != trust::TermID::END) {
            return true;
        }
    }
    if (term->m_left && term->m_left->getTermID() != trust::TermID::END) {
        return true;
    }
    if (term->m_right && term->m_right->getTermID() != trust::TermID::END) {
        return true;
    }
    return false;
}

// -- TermVisitorDefault: генерируемые «типовые» visit-методы --
// Каждый visit_<NAME> делегирует в convertForKind<Kind>: класс-селекция (Ident, control-flow)
// либо generic по node_type_for_kind_t<Kind>. Раскладка детей - в терм-конструкторах узлов.

template <ParserToken::Kind K>
AstNodePtr TermVisitorDefault::convertForKind(const trust::TermPtr& term, Context& ctx) {
    if constexpr (K == ParserToken::Kind::NotApplicable) {
        // TermID с Kind=NotApplicable (например COMMA_LEXEME) НИКОГДА не должен доходить
        // до конвертации в AstNode. Если он сюда попал - это ошибка логики (в отличие от
        // Unimplemented, где конструкция лишь не реализована). Диагностируем как фатальную.
        ctx.diag().report(Severity::Fatal, term->m_mapperRange, "Конструкция '{}' неприменима (не должна появляться в AST)",
                          trust::toString(term->getTermID()));
        return nullptr;
    } else if constexpr (K == ParserToken::Kind::Unimplemented) {
        // TermID с Kind=Unimplemented (AWAIT/YIELD/WHEN_ALL/WHEN_ANY/FILLING/TYPENAME/PARENT и т.п.):
        // синтаксис распознан лексером/грамматикой, но конвертация в AstNode не реализована.
        // Это НЕ внутренняя ошибка - пользовательская конструкция, требующая диагностики с позицией
        // в исходном файле. Узел не строится; convert/convertSeq его безопасно пропускают, а конвейер
        // (pipeline) не запускает semantic/transpile при наличии ошибок (runPipeline: errorCount после ParseAST).
        ctx.diag().report(Severity::Error, term->m_mapperRange, "Конструкция '{}' не реализована", trust::toString(term->getTermID()));
        return nullptr;
    } else if constexpr (K == ParserToken::Kind::Ident) {
        // Класс-селекция Ident→CallExpr vs IdentName: структурный предикат
        // «есть конвертируемые дети». Раскладка детей (callee/args) - в CallExpr-конструкторе.
        if (hasConvertibleChildren(term)) {
            return std::make_shared<CallExpr>(ParserToken::Kind::CallExpr, term, &ctx);
        }
        return std::make_shared<IdentName>(ParserToken::Kind::Ident, term, &ctx);
    } else if constexpr (K == ParserToken::Kind::IfStmt || K == ParserToken::Kind::WhileStmt || K == ParserToken::Kind::DoWhileStmt ||
                         K == ParserToken::Kind::MatchingStmt || K == ParserToken::Kind::WithStmt) {
        // Control-flow: узел строит детей; полный охват statement'а (range) вычисляется на лету
        // в override ControlFlowStmt::range()/MatchStmt::range()/WithStmt::range() (Term не мутируется).
        return std::make_shared<node_type_for_kind_t<K>>(K, term, &ctx);
    } else {
        using NT = node_type_for_kind_t<K>;
        if constexpr (std::is_same_v<NT, AstNodeBase>) {
            // Kind=END (TermID::NONE) - не конвертируется.
            FAULT("convertForKind: TermID с Kind=END достиг конвертации");
            return nullptr;
        } else {
            // Охват операторного терма (Binary) вычисляется на лету в Binary::range() - Term не мутируется.
            auto node = std::make_shared<NT>(K, term, &ctx);
            // Постфиксная аннотация типа литерала `literal :Type` (грамматика
            // `digits_literal type_item` кладёт тип в m_type терма) переносится в
            // Literal::typeAnnotation - семантика задаёт литералу аннотированный тип
            // (статическая типизация литералов: `0 :Bool`, `5 :Rational`, `5 :BigInteger`).
            if constexpr (std::is_same_v<NT, Literal>) {
                if (term->m_type) {
                    node->typeAnnotation = convertChild(ctx, term->m_type);
                }
            }
            return node;
        }
    }
}

#define TRUST_TVD_DEF_NOCASE(name)
#define TRUST_TVD_DEF_GENCASE(name, kind)                                                   \
    AstNodePtr TermVisitorDefault::visit_##name(const trust::TermPtr& term, Context& ctx) { \
        return this->convertForKind<ParserToken::Kind::kind>(term, ctx);                    \
    }
#define TRUST_TVD_DEF_GENCASE3(name, kind, T)                                               \
    AstNodePtr TermVisitorDefault::visit_##name(const trust::TermPtr& term, Context& ctx) { \
        return this->convertForKind<ParserToken::Kind::kind>(term, ctx);                    \
    }
#define TRUST_TVD_DEF_GET(_1, _2, _3, NAME, ...) NAME
#define TRUST_TVD_DEF_DISPATCH(...) TRUST_TVD_DEF_GET(__VA_ARGS__, TRUST_TVD_DEF_GENCASE3, TRUST_TVD_DEF_GENCASE, TRUST_TVD_DEF_NOCASE)(__VA_ARGS__)
#define TRUST_TVD_DEF_CASE(...) TRUST_TVD_DEF_DISPATCH(__VA_ARGS__)
TERMS(TRUST_TVD_DEF_CASE)
#undef TRUST_TVD_DEF_CASE
#undef TRUST_TVD_DEF_DISPATCH
#undef TRUST_TVD_DEF_GET
#undef TRUST_TVD_DEF_GENCASE3
#undef TRUST_TVD_DEF_GENCASE
#undef TRUST_TVD_DEF_NOCASE

// -- TermToAstConverter --

AstNodePtr TermToAstConverter::convert(const trust::TermPtr& term) {
    if (!term || term->getTermID() == trust::TermID::END) {
        return nullptr;
    }

    // Определение макроса (MACRO_SEQ с телом в m_right) или его удаление (MACRO_DEL) -
    // это compile-time конструкция, в AST не попадает (макросы уже раскрыты/зарегистрированы).
    if (term->isMacro()) {
        return nullptr;
    }

    std::string text = term->getText();

    // Встроенный контекст-макрос-маркер `@__CHECK_AREA__(...)`: терм приходит единым
    // TermID::MACRO_CONTEXT-термом с текстом "@__CHECK_AREA__" и аргументами в дочерних
    // NAME-термах [area, behavior?, attr...]. Строим лист-узел CheckAreaStmt (поля-ограничения
    // заполняем здесь, т.к. терм-конструктор - лист и ctx игнорирует). Семантика валидирует
    // по текущей области (единый скоуп-стек) и УДАЛЯЕТ маркер - до транспилятора он не доходит.
    if (term->getTermID() == trust::TermID::MACRO_CONTEXT && text == "@__CHECK_AREA__") {
        auto marker = std::make_shared<CheckAreaStmt>(ParserToken::Kind::CheckAreaStmt, term, &m_ctx);
        std::vector<std::string> argTexts;
        for (const auto& c : term->m_sequence) {
            if (c) {
                argTexts.emplace_back(c->getText());
            }
        }
        if (!argTexts.empty()) {
            marker->area = areaKindFromString(argTexts[0]);
        }
        if (argTexts.size() >= 2) {
            const std::string& b = argTexts[1];
            marker->behavior = (b == "default")  ? std::optional<Severity>(std::nullopt)
                             : (b == "ignore")   ? std::optional<Severity>(Severity::Ignore)
                             : (b == "warning")  ? std::optional<Severity>(Severity::Warning)
                             : (b == "error")    ? std::optional<Severity>(Severity::Error)
                                                 : std::nullopt;
        }
        // Требуемые атрибуты текущей области (AttrId) - лежат в attrs() маркера (семантика читает
        // их как required-список). Резолв через AttrPool (layering: ast не зависит от semantic).
        for (size_t i = 2; i < argTexts.size(); ++i) {
            const auto id = m_ctx.attrs().lookup(argTexts[i]);
            if (id.has_value()) {
                // manual=false: семантика (analyzeCheckAreaStmt) читает эти attr как требования
                // области - они обрабатываются, а не «unhandled» (иначе reportUnhandledAttributes).
                marker->add_attr(*id, /*manual=*/false);
            } else {
                m_ctx.diag().report(Severity::Error, term->m_mapperRange,
                                    "@__CHECK_AREA__: unknown attribute '{}'", argTexts[i]);
            }
        }
        return marker;
    }

    AstNodePtr node = dispatchTerm(term, *this, m_ctx);

    // Документирующий комментарий, привязанный грамматикой к терму-идентификатору
    // (объявлению), переносим в узел (term->m_docs - см. include/syntax/term.h). Для
    // не-объявлений m_docs пуст - док остаётся отдельным sibling-узлом Document.
    if (node && !term->m_docs.empty() && isDeclKindForDocs(node->kind())) {
        std::string d;
        for (const auto& doc : term->m_docs) {
            if (!d.empty()) {
                d += "\n";
            }
            d += std::string(doc->getText());
        }
        node->documentation = std::move(d);
    }

    // Document - чистый leaf (полный текст комментария): никакой '^'-нормализации,
    // атрибутов или диагностики иммутабельности (комментарий может оканчиваться на '^').
    if (node && node->kind() == ParserToken::Kind::Document) {
        return node;
    }

    // Нормализация ':' (TypeName) и '^' (иммутабельность) выполняется терм-конструкторами
    // узлов (normalizeTermText). Здесь остаётся только диагностика: '^' неприменим к меткам
    // блоков/областям имён - ошибка синтеза (конструктором текст при этом не срезается).
    // Исключение: тип-вызов `:Type^(args)` (DictLiteralNode с prefix=true) - '^' это const
    // контейнера (attr::ReadOnly на m_type, ставится в visit_TYPE), валидный квалификатор типа.
    if (!text.empty() && text.back() == '^' && node && !canHaveImmutableQualifier(node->kind())) {
        const bool typeCallPrefix = node->kind() == ParserToken::Kind::DictLiteral && static_cast<const DictLiteralNode*>(node.get())->prefix;
        if (!typeCallPrefix) {
            m_ctx.diag().report(Severity::Error, term->m_mapperRange, "Immutable qualifier '^' is not applicable in block labels or namespaces");
        }
    }

    // Trust-конструкции (pre/post/assert) после имени объявления: привязываем их к узлу
    // идентификатора. Грамматика кладёт конд-термы в m_sequence терма-имени (m_left оператора
    // :=/::=/[]=/=); hasConvertibleChildren пропускает TRUST_*, чтобы имя осталось IdentName.
    // Допустимы только при ОПРЕДЕЛЕНИИ (:=/::=): FuncDecl/VarDecl/TypeDecl получают их в
    // node->m_trust, который НЕ входит в children()/collectChildren - анализатор и транспилятор
    // полностью игнорируют. Перед `[]=` (append) - недопустимо -> ошибка.
    if (node && term->m_left && !term->m_left->m_sequence.empty()) {
        const auto tID = term->getTermID();
        const bool isDefOp = (tID == trust::TermID::CREATE_NAME || tID == trust::TermID::CREATE_TYPE || tID == trust::TermID::ASSIGN);
        const bool isAppend = (tID == trust::TermID::APPEND);
        for (const auto& condTerm : term->m_left->m_sequence) {
            if (!condTerm || condTerm->getTermID() != trust::TermID::TRUST_CONTRACT) {
                continue;
            }
            if (isDefOp) {
                AstNodePtr cn = convert(condTerm);
                if (cn) {
                    node->m_trust.push_back(std::move(cn));
                }
            } else if (isAppend) {
                // `x @{ ... @} []= v` - утверждение после append-имени недопустимо (не определение).
                m_ctx.diag().report(Severity::Error, condTerm->m_mapperRange, "trust-condition is only allowed at a declaration (':='/':='), not after '[]='");
            }
        }
    }

    convertAttrsToNode(term, node, m_ctx);
    return node;
}

void TermToAstConverter::convertSeq(const trust::TermPtr& term, std::vector<AstNodePtr>& out) {
    if (!term || term->getTermID() == trust::TermID::END) {
        return;
    }

    // -- m_sequence (children stored in a vector, e.g. members of a block) --
    // SEQUENCE-терм - синтаксический контейнер операторов (`sequence` non-terminal) и doc-bundle,
    // НЕ пользовательский скоуп: рекурсивно разворачиваем, иначе вокруг тела блока/функции/дока
    // появляется лишний ScopeBlock-слой. Пользовательские скоупы - только BLOCK-термы.
    for (const auto& child : term->m_sequence) {
        if (child) {
            flattenInto(child, out);
        }
    }

    // -- m_args (named arguments, e.g. function call arguments) --
    if (term->m_args) {
        for (const auto& [name, argTerm] : *term->m_args) {
            (void)name; // Имя аргумента уже сохранено в ARGUMENT-обёртке (m_left)
            if (!argTerm || argTerm->getTermID() == trust::TermID::END) {
                continue;
            }
            AstNodePtr an = convert(argTerm);
            if (an) {
                out.push_back(std::move(an));
            }
        }
    }

    // -- m_left --
    // Для jump-термов (INT_PLUS/INT_MINUS/INT_REPEAT) m_left всегда null: значение живёт
    // в m_right, label/namespace - в m_text (переносится в JumpStmt::m_label конструктором).
    // Поэтому спец-исключение не требуется - jump-терм обходится единообразно.
    if (term->m_left) {
        AstNodePtr ln = convert(term->m_left);
        if (ln) {
            out.push_back(std::move(ln));
        }
    }

    // -- m_right (chained list) --
    if (term->m_right) {
        trust::TermPtr cur = term->m_right;
        while (cur && cur->getTermID() != trust::TermID::END) {
            AstNodePtr rn = convert(cur);
            if (rn) {
                out.push_back(std::move(rn));
            }
            cur = cur->m_right;
        }
    }
}

void TermToAstConverter::flattenInto(const trust::TermPtr& term, std::vector<AstNodePtr>& out) {
    if (!term) {
        return;
    }
    // SEQUENCE-терм - синтаксический контейнер (последовательность операторов, doc-bundle):
    // разворачиваем рекурсивно в детей. BLOCK-термы (пользовательские скоупы) и прочие
    // конвертируются как есть (ScopeBlock для BLOCK).
    if (term->getTermID() == trust::TermID::SEQUENCE) {
        for (const auto& child : term->m_sequence) {
            if (child) {
                flattenInto(child, out);
            }
        }
    } else {
        if (AstNodePtr cn = convert(term); cn) {
            out.push_back(std::move(cn));
        }
    }
}

std::vector<AstNodePtr> TermToAstConverter::termToAst(const trust::TermPtr& term, Context& ctx) {
    std::vector<AstNodePtr> result;
    if (!term) {
        return result;
    }
    // Единый вход конвертации корня: как и тело модуля, разворачиваем SEQUENCE-контейнер
    // (иначе верхнеуровневые операторы оборачиваются в ScopeBlock-обёртку, которую
    // транспайлер ошибочно принимает за безымянный блок кода вне функции).
    convertModuleBody(ctx, term, result);
    return result;
}

// -- Спец-термы --

AstNodePtr TermToAstConverter::visit_MODULE(const trust::TermPtr& term, Context& ctx) {
    if (!term->isCall()) {
        return std::make_shared<IdentName>(ParserToken::Kind::Ident, term, &ctx);
    }
    if (term->m_sequence.empty()) {
        ctx.diag().report(Severity::Error, term->m_mapperRange, "Module '{}' is not loaded", term->getText());
        return std::make_shared<AstNodeAttr>(ParserToken::Kind::Ident, term);
    }
    auto mn = std::make_shared<ModuleNode>(ParserToken::Kind::ModuleDecl, term, &ctx);
    // Аргументы оператора загрузки `\module(mod, masks)` - список масок фильтра
    // экспорт-интерфейса (glob `*`/`?`, через запятую = OR). Без аргументов - все экспорты.
    std::string masks;
    if (term->m_args) {
        for (const auto& [name, arg] : *term->m_args) {
            if (arg) {
                if (!masks.empty()) {
                    masks += ',';
                }
                masks += std::string(arg->getText());
            }
        }
    }
    mn->setImportMasks(std::move(masks));
    return mn;
}

// True, если RHS-терм `::=` - forward-объявление (нативного) класса: CLASS-терм, чей текст -
// ИМЯ (native `%std::pair` или голое имя), а НЕ type_class (`:Base`, начинается с ':')
// пользовательского класса (class_type_def). Нативный класс: `%`-имя → C++-имя без '%'.
static bool isNativeClassFwdTerm(const trust::TermPtr& rhs) {
    return rhs && rhs->getTermID() == trust::TermID::CLASS && !rhs->getText().empty() &&
           rhs->getText().front() != ':';
}

AstNodePtr TermToAstConverter::visit_CREATE_TYPE(const trust::TermPtr& term, Context& ctx) {
    // `::=` тип-определение. Forward-объявление (нативного) класса:
    // `Pair ::= %std::pair<T1,T2> { ... };` / `String ::= %std::string { };` - RHS CLASS-терм с
    // m_left = имя (native/голое) → right = ClassDecl (trust-имя слева, C++-имя из RHS,
    // типовые параметры, члены-интерфейс). Иначе - generic (алиас/enum/variant/польз. класс).
    if (isNativeClassFwdTerm(term->m_right)) {
        auto ncd = std::make_shared<ClassDecl>(ParserToken::Kind::ClassDecl, term, &ctx);
        // Атрибуты (@[include]) нативной декларации класса - на терме-имени `::=` (m_left),
        // куда template_prefix/name_attr положили их; конвертируем в атрибуты узла.
        if (term->m_left) {
            AstNodePtr ncdBase = ncd;
            convertAttrsToNode(term->m_left, ncdBase, ctx);
        }
        // Тип-определение: left = trust-имя (Ident), right = ClassDecl.
        auto b = std::make_shared<Binary>(ParserToken::Kind::TypeDecl, term, &ctx);
        b->m_right = ncd;
        return b;
    }
    return convertForKind<ParserToken::Kind::TypeDecl>(term, ctx);
}

AstNodePtr TermToAstConverter::visit_CREATE_NAME(const trust::TermPtr& term, Context& ctx) {
    // CREATE_NAME (`:=`) - оператор объявления функции И переменной (единый синтаксический узел).
    // Класс-селекция по форме m_left: сигнатура функции (m_left->isCall()) → FuncDecl;
    // иначе переменная → VarDecl. Раскладка детей - в FuncDecl/VarDecl конструкторах.
    // Для переменной охват [имя, expr] вычисляется в VarDecl::range(); диапазон функции
    // ([имя, оператор], без тела) - в FuncDecl::range() (признак функции).
    if (term->m_left && term->m_left->isCall()) {
        auto fd = std::make_shared<FuncDecl>(ParserToken::Kind::FuncDecl, term, &ctx);
        // Объявление нативного шаблона-ТИПА `<T> %std::vector() := ...;` (грамматика
        // `template_prefix assign_seq` кладёт типовые параметры в term->m_template). Это НЕ функция,
        // а тип-конструктор: помечаем и переносим типовые параметры как ArgNode-узлы (B1 - типовые
        // без ограничений; m_type/дефолт - B2/B3). C++-имя шаблона - из нативного имени `%std::vector`.
        if (term->m_template.has_value()) {
            fd->m_isNativeTemplateCtor = true;
            std::vector<AstNodePtr> params;
            params.reserve(term->m_template->size());
            for (const auto& [pname, pterm] : *term->m_template) {
                (void)pname;
                if (pterm) {
                    params.push_back(std::make_shared<ArgNode>(std::string(pterm->getText())));
                }
            }
            fd->m_templateParams = std::move(params);
            // C++-имя шаблона - из нативного имени (`%std::vector` → "std::vector").
            if (term->m_left) {
                std::string_view t = term->m_left->getText();
                if (t.size() > 1 && t[0] == '%') {
                    t.remove_prefix(1);
                }
                fd->m_nativeName = std::string(t);
            }
        }
        return fd;
    }
    // Префиксный сигл ссылочного типа `&& x : Int32 := 5` / `&& x := 5` / `&* u := 10` / `&? w := & x`:
    // assign_item = `ptr lval` (`&`/`&?` → OPERATOR_PTR) или `take rval_name` (`*` → TAKE), где левый
    // терм `:=` - ref-оператор, обёртывающий lval-имя (`x : Int32` в m_right; тип - в m_right->m_type,
    // может отсутствовать - авто-вывод pointee из инициализатора). Разворачиваем: имя берём из m_right
    // ref-оператора, а вид ссылки из сигла превращаем в атрибут @[reftype("...")] на объявлении -
    // дальше работают стандартные applyRefAttrs (семантика) и reftype-ветка DeclEmitter (кодоген).
    // Вид/имя атрибута - из единого источника refTypeFromTypeSigil/refTypeName (types/typekind.hpp).
    // В visit_ASSIGN (`*x = 7`) - это разыменование-мутация, НЕ сигл (обрабатывается отдельно).
    if (term->m_left && (term->m_left->getTermID() == trust::TermID::OPERATOR_PTR || term->m_left->getTermID() == trust::TermID::TAKE) &&
        term->m_left->m_right) {
        // Вид из сигла (refTypeFromTypeSigil). Константность НЕ разбирается здесь: нативные
        // const-варианты удалены, константность - атрибут @[readonly@] (`^` на имени), применяется
        // стандартно (convertAttrsToNode / applyRefAttrs).
        const auto kind = refTypeFromTypeSigil(term->m_left->getText());
        if (kind) {
            const TermPtr nameTerm = term->m_left->m_right;
            term->m_left = nameTerm; // `:=`: слева - имя (с типом, если был), справа - инициализатор
            auto vd = std::make_shared<VarDecl>(ParserToken::Kind::VarDecl, term, &ctx);
            const AttrPool& ap = ctx.attrs();
            if (const auto rid = ap.lookup(attr::Reftype); rid.has_value()) {
                vd->add_attr(*rid);
                vd->set_attr_args(*rid, {std::string(refTypeName(*kind))});
            }
            return vd;
        }
    }
    // Деструктуризация `a, b, ... := source;`: многоимённый LHS (цепочка m_left->m_left) + RHS
    // (`... source` - spread-коллекция, или выражение-кортеж). `x := ...;` (forward, одно имя) - НЕ.
    const bool isDestructure = term->m_left && term->m_left->m_left && term->m_right;
    if (isDestructure) {
        return std::make_shared<DestructureDecl>(ParserToken::Kind::DestructureDecl, term, &ctx);
    }
    // `x := ... source;` - spread с ОДНОЙ целью: неоднозначно (вся коллекция или первый элемент?)
    // и не поддерживается (точная привязка/rest требует маркер `rest...` или список имён). Guard.
    if (term->m_right && term->m_right->getTermID() == trust::TermID::ELLIPSIS && term->m_right->m_right) {
        ctx.diag().report(Severity::Error, term->m_right->m_mapperRange,
                          "spread '...' requires multiple destructuring targets "
                          "(e.g. 'a, b := ... source;'); single-target 'x := ... source;' is not supported");
    }
    return std::make_shared<VarDecl>(ParserToken::Kind::VarDecl, term, &ctx);
}

AstNodePtr TermToAstConverter::visit_OPERATOR_PTR(const trust::TermPtr& term, Context& ctx) {
    const std::string_view t = term->getText();
    // Нативные (сырые) C++ операторы: текст с ведущим '%' → отдельный узел NativeRefMakeExpr.
    // `%&` - ЕДИНСТВЕННЫЙ оператор нативной ссылки (address-of; контекст T&/T* задаёт левый
    // оператор создания/присваивания). `%*` в выражении - ошибка (указатель только тип/декларация).
    if (t.empty() || t[0] != '%') {
        return TermVisitorDefault::visit_OPERATOR_PTR(term, ctx); // умные & / && / &? → RefMakeExpr
    }
    // Позиция ТИПА (`%& Int32` / `%* Int32` в аннотации `name : %& Int32`): оператор обёртывает тип →
    // делегируем в RefMakeExpr (resolveType применит refTypeFromTypeSigil → kRef/kPtr).
    if (term->m_right && term->m_right->getTermID() == trust::TermID::TYPE) {
        return TermVisitorDefault::visit_OPERATOR_PTR(term, ctx);
    }
    if (t == "%*") {
        ctx.diag().report(Severity::Error, term->m_mapperRange,
                          "native pointer operator '%*' is only valid as a type/declaration marker; dereference with '*'");
        return nullptr;
    }
    auto node = std::make_shared<NativeRefMakeExpr>(ParserToken::Kind::NativeRefMakeExpr, term, &ctx);
    return node;
}

AstNodePtr TermToAstConverter::visit_TAKE(const trust::TermPtr& term, Context& ctx) {
    // Разименование `*`/`*^` - ОБЩИЙ оператор для всех видов ссылок (умных и нативных):
    // всегда RefTakeExpr; для нативных ссылок -Wnative-ref диагностику ставит семантика.
    return TermVisitorDefault::visit_TAKE(term, ctx);
}

AstNodePtr TermToAstConverter::visit_SWAP(const trust::TermPtr& term, Context& ctx) {
    // `x :=: y` - swap двух ссылок/переменных. Единый узел Binary(AssignOp) с оператором ":=":;
    // m_left/m_right строит терм-конструктор Binary из term->m_left/m_right (AppendLeft/Right в
    // грамматике assign_seq SWAP). text() узла = ":=:" (utils::isSwapOp в семантике/транспиляторе).
    return std::make_shared<Binary>(ParserToken::Kind::AssignOp, term, &ctx);
}

AstNodePtr TermToAstConverter::visit_ASSIGN(const trust::TermPtr& term, Context& ctx) {
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

namespace {

// Элементы литерала словаря/кортежа из канонических пар (name, term) грамматики `args`:
// нормализуются к ЕДИНОЙ форме Binary(AssignOp) (left=Ident-метка или пустой, right=значение).
void appendDictElementsFromArgs(Context& ctx, const trust::TermPtr& term, DictLiteralNode& node) {
    if (!term || !term->m_args) {
        return;
    }
    for (const auto& [name, argTerm] : *term->m_args) {
        AstNodePtr value;
        AstNodePtr typeAnn; // явная аннотация типа члена (`name:Type`)
        if (argTerm && argTerm->getTermID() == trust::TermID::ARGUMENT) {
            value = convertChild(ctx, argTerm->m_right); // именованный name=value: правая часть
            // Тип может лежать на ARGUMENT (m_type) или на его значении (m_right->m_type) -
            // грамматика `name type_item named_rhs` ставит его на значение.
            if (argTerm->m_type) {
                typeAnn = convertChild(ctx, argTerm->m_type);
            } else if (argTerm->m_right && argTerm->m_right->m_type) {
                typeAnn = convertChild(ctx, argTerm->m_right->m_type);
            }
        } else {
            value = convertChild(ctx, argTerm); // безымянный элемент: само выражение
            if (argTerm && argTerm->m_type) {
                typeAnn = convertChild(ctx, argTerm->m_type);
            }
        }

        std::string elname = name;
        // ЕДИНЫЙ узел аргумента: (имя из ArgsPair, явный тип, значение). Безнарный член enum/variant
        // `HIGH` (имя в value-Ident) НЕ сдвигается здесь - имя/значение разрешает анализатор
        // (enumVariantMember), т.к. в словаре голое значение - это значение, а не имя.
        // Именованный ARGUMENT-член (name=value) строим ЧЕРЕЗ ТЕРМ: сохраняем исходный source-range
        // члена (иначе range() невалиден, т.к. ручной конструктор без терма). Необходимо для
        // source-map/name-маппинга членов Enum/Variant/Dict. Голые значения (безымянные) - как было.
        AstNodePtr arg;
        if (argTerm && argTerm->getTermID() == trust::TermID::ARGUMENT) {
            arg = std::make_shared<ArgNode>(argTerm, std::move(typeAnn), std::move(value));
        } else {
            arg = std::make_shared<ArgNode>(std::move(elname), std::move(typeAnn), std::move(value));
        }
        node.m_body.push_back(std::move(arg));
    }
}

// Является ли аргумент `Tuple(...)` «типовой формой»: TYPE-терм (`:Rational`) или имя с типом
// (`sum:Rational` - m_type задан). В позиции аннотации типа аргументы - типы, в позиции
// литерала - значения (`1`, `'x'`). Различает `Tuple(:Rational, ...)` (тип) от `:Tuple(1, ...)` (литерал).
bool argIsTypeForm(const trust::TermPtr& argTerm) {
    if (!argTerm) {
        return false;
    }
    if (argTerm->getTermID() == trust::TermID::TYPE) {
        return true;
    }
    return argTerm->m_type != nullptr; // name:Type - имя с типом (не значение)
}

bool isAllTupleTypeArgs(const trust::TermPtr& term) {
    if (!term || !term->m_args || term->m_args->empty()) {
        return false;
    }
    for (const auto& [name, argTerm] : *term->m_args) {
        (void)name;
        if (!argIsTypeForm(argTerm)) {
            return false;
        }
    }
    return true;
}

} // namespace

AstNodePtr TermToAstConverter::visit_DICT(const trust::TermPtr& term, Context& ctx) {
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

AstNodePtr TermToAstConverter::visit_TENSOR(const trust::TermPtr& term, Context& ctx) {
    // Литерал массива `[1,2:Int8,3,]` (возможно типизированный `[...]:Int32`) → DictLiteralNode
    // с kind=ArrayInit (позиционные элементы; имя-метка у элемента - ""). Аннотация `]:Type`
    // сохраняется механически в m_type - интерпретирует анализатор (тип элемента массива).
    auto node = std::make_shared<DictLiteralNode>(ParserToken::Kind::ArrayInit, term);
    node->m_type = term && term->m_type ? convertChild(ctx, term->m_type) : nullptr;
    appendDictElementsFromArgs(ctx, term, *node);
    return node;
}

AstNodePtr TermToAstConverter::visit_RANGE(const trust::TermPtr& term, Context& ctx) {
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

// Блоки перехвата прерываний {+}/{ -}/{*} → TryCatchStmt. Обычный { ... } (BLOCK)
// конвертируется как ScopeBlock; эти три вида - как try/catch-блок. Синтетических веток НЕ добавляем:
// при отсутствии явных catch кодогенерация берёт класс по text() ('{+'→IntPlus, '{-'→IntMinus,
// '{*'→IntAny) как пустой catch-перехват (swallow). {*} (try/catch/else из макросов) - visit_BLOCK_TRY.
AstNodePtr TermToAstConverter::visit_BLOCK_PLUS(const trust::TermPtr& term, Context& ctx) {
    return std::make_shared<TryCatchStmt>(ParserToken::Kind::TryCatchStmt, term, &ctx);
}

AstNodePtr TermToAstConverter::visit_BLOCK_MINUS(const trust::TermPtr& term, Context& ctx) {
    return std::make_shared<TryCatchStmt>(ParserToken::Kind::TryCatchStmt, term, &ctx);
}

AstNodePtr TermToAstConverter::visit_BLOCK_TRY(const trust::TermPtr& term, Context& ctx) {
    return std::make_shared<TryCatchStmt>(ParserToken::Kind::TryCatchStmt, term, &ctx);
}

AstNodePtr TermToAstConverter::visit_ARGUMENT(const trust::TermPtr& term, Context& ctx) {
    // ЕДИНЫЙ узел аргумента (name в m_left, тип в m_type, значение в m_right) → ArgNode.
    // Раскладка слотов - в ArgNode-конструкторе (ast_nodes.cpp).
    return std::make_shared<ArgNode>(ParserToken::Kind::ArgNode, term, &ctx);
}

AstNodePtr TermToAstConverter::visit_TYPE(const trust::TermPtr& term, Context& ctx) {
    // Нативный шаблон-тип `vector<Int32>` (грамматика `type_class LT template_args GT`): признак и
    // типовые аргументы - в m_template. Строим IdentType с типовыми аргументами (каждый - узел типа:
    // голое `Int32` → IdentName, `:Int32`/`:MyClass<:Int8>` → IdentType). Отличие от кортежных
    // параметров `:Type(...)` (m_args, ветка ниже) - изолированный механизм (см. MEMORY.md).
    if (term->m_template.has_value()) {
        auto tpl = std::make_shared<IdentType>(term);
        std::vector<AstNodePtr> args;
        args.reserve(term->m_template->size());
        for (const auto& [aname, argTerm] : *term->m_template) {
            (void)aname;
            if (argTerm) {
                args.push_back(convertChild(ctx, argTerm));
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
    // Конструкция/литерал - когда есть вызов-аргументы (m_args) и m_type НЕ является
    // размерностями `[...]` (ARGS-терм; `Matrix[2,3](Float)` - аннотация с dims → default).
    // Хвостовая элементная аннотация `:Type(...):Elem` (m_type - TYPE-терм) допускается.
    if (term->m_args && !(term->m_type && term->m_type->getTermID() == trust::TermID::ARGS)) {
        // Параметризованная АННОТАЦИЯ типа `Tuple(:Rational, sum:Rational)` (все аргументы -
        // типы, `:Rational` / `name:Type`) → IdentType с параметрами (позиционные → IdentType,
        // именованные → ArgNode(name, type)), чтобы тип не терялся. Это решение чисто
        // механическое (аргументы - ТИПЫ), не зависит от имени типа.
        if (isAllTupleTypeArgs(term)) {
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
                    params->push_back(std::make_shared<ArgNode>(std::string(argTerm->getText()), convertChild(ctx, argTerm->m_type)));
                }
            }
            return std::make_shared<IdentType>(term, std::nullopt, std::move(params));
        }
        // `:Type(args)` с НЕ-типовыми аргументами - литерал/конструкция/вызов (значения).
        // ЕДИНЫЙ узел DictLiteralNode с аннотацией типа (m_type = контейнер). Анализатор по
        // СЛОТУ (тип/значение) и типу из реестра решает: в value-слоте Tuple → kind=Tuple,
        // иначе - каст/конструктор (кодогенерация); в type-слоте аннотация - resolveType.
        // Константность `^` (`:Array^`) → attr::ReadOnly на контейнере (→ std::array).
        // Хвостовая элементная аннотация `:Type(...):Elem` (term->m_type) → arrayElementAnnotation.
        {
            auto node = std::make_shared<DictLiteralNode>(ParserToken::Kind::DictLiteral, term);
            // Контейнер типа: IdentType из терма - normalizeTermText конструктора срезает
            // ведущий ':' и хвостовой '^' (`:Array^` → "Array").
            node->m_type = std::make_shared<IdentType>(term);
            // Константность '^' в имени контейнера → attr::ReadOnly. Единый хелпер
            // applyReadonlyFromCaret (как в convertAttrsToNode) получает ИСХОДНЫЙ текст терма
            // (в т.ч. '^'), сам определяет наличие суффикса и применяет атрибут; '^' здесь не
            // срезается - это сделал normalizeTermText конструктора.
            applyReadonlyFromCaret(static_cast<AstNodeAttr&>(*node->m_type), term->getText(), &ctx.attrs());
            node->prefix = true; // `:Type(args)` - префиксная форма (type-call/конструкция)
            if (term->m_type) {
                node->arrayElementAnnotation = convertChild(ctx, term->m_type);
            }
            appendDictElementsFromArgs(ctx, term, *node);
            return node;
        }
    }
    return TermVisitorDefault::visit_TYPE(term, ctx);
}

namespace {

// `"fmt"(args)` / `'fmt'(args)` - строка как формат-строка (правило `string: strtype call`,
// parser.y): аргументы цепляются к строковому терму. Строим CallExpr(callee=Literal),
// чтобы транспилятор мог эмитить `std::format(fmt, args...)`. Без аргументов обычный литерал.
AstNodePtr buildStringFormatCall(ParserToken::Kind litKind, const trust::TermPtr& term, Context& ctx) {
    auto call = std::make_shared<CallExpr>(ParserToken::Kind::CallExpr, std::make_shared<Literal>(litKind, term));
    if (term->m_args && !term->m_args->empty()) {
        call->m_args.emplace();
        for (const auto& [name, argTerm] : *term->m_args) {
            (void)name;
            if (argTerm) {
                call->m_args->push_back(convertChild(ctx, argTerm));
            }
        }
    }
    return call;
}

} // namespace

AstNodePtr TermToAstConverter::visit_STRWIDE(const trust::TermPtr& term, Context& ctx) {
    if (term->m_args) {
        return buildStringFormatCall(ParserToken::Kind::StrWide, term, ctx);
    }
    return TermVisitorDefault::visit_STRWIDE(term, ctx);
}

AstNodePtr TermToAstConverter::visit_STRCHAR(const trust::TermPtr& term, Context& ctx) {
    if (term->m_args) {
        return buildStringFormatCall(ParserToken::Kind::StrChar, term, ctx);
    }
    return TermVisitorDefault::visit_STRCHAR(term, ctx);
}

// -- Свободные хелперы конвертации (см. term_to_ast.hpp) --
// Устраняют дублирование `TermToAstConverter conv{ctx}; ...` в терм-конструкторах узлов.

AstNodePtr convertChild(Context& ctx, const trust::TermPtr& term) {
    TermToAstConverter conv{ctx};
    return conv.convert(term);
}

void convertChildren(Context& ctx, const trust::TermPtr& term, std::vector<AstNodePtr>& out) {
    TermToAstConverter conv{ctx};
    conv.convertSeq(term, out);
}

void convertModuleBody(Context& ctx, const trust::TermPtr& term, std::vector<AstNodePtr>& out) {
    if (!term) {
        return;
    }
    TermToAstConverter conv{ctx};
    // SEQUENCE-терм - контейнер тела модуля: разворачиваем его детей (convertSeq разворачивает
    // вложенные SEQUENCE-термы, пользовательские { ... }/BLOCK-термы сохраняет как ScopeBlock).
    // Модуль сам открывает глобальный скоуп, поэтому ScopeBlock-обёртка не создаётся.
    if (term->getTermID() == trust::TermID::SEQUENCE) {
        conv.convertSeq(term, out);
    } else if (AstNodePtr cn = conv.convert(term); cn) {
        out.push_back(std::move(cn));
    }
}

} // namespace trust
