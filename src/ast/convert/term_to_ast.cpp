// Term->AST conversion driver: dispatch, attribute transfer, sequence flattening.

#include "ast/term_to_ast.hpp"
#include "ast/convert/term_to_ast_internal.hpp"
#include "ast/ast_nodes.hpp"
#include "attrs/attr_builtin.hpp"
#include "attrs/attr_args.hpp"
#include "ast/nodes/attr_parser.hpp"
#include "syntax/check_area.hpp"
#include "ast/ident_name.hpp"
#include "ast/token_base.hpp"
#include "ast/token_type.hpp"
#include "syntax/term/term.h"
#include "syntax/term/term_types.h"
#include "ast/convert_services.hpp"
#include "types/typekind.hpp"
#include "utils/error.hpp"
#include "utils/string_interner.hpp"

#include <memory>
#include <optional>
#include <type_traits>
#include <vector>

namespace trust {

AstNodePtr dispatchTerm(const trust::TermPtr& term, TermVisitor& visitor, ConvertServices& ctx) {
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
        // при наличии ошибок (см. Pipeline::run: проверка errorCount после ParseAST).
        ctx.diag().report(Severity::Error, term->m_mapperRange, "Конструкция '{}' не реализована", trust::toString(term->getTermID()));
        return nullptr;
    }
}

