// Generated: src/semantic/name_resolution.cpp (driver)
#include "semantic/name_resolution.hpp"
#include "diag/flag_values.hpp"
#include "semantic/debug_scope.hpp"
#include "utils/trace.hpp"
#include "semantic/analysis_common.hpp"
#include "semantic/format_check.hpp"
#include "semantic/solver.hpp"
#include "semantic/stack_check.hpp"
#include "analysis/symbol_table.hpp"
#include "semantic/type_inference.hpp"
#include "attrs/attr_builtin.hpp"
#include "ast/ref_syntax.hpp"
#include "semantic/ref_kind.hpp"
#include "ast/token.hpp"
#include "diag/diag.hpp"
#include "diag/options.hpp"
#include "diag/registry.hpp"
#include "semantic/diag.hpp"
#include "types/promotion.hpp"
#include "types/registry.hpp"
#include "types/type_id.hpp"
#include "types/type_names.hpp"
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
    m_actx.symbols().forEachScope([&](const SymbolTable::Scope& s) {
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

// -- Встроенные системные маркеры отладочного вывода (УРОВЕНЬ 1) ----------------
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
        trust::trace::setFilter(st->args.empty() ? std::string_view{} : std::string_view(st->args[0]));
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
    std::vector<std::string> named;
    std::string nameMasks;
    for (const std::string& arg : st->args) {
        if (arg.find('=') != std::string::npos) {
            named.push_back(arg);
            continue;
        }
        nameMasks += (nameMasks.empty() ? "" : ",") + arg;
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

// -- Атрибут @[matcher("fn")] на операторе match -------------------------------
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
    const bool typeMatch = (match.m_op == "~>" || match.m_op == "~~>" || match.m_op == "~~~>");
    if (typeMatch) {
        m_actx.ctx().diag().report(Severity::Error, rng,
                                   "@[matcher(\"{}\")] is not applicable to type-matching operator '{}'; matcher overrides value comparison", name, match.m_op);
        return;
    }
    // Нативный C++-символ (%...) - резолвить нельзя, сигнатуру не проверяем (кодоген эмитит как есть).
    if (!name.empty() && name.front() == '%') {
        return;
    }
    Symbol* s = resolveSimple(nullptr, name);
    if (!s || !s->decl || s->decl->kind() != ParserToken::Kind::FuncDecl) {
        m_actx.ctx().diag().report(Severity::Error, rng,
                                   "@[matcher(\"{}\")]: '{}' is not a declared function; a matcher predicate must be declared before this match", name, name);
        return;
    }
    TypeRegistry& reg = m_actx.ctx().types();
    const auto* fd = reg.getTypeDataAs<FunctionTypeData>(s->type);
    if (!fd) {
        return; // сигнатура неизвестна (обобщённая/шаблон) - кодоген эмитит вызов как есть
    }
    if (fd->paramTypes.size() != 2) {
        m_actx.ctx().diag().report(Severity::Error, rng, "@[matcher(\"{}\")]: predicate must take exactly 2 arguments (T_value, T_pattern), got {}", name,
                                   fd->paramTypes.size());
        return;
    }
    const TypeId rt = fd->returnType == INVALID_TYPE_ID ? INVALID_TYPE_ID : reg.getCanonicalTypeId(fd->returnType);
    const bool returnsBool = rt != INVALID_TYPE_ID && getGroup(getKindFromId(rt)) == Group::kLogical;
    if (!returnsBool) {
        m_actx.ctx().diag().report(Severity::Error, rng, "@[matcher(\"{}\")]: predicate must return bool (got '{}')", name,
                                   fd->returnType == INVALID_TYPE_ID ? "void" : std::string(reg.getFullTypeName(fd->returnType)));
    }
}

// -- Применение ортогональных квалификаторов типа (const + вид ссылки) --
// Единый источник для переменных (analyzeVarDecl) и параметров (declareFuncParams).
TypeId NameResolutionPass::applyRefAttrs(TypeId base, const AstNodeAttr& node, MapperRange range) {
    if (base == INVALID_TYPE_ID) {
        // Нетипизированное объявление (тип выводится из инициализатора): вид/D применить не к чему,
        // материализация ресурса невозможна. Это НЕ тихий пропуск - явная диагностика (AGENTS: без
        // fallback для невалидных данных): у нетипизированной переменной T ресурса неизвестен.
        const AttrPool& attrs0 = m_actx.ctx().attrs();
        if (const auto did0 = attrs0.lookup(attr::Deleter); did0.has_value() && node.has_attr(*did0)) {
            m_actx.ctx().diag().report(Severity::Error, range,
                                       "attribute 'deleter' requires an explicit type annotation (the resource value type cannot be inferred); "
                                       "use '<name> : @[reftype(...)@] @[deleter(D)@] <Type> := <handle>'");
        }
        return base;
    }
    const AttrPool& attrs = m_actx.ctx().attrs();
    // Константность ('^' → attr::ReadOnly): бит kConstFlag → `const T` в C++ (getCppTypeName).
    if (node.has_attr(attrs, attr::ReadOnly)) {
        base = setFlag(base, SymbolFlag::Const);
    }
    // Вид ссылки (@[reftype("ptr")]) - плоский enum RefType. Первая ссылка - fast-path бит,
    // вложенность - составной узел (единый источник: TypeRegistry::applyRefType).
    // Вид может быть задан ЛИБО у типа (`x : &Int32` → тип уже несёт признак), ЛИБО у переменной
    // (`& x : Int32` → атрибут reftype на узле переменной). Если задан у обоих - они ОБЯЗАНЫ
    // совпадать (иначе - ошибка); если только у переменной - применяем к pointee.
    auto reftype_id = attrs.lookup(attr::Reftype);
    if (reftype_id.has_value() && node.has_attr(*reftype_id)) {
        const std::vector<std::string>* rargs = node.attr_args(*reftype_id);
        if (!rargs || rargs->empty()) {
            m_actx.ctx().diag().report(Severity::Error, range, "attribute 'reftype' requires a reference-kind parameter, e.g. @[reftype(\"ptr\")]");
        } else {
            auto refkind = refKindFromAttrArgs(rargs);
            if (!refkind) {
                m_actx.ctx().diag().report(Severity::Error, range, "unknown reference kind '{}'", rargs->front());
            } else {
                // Расширенная форма: @[reftype("<kind>", <access_policy>[, <impl>])]. 2-й аргумент -
                // политика доступа (Group::kAccessPolicy; shared/weak/unique). 3-й (опциональный) -
                // класс реализации механизма доступа (поверх политики; только shared/weak). Пер-объектный
                // таймаут в атрибуте НЕ задаётся (настройка глобальная: дефолт + -fsync-deadlock= +
                // --trust:fsync-deadlock=); при необходимости - отдельный impl или конструктор.
                if (rargs->size() > 3) {
                    m_actx.ctx().diag().report(Severity::Error, range, "attribute 'reftype' accepts at most 3 arguments: kind[, access_policy[, impl]]");
                }
                // Политика - РЕАЛЬНЫЙ тип реестра (findType/isAccessPolicyType); сохраняется в ТИПЕ
                // (RefTypeData::accessPolicyType), а не выбрасывается. Диагностика - внутри хелпера.
                const TypeId accessPolicy = resolveAccessPolicy(m_actx, rargs, *refkind, range);
                // 3-й аргумент (опциональный) - класс реализации механизма доступа (поверх политики);
                // тоже ЧАСТЬ типа (RefTypeData::implType). Требует явной политики (валидация в хелпере).
                const TypeId impl = resolveImpl(m_actx, rargs, *refkind, accessPolicy, range);
                // Единая точка применения/проверки вида (semantic/ref_kind.hpp): совместим с типом ->
                // применяется к pointee-значению; иначе - mismatch-error (текст как был). Для shared/weak
                // с политикой/реализацией вид ВСЕГДА становится структурным узлом (policy/impl входят в тип).
                base =
                    applyDeclaredRefKind(m_actx, base, *refkind, range, /*subjectName=*/{}, /*actualLabel=*/"type", RefNameStyle::Mnemonic, accessPolicy, impl);
            }
        }
    }
    // Deleter внешнего ресурса (@[deleter(D)]): ровно один аргумент - имя deleter-ТИПА в
    // реестре (встроенный deleter-тип Group::kDeleterPolicy, напр. FreeDeleter, либо
    // пользовательский нативный класс). Допустим ТОЛЬКО на владеющих видах unique/shared.
    // Для unique D - ЧАСТЬ типа (пересобираем узел unique<T,D>); для shared D стирается
    // (тип остаётся Shared<T>), применяется кодогеном при создании/adopt.
    auto deleter_attr = attrs.lookup(attr::Deleter);
    if (deleter_attr.has_value() && node.has_attr(*deleter_attr)) {
        const std::vector<std::string>* dargs = node.attr_args(*deleter_attr);
        const RefType curKind = getRefType(getKindFromId(base));
        if (!dargs || dargs->empty()) {
            m_actx.ctx().diag().report(Severity::Error, range, "attribute 'deleter' requires a deleter type parameter, e.g. @[deleter(FreeDeleter)]");
        } else if (dargs->size() != 1) {
            m_actx.ctx().diag().report(Severity::Error, range, "attribute 'deleter' accepts exactly one argument: the deleter type name");
        } else if (checkQualifierApplicable(m_actx, attr::Deleter, curKind, range)) {
            TypeRegistry& reg = m_actx.ctx().types();
            const auto did = reg.findType(dargs->front());
            if (!did.has_value() || (!reg.isDeleterPolicyType(*did) && !reg.isNativeClassType(*did))) {
                m_actx.ctx().diag().report(Severity::Error, range,
                                           "unknown deleter type '{}' (expected a registered deleter type, e.g. FreeDeleter, or a native class)",
                                           dargs->front());
            } else if (curKind == RefType::kUnique) {
                // D входит в тип: пересобираем unique<T,D> (в т.ч. из fast-path бита unique<T>).
                base = reg.applyRefType(reg.getPointeeType(base), RefType::kUnique, *did);
            }
        }
    }
    // Lifetime-квалификатор (@[lifetime(<area>[, <name>])]): область жизни невладеющего view.
    // Пока проверяется только КОНТРАКТ (закрытый набор area + правила name) и применимость
    // (только Native-ось); инференс регионов/escape-правила - отдельный трек (types/REFType.md).
    if (const auto lt = attrs.lookup(attr::Lifetime); lt.has_value() && node.has_attr(*lt)) {
        validateLifetimeArgs(m_actx, node.attr_args(*lt), getRefType(getKindFromId(base)), range);
    }
    // pin: зарегистрирован как built-in, но pin-механика (запрет move/copy для самоссылающихся/
    // интрузивных типов) не реализована -> явная диагностика (матрица checkQualifierApplicable),
    // а не тихое принятие с ложной гарантией стабильности адреса.
    if (const auto pinid = attrs.lookup(attr::Pin); pinid.has_value() && node.has_attr(*pinid)) {
        checkQualifierApplicable(m_actx, attr::Pin, getRefType(getKindFromId(base)), range);
    }
    return base;
}

} // namespace trust
