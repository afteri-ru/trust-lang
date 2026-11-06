#include "semantic/core/name_resolution.hpp"
#include "diag/flag_values.hpp"
#include "semantic/core/debug_scope.hpp"
#include "utils/utils.hpp"
#include "semantic/solver.hpp"
#include "semantic/ref/stack_check.hpp"
#include "symbols/symbol_table.hpp"
#include "semantic/typer/type_inference.hpp"
#include "attrs/attr_builtin.hpp"
#include "ast/ref_syntax.hpp"
#include "semantic/ref/ref_kind.hpp"
#include "ast/token.hpp"
#include "diag/diag.hpp"
#include "diag/options.hpp"
#include "diag/registry.hpp"
#include "semantic/diag.hpp"
#include "semantic/ref/ref_qualifiers.hpp"
#include "types/call.hpp"
#include "types/type_system.hpp"
#include "types/type_id.hpp"
#include "types/names.hpp"
#include "utils/strings.hpp"
#include <algorithm>
#include <format>
#include <string>

namespace trust {
// Проверка области применения макроса: маркер встречается в теле макроса на сайте раскрытия.
// Текущая область выводится из ЕДИНОГО скоуп-стека (создатели скоупов) - без отдельного
// параллельного стека областей. Маркер не генерирует код и УДАЛЯЕТСЯ после проверки.
void NameResolutionPass::analyzeCheckAreaStmt(AstNodePtr& self) {
    if (!self || self->kind() != ParserToken::Kind::CheckAreaStmt) {
        self = nullptr;
        return;
    }
    // Типизированный доступ без RTTI (R1). Локальный владелец сохраняет узел живым, пока
    // self обнуляется ниже (маркер удаляется из дерева).
    AstNodePtr node = std::move(self);
    auto* st = node->as<CheckAreaStmt>();
    const MapperRange rng = st->range();
    if (!st->area.has_value()) { // область не задана - нечего проверять
        self = nullptr;
        return;
    }
    const AreaKind required = *st->area;

    // Текущие области из ЕДИНОГО скоуп-стека (creator-узлы, от внутреннего к глобальному).
    std::vector<const AstNodeBase*> creators;
    m_actx.symbols().forEachScope([&](const Scope& s) {
        if (s.creator) {
            creators.push_back(s.creator);
        }
    });
    bool hasClass = false;
    for (const AstNodeBase* n : creators) {
        if (n->kind() == ParserToken::Kind::ClassDecl) {
            hasClass = true;
        }
    }
    std::vector<AreaKind> cur;
    for (const AstNodeBase* n : creators) {
        if (n->kind() == ParserToken::Kind::ModuleDecl) {
            cur.push_back(AreaKind::Module);
        } else if (n->kind() == ParserToken::Kind::ScopeBlock || n->kind() == ParserToken::Kind::sequence) {
            cur.push_back(AreaKind::Block);
        } else if (n->is<FuncDecl>()) {
            cur.push_back(AreaKind::Function);
            if (hasClass) {
                cur.push_back(AreaKind::Method);
            }
        } else if (n->kind() == ParserToken::Kind::ClassDecl) {
            cur.push_back(AreaKind::Class);
        } else if (n->is<WhileStmt>()) {
            cur.push_back(AreaKind::While);
            cur.push_back(AreaKind::Loop);
        } else if (n->is<DoWhileStmt>()) {
            cur.push_back(AreaKind::DoWhile);
            cur.push_back(AreaKind::Loop);
        } else if (n->is<WithStmt>()) {
            cur.push_back(AreaKind::With);
        } else if (n->is<TryCatchStmt>()) {
            cur.push_back(AreaKind::Try);
        } else if (n->is<CatchBlock>()) {
            cur.push_back(AreaKind::Catch);
        } else if (n->is<IfStmt>()) {
            // Whole-if: создатель IfStmt-скоупа означает «внутри if» (then/elseif/else - не
            // различаются, ветки отдельных скоупов не открывают). Отдельных elseif/else НЕ
            // заявляем (см. TRUST_CHECK_AREAS), пока не реализована per-branch-детекция.
            cur.push_back(AreaKind::If);
        } else if (n->is<MatchStmt>()) {
            // Whole-match: создатель MatchingStmt-скоупа = «внутри match» (любая ветка/default).
            cur.push_back(AreaKind::Match);
        }
    }

    bool inside = false;
    for (const auto& c : cur) {
        if (c == required) {
            inside = true;
            break;
        }
    }

    // Атрибуты (AttrId) на ближайшем creator, несущем атрибуты.
    bool attrsOk = true;
    const AstNodeBase* areaNode = nullptr;
    for (const AstNodeBase* n : creators) {
        if (n->as_attr()) {
            areaNode = n;
            break;
        }
    }
    if (!st->attrs().empty()) {
        if (!areaNode) {
            attrsOk = false;
        } else {
            const AstNodeAttr* an = areaNode->as_attr();
            for (const AttrId id : st->attrs()) { // требуемые атрибуты лежат в attrs() самого маркера
                if (!an->has_attr(id)) {
                    attrsOk = false;
                    break;
                }
            }
        }
    }

    if (inside && attrsOk) {
        self = nullptr; // ограничение выполнено - маркер удаляем (кода не даёт)
        return;
    }

    // Severity: явный override (behavior) -> default -> -W-опция (nullopt = ignore).
    std::optional<Severity> sevOpt = st->behavior.has_value() ? st->behavior : std::optional<Severity>(m_actx.ctx().opts().get(semantic::DiagId::CheckArea));
    self = nullptr; // маркер всегда удаляется (не генератор кода)

    if (!sevOpt.has_value() || *sevOpt == Severity::Ignore || *sevOpt == Severity::Remark || *sevOpt == Severity::Note) {
        return; // ignore/низкий severity - без диагностики
    }
    const Severity sev = *sevOpt;

    std::string curList;
    for (const auto& c : cur) {
        if (!curList.empty()) {
            curList += ", ";
        }
        curList.append(areaKindName(c));
    }
    if (curList.empty()) {
        curList = "module(top-level)";
    }
    if (!attrsOk) {
        m_actx.ctx().diag().report(sev, rng, "@__CHECK_AREA__: current area '{}' lacks required attribute(s)", curList);
    } else {
        m_actx.ctx().diag().report(sev, rng, "@__CHECK_AREA__: macro allowed only inside area '{}', but current area is '{}'", areaKindName(required), curList);
    }
}

// `@__DEBUG__(<masks>)` / `@__DEBUG__()` - устанавливает (пустой => сбрасывает) фильтр сообщений
// TRUST_DEBUG и ЭХОМ печатает применённый фильтр (подтверждение, что параметры применились/
// изменились/выключены). `@__DEBUG_SCOPE__(...)` - печатает ТЕКУЩЕЕ СОСТОЯНИЕ анализатора в точке
// (независимо от фильтра сообщений). Каждая строка вывода префиксуется локацией самого МАКРОСА
// в исходном `.src` (`файл:строка: `). Маркер кода не генерирует и УДАЛЯЕТСЯ после обработки.
void NameResolutionPass::analyzeDebugStmt(AstNodePtr& self) {
    if (!self || self->kind() != ParserToken::Kind::DebugStmt) {
        self = nullptr;
        return;
    }
    // Типизированный доступ без RTTI (R1); локальный владелец - чтобы узел жил до self = nullptr.
    AstNodePtr node = std::move(self);
    auto* st = node->as<DebugStmt>();

    // Префикс локации МАКРОСА в исходнике (.src) - ставится на КАЖДУЮ строку его вывода.
    // Каталоги сокращаются до многоточия: `.../file.src:строка`.
    const MapperRange markerRange = st->range();
    const auto lc = m_actx.ctx().source().line_column(markerRange.begin);
    const std::string loc = std::format("{}:{}: ", trust::utils::shortenedPath(m_actx.ctx().source().filename(markerRange.begin)), lc.line);

    if (st->mode == DebugMode::Filter) {
        // Нет аргументов => выключить вывод (пустой фильтр); иначе - заданные маски.
        trust::trace::setFilter(st->args.empty() ? std::string_view{} : st->args[0]);
        // ЭХО применения фильтра - безусловно (в т.ч. при выключении), чтобы в выводе было видно,
        // какие параметры фильтрации применились.
        trust::trace::out() << trust::utils::prefixEachLine(st->args.empty() ? std::string("@__DEBUG__: debug messages DISABLED (empty filter)")
                                                                             : std::format("@__DEBUG__: debug messages ENABLED, filter='{}'", st->args[0]),
                                                            loc);
        self = nullptr;
        return;
    }

    // `@__DEBUG_SCOPE__` печатает ТЕКУЩЕЕ СОСТОЯНИЕ анализатора в точке: как только узел достигнут,
    // вызывается функция дампа с опциями макроса. Аргументы: позиционные — ФИЛЬТР ИМЁН (маски;
    // пусто — полный дамп), затем именованные опции (валидированы на парсинге макроса).
    std::vector<std::string_view> named;
    std::string nameMasks;
    for (std::string_view arg : st->args) {
        if (arg.find('=') != std::string_view::npos) {
            named.push_back(arg);
            continue;
        }
        if (!nameMasks.empty()) {
            nameMasks += ',';
        }
        nameMasks += arg;
    }
    trust::debug::ScopeDumpOpts opts;
    std::string err;
    if (!trust::debug::parseScopeDumpArgs(named, opts, err)) {
        m_actx.ctx().diag().report(Severity::Error, st->range(), "@__DEBUG_SCOPE__: {} (supported options: {})", err, trust::trace::scopeDumpOptionsUsage());
    } else {
        if (!nameMasks.empty()) {
            opts.name_mask = nameMasks;
            opts.show_names = true;
        }
        trust::trace::out() << trust::utils::prefixEachLine(trust::debug::formatScopeStack(m_actx.symbols(), opts), loc);
    }
    self = nullptr; // маркер всегда удаляется (кода не даёт)
}

// Переопределяет сравнение по значению (==/===>): вместо (tmp == pattern) в каждой ветке
// кодогенерация эмитит вызов fn(tmp, pattern). Семантика резолвит имя функции и проверяет,
// что это объявленная функция-предикат с сигнатурой `bool fn(T_value, T_pattern)`.
void NameResolutionPass::analyzeMatchMatcher(MatchStmt& match) {
    const AttrPool& pool = m_actx.ctx().attrs();
    const auto matcher_id = pool.lookup(attr::Matcher);
    if (!matcher_id.has_value() || !match.has_attr(*matcher_id)) {
        return; // атрибута нет - ничего не делаем
    }
    const MapperRange rng = match.range();
    const std::vector<std::string>* args = match.attr_args(*matcher_id);
    if (!args || args->empty() || (*args)[0].empty()) {
        m_actx.ctx().diag().report(Severity::Error, rng,
                                   "@[matcher(...)] expects one argument - the name of a predicate function 'bool fn(T_value, T_pattern)'");
        return;
    }
    const std::string name = (*args)[0];

    // Matcher переопределяет сравнение ПО ЗНАЧЕНИЮ; сопоставление по типу несовместимо.
    const bool typeMatch = isTypeMatchOp(match.m_op);
    if (typeMatch) {
        m_actx.ctx().diag().report(Severity::Error, rng,
                                   "@[matcher(\"{}\")] is not applicable to type-matching operator '{}'; matcher overrides value comparison", name,
                                   matchOpSpelling(match.m_op));
        return;
    }
    // matcher-предикат резолвится по trust-имени; нативных имён в выражениях нет.
    Symbol* s = resolveSimple(nullptr, name);
    if (!s || !s->decl || s->decl->kind() != ParserToken::Kind::FuncDecl) {
        m_actx.ctx().diag().report(Severity::Error, rng,
                                   "@[matcher(\"{}\")]: '{}' is not a declared function; a matcher predicate must be declared before this match", name, name);
        return;
    }
    // Результат резолва сохраняется на узле: кодоген читает объявление, а не повторно резолвит
    // имя по тексту (R53 — без identity-by-text в бэкенде).
    match.m_matcherDecl = s->decl;
    const TypeReadModel reg = m_actx.ctx().readTypes();
    const auto* fd = reg.store().getTypeDataAs<FunctionTypeData>(s->type.getTypeId());
    if (!fd) {
        return; // сигнатура неизвестна (обобщённая/шаблон) - кодоген эмитит вызов как есть
    }
    if (fd->paramTypes.size() != 2) {
        m_actx.ctx().diag().report(Severity::Error, rng, "@[matcher(\"{}\")]: predicate must take exactly 2 arguments (T_value, T_pattern), got {}", name,
                                   fd->paramTypes.size());
        return;
    }
    const TypeId rt = fd->returnType == INVALID_TYPE_ID ? INVALID_TYPE_ID : reg.store().desugaredTypeId(fd->returnType);
    const bool returnsBool = rt != INVALID_TYPE_ID && typeGroupOf(rt) == Group::kLogical;
    if (!returnsBool) {
        m_actx.ctx().diag().report(Severity::Error, rng, "@[matcher(\"{}\")]: predicate must return bool (got '{}')", name,
                                   fd->returnType == INVALID_TYPE_ID ? "void" : std::string(reg.printer().typeNameOf(fd->returnType)));
    }
}

// Единый источник для переменных (analyzeVarDecl) и параметров (declareFuncParams).
QualType NameResolutionPass::applyRefAttrs(QualType base, const AstNodeAttr& node, MapperRange range, TypeId* deleterOutsideType) {
    return applyDeclarationQualifiers(m_actx.ctx(), m_actx.types(), base, node, range, deleterOutsideType);
}

} // namespace trust