namespace detail {

void convertAttrsToNode(const trust::TermPtr& term, AstNodePtr& node, ConvertServices& ctx) {
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
            // Для name-node атрибут уже применён в assignNameFromRaw; здесь - идемпотентная
            // страховка для не-имён (`*^`, `&^`, `method^()`). Текст не срезается: '^' остаётся
            // частью spelling (name-node хранит уже каноническое имя без '^').
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
bool hasConvertibleChildren(const trust::TermPtr& term) {
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

/// Экспорт нативного имени trust-сущности: `@[export("fib")]` / `@[export("ns::fib")]`.
/// Единственный писатель целевого C++-имени для ЭКСПОРТА (`DeclCppFacts`, владелец реестра; R1);
/// импорт пишут конструкторы объявлений из RHS-цели `:= %cpp ...`. Оба источника сразу — ошибка
/// (один факт, один писатель).
/// ABI — `cppLinkageOf(name)` в точке кодогена. Экспорт требует определения (тело / инициализатор).
void applyExportBinding(const AstNodePtr& node, ConvertServices& ctx) {
    if (!node) {
        return;
    }
    AstNodeAttr* attrNode = node->as_attr();
    if (attrNode == nullptr) {
        return;
    }
    const AttrPool& pool = ctx.attrs();
    const auto exportId = pool.lookup(attr::Export);
    if (!exportId.has_value() || !attrNode->has_attr(*exportId)) {
        return;
    }
    const ParserToken::Kind kind = node->kind();
    const bool isFunction = kind == ParserToken::Kind::FuncDecl;
    const bool isVariable = kind == ParserToken::Kind::VarDecl;
    if (!isFunction && !isVariable) {
        // Диагностика — только на самом объявлении. Вложенные узлы имени/типа могут нести тот же
        // атрибут при разборе (`@[include]`/`@[export]` на терме-имени); они не являются
        // объявлением и здесь пропускаются, иначе одна ошибка репортилась бы дважды.
        if (kind == ParserToken::Kind::TypeDecl || kind == ParserToken::Kind::ClassDecl || kind == ParserToken::Kind::StructDecl ||
            kind == ParserToken::Kind::DestructureDecl) {
            ctx.diag().report(Severity::Error, node->range(),
                              "native export is applicable only to a free function or variable; '{}' does not publish a C/C++ symbol",
                              node->kind() == ParserToken::Kind::DestructureDecl ? "destructuring declaration" : "type declaration");
        }
        return;
    }
    const std::vector<std::string>* args = attrNode->attr_args(*exportId);
    if (args == nullptr || args->empty() || args->at(0).empty()) {
        // Пустой/отсутствующий аргумент не является маркером: экспорт обязан назвать C/C++-символ.
        ctx.diag().report(Severity::Error, node->range(), "native export requires a non-empty C/C++ symbol name argument: '@[export(\"name\")]'");
        return;
    }
    const std::string& name = args->at(0);
    if (isFunction && static_cast<const FuncDecl&>(*node).isLambda()) {
        ctx.diag().report(Severity::Error, node->range(), "native export is not applicable to a lambda: the exported C/C++ symbol must be a named free function");
        return;
    }
    auto& decl = static_cast<Decl&>(*node);
    if (decl.bindsNativeCpp()) {
        ctx.diag().report(Severity::Error, node->range(),
                          "native name is set twice: by the right-hand side ':= %name ...' and by '@[export(\"{}\")]'", name);
        return;
    }
    const bool hasBody = isFunction ? static_cast<const FuncDecl&>(decl).hasBody()
                                    : static_cast<const VarDecl&>(decl).initKind() == VarInitKind::Initialized;
    if (!hasBody) {
        ctx.diag().report(Severity::Error, node->range(),
                          "native export '{}' requires a definition (function body / variable initializer)", name);
        return;
    }
    if (isFunction && static_cast<const FuncDecl&>(decl).m_templateParams.has_value()) {
        ctx.diag().report(Severity::Error, node->range(),
                          "native export of a template declaration is not supported: the exported C/C++ symbol must be concrete");
        return;
    }
    decl.m_pendingNativeCppName = name;
    decl.m_bindsNativeCpp = true;
}

/// Объявления создают trust-идентификатор; `%` допустим только как маркер RHS-цели.
void rejectNativeDeclaredName(const AstNodePtr& node, ConvertServices& ctx) {
    if (!node) {
        return;
    }
    // TypeDecl — Binary (не Decl): LHS-имя проверяется отдельно.
    if (node->kind() == ParserToken::Kind::TypeDecl) {
        const auto* binary = node->as<Binary>();
        const AstNodeBase* left = binary != nullptr ? binary->m_left.get() : nullptr;
        const HasText* leftText = left != nullptr ? left->as_text() : nullptr;
        if (leftText != nullptr && !leftText->text().empty() && leftText->text().front() == '%') {
            ctx.diag().report(Severity::Error, left->range(),
                              "native name '{}' cannot be declared: a declaration creates a trust identifier only; declare the trust name on the left",
                              leftText->text());
        }
        return;
    }
    const ParserToken::Kind k = node->kind();
    if (k != ParserToken::Kind::FuncDecl && k != ParserToken::Kind::VarDecl && k != ParserToken::Kind::ClassDecl &&
        k != ParserToken::Kind::StructDecl) {
        return;
    }
    const auto& decl = static_cast<const Decl&>(*node);
    const std::string name(decl.text());
    if (name.empty() || name.front() != '%') {
        return;
    }
    const char* hint = (k == ParserToken::Kind::ClassDecl || k == ParserToken::Kind::StructDecl)
                           ? "declare the trust name on the left and the C++ name on the right: 'Name ::= %cpp { };'"
                           : (k == ParserToken::Kind::VarDecl ? "write '<name>:Type := %cpp ...;' (native import)"
                                                              : "write '<name>(...):T := %cpp ...;' (native import)");
    ctx.diag().report(Severity::Error, node->range(),
                      "native name '{}' cannot be declared: a declaration creates a trust identifier only; {}", name, hint);
}

} // namespace detail

// Каждый visit_<NAME> делегирует в convertForKind<Kind>: класс-селекция (Ident, control-flow)
// либо generic по node_type_for_kind_t<Kind>. Раскладка детей - в терм-конструкторах узлов.

template <ParserToken::Kind K>
AstNodePtr TermVisitorDefault::convertForKind(const trust::TermPtr& term, ConvertServices& ctx) {
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
        // (pipeline) не запускает semantic/transpile при наличии ошибок (Pipeline::run: errorCount после ParseAST).
        ctx.diag().report(Severity::Error, term->m_mapperRange, "Конструкция '{}' не реализована", trust::toString(term->getTermID()));
        return nullptr;
    } else if constexpr (K == ParserToken::Kind::Ident) {
        // Класс-селекция Ident→CallExpr vs IdentName: структурный предикат
        // «есть конвертируемые дети». Раскладка детей (callee/args) - в CallExpr-конструкторе.
        // `%name` в ПОЗИЦИИ ВЫРАЖЕНИЯ - сырая нативная ссылка: запрещена. Нативное имя —
        // свойство объявленной trust-сущности (RHS `:= %cpp ...` / `@[export("…")]`), ссылки идут
        // по trust-именам. Объявление-источник имени проверяется отдельно
        // (rejectNativeDeclaredName), сюда его терм не попадает.
        if (!term->getText().empty() && term->getText().front() == '%') {
            ctx.diag().report(Severity::Error, term->m_mapperRange,
                              "native reference '{}' is not allowed: use the trust name of the declared entity "
                              "(declare the native name on the entity: ':= %cpp ...' or '@[export(\"name\")]')",
                              term->getText());
            return nullptr;
        }
        if (detail::hasConvertibleChildren(term)) {
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
            // moduleRef — внутренний факт (модуль-владелец имени), не синтаксис ссылки.
            if constexpr (std::is_base_of_v<IdentName, NT>) {
                if (!term->m_moduleIndices.empty()) {
                    node->moduleRef = term->m_moduleIndices.front();
                }
            }
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
    AstNodePtr TermVisitorDefault::visit_##name(const trust::TermPtr& term, ConvertServices& ctx) { \
        return this->convertForKind<ParserToken::Kind::kind>(term, ctx);                    \
    }
#define TRUST_TVD_DEF_GENCASE3(name, kind, T)                                               \
    AstNodePtr TermVisitorDefault::visit_##name(const trust::TermPtr& term, ConvertServices& ctx) { \
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

AstNodePtr TermToAstConverter::convert(const trust::TermPtr& term) {
    if (!term || term->getTermID() == trust::TermID::END) {
        return nullptr;
    }

    // Определение макроса (MACRO_SEQ с телом в m_right) или его удаление (MACRO_DEL) -
    // это compile-time конструкция, в AST не попадает (макросы уже раскрыты/зарегистрированы).
    if (term->isMacro()) {
        return nullptr;
    }

    std::string_view text = term->getText();

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
            marker->behavior = (b == "default")   ? std::optional<Severity>(std::nullopt)
                               : (b == "ignore")  ? std::optional<Severity>(Severity::Ignore)
                               : (b == "warning") ? std::optional<Severity>(Severity::Warning)
                               : (b == "error")   ? std::optional<Severity>(Severity::Error)
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
                m_ctx.diag().report(Severity::Error, term->m_mapperRange, "@__CHECK_AREA__: unknown attribute '{}'", argTexts[i]);
            }
        }
        return marker;
    }

    // Встроенные системные маркеры отладочного вывода (УРОВЕНЬ 1): `@__DEBUG__` / `@__DEBUG_SCOPE__`
    // приходят единым TermID::MACRO_CONTEXT-термом (текст = имя макроса, аргументы - дочерние
    // NAME-термы). Строим лист-узел DebugStmt; семантика применяет эффект (установка фильтра /
    // дамп состояния скоупа) и УДАЛЯЕТ маркер - до транспилятора он не доходит.
    if (term->getTermID() == trust::TermID::MACRO_CONTEXT && (text == "@__DEBUG__" || text == "@__DEBUG_SCOPE__")) {
        auto marker = std::make_shared<DebugStmt>(ParserToken::Kind::DebugStmt, term, &m_ctx);
        marker->mode = (text == "@__DEBUG_SCOPE__") ? DebugMode::Scope : DebugMode::Filter;
        for (const auto& c : term->m_sequence) {
            if (c) {
                marker->args.emplace_back(internedTextSlot(c->getText()));
            }
        }
        return marker;
    }

    AstNodePtr node = dispatchTerm(term, *this, m_ctx);

    // Документирующий комментарий, привязанный грамматикой к терму-идентификатору
    // (объявлению), переносим в узел (term->m_docs - см. include/syntax/term/term.h). Для
    // не-объявлений m_docs пуст - док остаётся отдельным sibling-узлом Document.
    if (node && !term->m_docs.empty() && isDeclKindForDocs(node->kind())) {
        if (term->m_docs.size() == 1 && term->m_docs[0]) {
            // Один док интернируется: Term (в т.ч. owned-док макроса) может быть уничтожен
            // раньше AST, а documentation должна жить вместе с AST (в пуле строк `StringInterner`).
            node->setDocumentation(term->m_docs[0]->getText());
        } else {
            std::string d;
            for (const auto& doc : term->m_docs) {
                if (!doc) {
                    continue;
                }
                if (!d.empty()) {
                    d += "\n";
                }
                d += std::string(doc->getText());
            }
            node->setDocumentation(d);
        }
    }

    // Document - чистый leaf (полный текст комментария): никакой '^'-нормализации,
    // атрибутов или диагностики иммутабельности (комментарий может оканчиваться на '^').
    if (node && node->kind() == ParserToken::Kind::Document) {
        return node;
    }

    // Нормализация ':' (TypeName) и '^' (иммутабельность) выполняется ТОЛЬКО при создании
    // name-node (assignNameFromRaw). Здесь остаётся диагностика для контекстов, где '^'
    // синтаксически является квалификатором, но не применим: метки блоков/namespace и
    // объявления без поддержки квалификатора. Литералы ('"abc^"'), операторы ('^^') и прочие
    // носители текста НЕ диагностируются: их '^' - часть spelling.
    const auto termId = term->getTermID();
    const bool jumpLabel =
        termId == trust::TermID::INT_PLUS || termId == trust::TermID::INT_MINUS || termId == trust::TermID::INT_BREAK || termId == trust::TermID::INT_CONTINUE;
    const bool immutableContext = node && ((node->kind() == ParserToken::Kind::ScopeBlock || node->kind() == ParserToken::Kind::ModuleDecl) || jumpLabel ||
                                           (isNameCarrierKind(node->kind()) && !canHaveImmutableQualifier(node->kind())));
    if (!text.empty() && text.back() == '^' && immutableContext) {
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
        const bool isDefOp =
            (tID == trust::TermID::CREATE_NAME || tID == trust::TermID::CREATE_TYPE || tID == trust::TermID::TYPE_DEFINE || tID == trust::TermID::ASSIGN);
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

    detail::convertAttrsToNode(term, node, m_ctx);
    detail::applyExportBinding(node, m_ctx);
    detail::rejectNativeDeclaredName(node, m_ctx);
    return node;
}

void TermToAstConverter::convertSeq(const trust::TermPtr& term, std::vector<AstNodePtr>& out) {
    if (!term || term->getTermID() == trust::TermID::END) {
        return;
    }

    // SEQUENCE-терм - синтаксический контейнер операторов (`sequence` non-terminal) и doc-bundle,
    // НЕ пользовательский скоуп: рекурсивно разворачиваем, иначе вокруг тела блока/функции/дока
    // появляется лишний ScopeBlock-слой. Пользовательские скоупы - только BLOCK-термы.
    for (const auto& child : term->m_sequence) {
        if (child) {
            flattenInto(child, out);
        }
    }

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

    // Для jump-термов (INT_PLUS/INT_MINUS/INT_BREAK/INT_CONTINUE) m_left всегда null: значение живёт
    // в m_right, label/namespace - в тексте терма (переносится в JumpStmt::m_label конструктором).
    // Поэтому спец-исключение не требуется - jump-терм обходится единообразно.
    if (term->m_left) {
        AstNodePtr ln = convert(term->m_left);
        if (ln) {
            out.push_back(std::move(ln));
        }
    }

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

std::vector<AstNodePtr> TermToAstConverter::termToAst(const trust::TermPtr& term, ConvertServices& ctx) {
    // Тексты AST интернируются в пул компиляции (Context), а не в процессный interner.
    StringInternerScope stringsScope(ctx.strings());
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

// Элементы литерала словаря/кортежа из канонических пар (name, term) грамматики `args`:
// нормализуются к ЕДИНОЙ форме Binary(AssignOp) (left=Ident-метка или пустой, right=значение).
void appendDictElementsFromArgs(ConvertServices& ctx, const trust::TermPtr& term, DictLiteralNode& node) {
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
        // source-map/name-маппинга членов Enum/Variant/Dict. Голые значения (безымянные) не сдвигаются.
        AstNodePtr arg;
        if (argTerm && argTerm->getTermID() == trust::TermID::ARGUMENT) {
            arg = std::make_shared<ArgNode>(argTerm, std::move(typeAnn), std::move(value), &ctx.attrs());
        } else {
            arg = std::make_shared<ArgNode>(std::move(elname), std::move(typeAnn), std::move(value), &ctx.attrs());
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
    if (argTerm->getTermID() == trust::TermID::ARGUMENT) {
        const trust::TermPtr inner = argTerm->m_type ? argTerm->m_type : argTerm->m_right;
        return inner && inner->getTermID() == trust::TermID::TYPE;
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

// `"fmt"(args)` / `'fmt'(args)` - строка как формат-строка (правило `string: strtype call`,
// parser.y): аргументы цепляются к строковому терму. Строим CallExpr(callee=Literal),
// чтобы транспилятор мог эмитить `std::format(fmt, args...)`. Без аргументов обычный литерал.
AstNodePtr buildStringFormatCall(ParserToken::Kind litKind, const trust::TermPtr& term, ConvertServices& ctx) {
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

// Устраняют дублирование `TermToAstConverter conv{ctx}; ...` в терм-конструкторах узлов.

AstNodePtr convertChild(ConvertServices& ctx, const trust::TermPtr& term) {
    TermToAstConverter conv{ctx};
    return conv.convert(term);
}

void convertChildren(ConvertServices& ctx, const trust::TermPtr& term, std::vector<AstNodePtr>& out) {
    TermToAstConverter conv{ctx};
    conv.convertSeq(term, out);
}

void convertModuleBody(ConvertServices& ctx, const trust::TermPtr& term, std::vector<AstNodePtr>& out) {
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

// Блоки перехвата прерываний {+}/{ -}/{*} → TryCatchStmt. Обычный { ... } (BLOCK)
// конвертируется как ScopeBlock; эти три вида - как try/catch-блок. Синтетических веток НЕ добавляем:
// при отсутствии явных catch кодогенерация берёт тип по TryKind (Plus→IntPlus, Minus→IntMinus,
// Any→IntAny) как пустой catch-перехват (swallow). {*} (try/catch/else из макросов) - visit_BLOCK_TRY.
AstNodePtr TermToAstConverter::visit_BLOCK_PLUS(const trust::TermPtr& term, ConvertServices& ctx) {
    return std::make_shared<TryCatchStmt>(ParserToken::Kind::TryCatchStmt, term, &ctx);
}

AstNodePtr TermToAstConverter::visit_BLOCK_MINUS(const trust::TermPtr& term, ConvertServices& ctx) {
    return std::make_shared<TryCatchStmt>(ParserToken::Kind::TryCatchStmt, term, &ctx);
}

AstNodePtr TermToAstConverter::visit_BLOCK_TRY(const trust::TermPtr& term, ConvertServices& ctx) {
    return std::make_shared<TryCatchStmt>(ParserToken::Kind::TryCatchStmt, term, &ctx);
}

} // namespace trust
