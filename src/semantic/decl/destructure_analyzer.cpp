// src/semantic/decl/destructure_analyzer.cpp
// Анализ деструктуризации.
#include "types/type_read.hpp"
#include "semantic/core/type_predicates.hpp"
#include "semantic/decl/destructure_analyzer.hpp"
#include "semantic/core/local_name.hpp"
#include "semantic/core/name_resolution.hpp"
#include "semantic/typer/type_set.hpp"
#include "semantic/verify/format_check.hpp"
#include "semantic/typer/operator_check.hpp"
#include "semantic/typer/call_binding.hpp"
#include "semantic/solver.hpp"
#include "symbols/symbol_table.hpp"
#include "types/call.hpp"
#include "semantic/typer/type_inference.hpp"
#include "attrs/attr_builtin.hpp"
#include "ast/ref_syntax.hpp"
#include "ast/token.hpp"
#include "diag/diag.hpp"
#include "diag/options.hpp"
#include "diag/registry.hpp"
#include "semantic/diag.hpp"
#include "types/type_system.hpp"
#include "types/type_id.hpp"
#include "types/names.hpp"
#include "types/typekind.hpp"
#include "utils/strings.hpp"
#include "utils/utils.hpp"
#include <algorithm>
#include <format>
#include <string>
#include <unordered_set>

namespace trust {

// Общий подсчёт слотов-элементов и валидация rest-цели для деструктуризации (spread-словаря и
// кортежа). Единый источник идентичного цикла в analyzeDestructure / analyzeDestructureTuple.
bool DestructureAnalyzer::collectDestructureSlots(const DestructureDecl& node, size_t& elementSlots, bool& hasRest) {
    elementSlots = 0;
    hasRest = false;
    const size_t cnt = node.m_targets.size();
    for (size_t i = 0; i < cnt; ++i) {
        auto* t = node.m_targets[i].get();
        if (!t || t->kind() != ParserToken::Kind::Ident) {
            continue;
        }
        const bool isRest = i < node.m_targetIsRest.size() && node.m_targetIsRest[i];
        if (isRest) {
            hasRest = true;
            if (i + 1 != cnt) {
                m_actx.ctx().diag().report(Severity::Error, t->range(), "rest target '...' must be the last destructuring target");
                return false; // фатально: rest не последняя - цели не разбираем
            }
        } else {
            ++elementSlots;
        }
    }
    return true;
}

// Деструктуризация `t1, ..., tN := [... ]source;`.
// Без маркера - ТОЧНАЯ привязка (Python/Rust/Go/C++/Haskell): каждая цель - один элемент; для
// статически-известного размера число целей == числу элементов. Суффикс `...` у цели (`rest...`,
// C++-pack) - «остаток»: связывает оставшиеся элементы; `_...` - извлечь, остаток отбросить;
// одиночный `_` - пропустить ровно один элемент. Спред (`... source`, Dict) - цели извлекаются
// pop_front (точная привязка) / rest = остаток; кортеж (без `...`) - цели std::get<N> с проверкой арности.
void DestructureAnalyzer::analyzeDestructure(DestructureDecl& node) {
    if (node.m_source) {
        m_core.analyzeNode(node.m_source);
    }
    if (node.m_targets.empty()) {
        return;
    }
    // Кортеж (структурный источник, НЕ spread): цели = элементы по индексу.
    if (!node.m_isSpread) {
        const TypeId src = node.m_source ? m_actx.facts().exprType(*node.m_source) : INVALID_TYPE_ID;
        const bool isTuple = src != INVALID_TYPE_ID && m_actx.ctx().readTypes().store().getTypeDataAs<TupleTypeData>(src) != nullptr;
        if (isTuple) {
            analyzeDestructureTuple(node, src);
            return;
        }
        m_actx.ctx().diag().report(Severity::Error, node.m_source ? node.m_source->range() : node.range(),
                                   "destructuring requires a tuple source (or use '...' for a dictionary spread)");
        return;
    }
    // spread (коллекция Dict). Проверка типа источника: допустим только словарь (Dict) - иначе
    // pop_front на не-коллекции упал бы лишь на этапе C++-компиляции (тихий fallback в семантике).
    const TypeReadModel& reg = m_actx.ctx().types();
    const TypeId srcType = node.m_source ? m_actx.facts().exprType(*node.m_source) : INVALID_TYPE_ID;
    if (!isDictTypeId(reg, srcType)) {
        m_actx.ctx().diag().report(Severity::Error, node.m_source ? node.m_source->range() : node.range(),
                                   "spread destructuring source must be a dictionary (Dict), got a non-collection type");
        return;
    }
    // Тип источника известен семантике: lowering создаст временную-копию источника позже
    // (после семантики), и кодоген обязан эмитить конкретный тип, а не `auto`. Храним тип на узле
    // деструктуризации (сама временная появляется только в lowering).
    node.m_sourceType = QualType::fromTypeId(srcType).withoutInferred();
    const size_t cnt = node.m_targets.size();
    // Число целей-элементов (НЕ rest) и наличие rest-маркера (`rest...` / `_...`).
    size_t elementTargets = 0;
    bool hasRest = false;
    if (!collectDestructureSlots(node, elementTargets, hasRest)) {
        return; // rest не последняя - Error репортнут
    }
    // Статическая арность: без rest - elementTargets == размер (точная привязка); с rest -
    // elementTargets <= размер (остаток поглощается rest).
    const int64_t size = m_core.typer().dictSizeOf(node.m_source.get());
    if (size >= 0) {
        if (hasRest) {
            if (static_cast<int64_t>(elementTargets) > size) {
                m_actx.ctx().diag().report(Severity::Error, node.m_source->range(),
                                           "destructuring arity mismatch: {} element target(s) before rest but dictionary has {} element(s)", elementTargets,
                                           size);
                return;
            }
        } else if (static_cast<int64_t>(elementTargets) != size) {
            m_actx.ctx().diag().report(Severity::Error, node.m_source->range(),
                                       "destructuring arity mismatch: {} target(s) but dictionary has {} element(s); add '...' rest or adjust targets",
                                       elementTargets, size);
            return;
        }
    }
    // Типизация целей: per-element (как в кортеже) - каждая цель получает runtime-тип своего
    // элемента (Int8..Int64 → Int64, Float → Double, Bool, StrChar...). ВНУТРИ ЦИКЛА тип расширяется
    // до МАКСИМАЛЬНОГО среди элементов (Bool/Int8 → Integer, float → Double) - элемент перечитывается
    // и может меняться; Any - только если тип не выводим (внешний источник / несовместимые категории).
    node.m_targetTypes.assign(cnt, INVALID_TYPE_ID);
    node.m_targetDeclaredTypes.assign(cnt, INVALID_TYPE_ID);
    node.m_inLoop = m_actx.namespaces().isInLoop();
    const std::vector<QualType> elemTypes = m_core.typer().dictElementTypes(node.m_source.get());
    std::vector<TypeId> naturalized;
    naturalized.reserve(elemTypes.size());
    for (const QualType et : elemTypes) {
        naturalized.push_back(m_core.typer().naturalRuntimeType(et.getTypeId()));
    }
    const TypeId joined = node.m_inLoop ? m_core.typer().joinElementTypes(naturalized) : INVALID_TYPE_ID;
    bool untyped = elemTypes.empty();
    for (const QualType et : elemTypes) {
        if (et.isValid() && et.isInferred()) {
            untyped = true;
            break;
        }
    }
    if (node.m_inLoop && untyped) {
        if (joined != INVALID_TYPE_ID) {
            m_actx.ctx().report(node.range(), semantic::DiagId::WidenAny, "destructured element type is not specified; widened to the maximum element type");
        } else {
            m_actx.ctx().report(node.range(), semantic::DiagId::WidenAny, "destructured element type is not inferable; widened to the generic type (Any)");
        }
    }
    size_t elemIdx = 0;
    for (size_t i = 0; i < cnt; ++i) {
        auto* t = node.m_targets[i].get();
        if (!t || t->kind() != ParserToken::Kind::Ident) {
            continue;
        }
        const bool isRest = i < node.m_targetIsRest.size() && node.m_targetIsRest[i];
        if (node.m_isAssign) {
            // Присваивание в существующие цели: резолв + проверка const. Тип any_cast - natural
            // runtime тип ЭЛЕМЕНТА (соответствует хранению Dict), а не цель; значение присваивается
            // в существующую переменную с её собственным типом (C++-конверсия).
            // rest в присваивании: допустима только мутация-идиома (rest == источнику); прочее
            // переиспользование - Error (иначе кодген молча присвоил бы Dict в несовместимую цель).
            if (isRest && !restTargetNameAllowed(static_cast<IdentName&>(*t), /*isSpreadDict=*/true, node.m_source.get())) {
                continue;
            }
            node.m_targetTypes[i] = isRest ? INVALID_TYPE_ID : (elemIdx < naturalized.size() ? naturalized[elemIdx] : INVALID_TYPE_ID);
            assignDestructureTarget(node, i, static_cast<IdentName&>(*t), isRest);
        } else {
            // Объявление: m_targetTypes[i] - тип any_cast = natural runtime тип ЭЛЕМЕНТА (как хранит
            // Dict); declaredType - тип объявляемой переменной (явная аннотация `a:Int32` фиксирует
            // его, иначе - тот же выведенный). Разделение важно: Dict хранит int как int64_t, поэтому
            // any_cast<int32_t> по аннотации Int32 упал бы на элементе, хранимом как int64_t.
            if (isRest) {
                // Шаг 1: переиспользование имени rest-цели. Допустима только мутация-идиома
                // (rest == источнику); прочее переиспользование - Error (иначе кодген молча дал бы
                // C++-redefinition). При конфликте цель не связываем.
                if (!restTargetNameAllowed(static_cast<IdentName&>(*t), /*isSpreadDict=*/true, node.m_source.get())) {
                    continue;
                }
                // Шаг 2: аннотация типа на rest-цели запрещена - кодген фиксирует rest как Dict и
                // аннотацию молча игнорировал бы. Явная диагностика вместо тихого игнора.
                if (i < node.m_targetTypeNodes.size() && node.m_targetTypeNodes[i]) {
                    m_actx.ctx().diag().report(Severity::Error, t->range(), "type annotation on a rest target '{}...' is not supported; rest type is inferred",
                                               requiredText(*t));
                    continue;
                }
            }
            const TypeId inferred =
                isRest ? INVALID_TYPE_ID : (node.m_inLoop ? joined : (elemIdx < naturalized.size() ? naturalized[elemIdx] : INVALID_TYPE_ID));
            // Вне цикла тип цели не расширяется (per-element типизация): если тип конкретного
            // элемента не выводится (naturalRuntimeType → INVALID), цель молча становится std::any.
            // Симметрично цикловому предупреждению WidenAny - явная диагностика вместо тихого
            // fallback на Any (AGENTS rule 5 «no silent fallback»).
            if (!node.m_inLoop && !isRest && inferred == INVALID_TYPE_ID && requiredText(*t) != "_") {
                m_actx.ctx().report(t->range(), semantic::DiagId::WidenAny,
                                    "destructured element type for target '{}' is not inferable; widened to the generic type (Any)", requiredText(*t));
            }
            const TypeId declaredType = explicitTargetType(node, i, inferred);
            node.m_targetTypes[i] = inferred;                                        // any_cast<T> = как хранит Dict
            node.m_targetDeclaredTypes[i] = isRest ? INVALID_TYPE_ID : declaredType; // тип переменной
            declareDestructureTarget(static_cast<IdentName&>(*t), isRest, declaredType);
        }
        if (!isRest) {
            ++elemIdx; // и `_`, и именованная цель занимают один элемент (индекс)
        }
    }
}

void DestructureAnalyzer::analyzeDestructureTuple(DestructureDecl& node, TypeId tupleType) {
    const auto* td = m_actx.ctx().readTypes().store().getTypeDataAs<TupleTypeData>(tupleType);
    const size_t elemCount = td ? td->elements.size() : 0;
    node.m_sourceArity = elemCount; // для кортеж-rest в кодогенерации (скоуп сброшен к моменту codegen)
    const size_t cnt = node.m_targets.size();
    // Слоты-элементы (связывание + skip `_`) и наличие rest-цели (`rest...` / `_...`).
    size_t slots = 0;
    bool hasRest = false;
    if (!collectDestructureSlots(node, slots, hasRest)) {
        return; // rest не последняя - Error репортнут
    }
    // Арность: без rest - слоты == число элементов; с rest - слоты <= числа элементов (остаток).
    if (hasRest) {
        if (slots > elemCount) {
            m_actx.ctx().diag().report(Severity::Error, node.range(),
                                       "destructuring arity mismatch: {} element target(s) before rest but tuple has {} element(s)", slots, elemCount);
            return;
        }
    } else if (slots != elemCount) {
        m_actx.ctx().diag().report(Severity::Error, node.range(), "destructuring arity mismatch: {} target(s) but tuple has {} element(s)", slots, elemCount);
        return;
    }
    node.m_targetTypes.assign(cnt, INVALID_TYPE_ID);
    size_t idx = 0;
    for (size_t i = 0; i < cnt; ++i) {
        auto& t = node.m_targets[i];
        if (!t || t->kind() != ParserToken::Kind::Ident) {
            continue;
        }
        const bool isRest = i < node.m_targetIsRest.size() && node.m_targetIsRest[i];
        auto& h = static_cast<IdentName&>(*t);
        if (isNoneMarker(t.get()) && !isRest) {
            ++idx; // skip-элемент занимает индекс, но не связывается
            continue;
        }
        if (node.m_isAssign) {
            // Присваивание в существующие цели (std::get): резолв + проверка const.
            assignDestructureTarget(node, i, h, isRest);
            if (!isRest) {
                ++idx;
            }
            continue;
        }
        if (isRest) {
            // rest: `_...` - отброс (ничего не связываем); именованный `rest...` - остаток кортежа
            // как `std::tuple<Elem...>` (конкретный тип вычисляет семантика из оставшихся элементов).
            if (isNoneMarker(t.get())) {
                continue;
            }
            // Шаг 1: rest кортежа не может переиспользовать существующую переменную (в т.ч. сам
            // источник): кортежный rest - НЕ мутация (в отличие от spread-словаря), иначе кодген
            // молча дал бы переобъявление/UB.
            if (!restTargetNameAllowed(h, /*isSpreadDict=*/false, node.m_source.get())) {
                continue;
            }
            // Шаг 2: аннотация типа на rest-цели запрещена - тип rest выводится из оставшихся
            // элементов кортежа. Явная диагностика вместо тихого игнора.
            if (i < node.m_targetTypeNodes.size() && node.m_targetTypeNodes[i]) {
                m_actx.ctx().diag().report(Severity::Error, t->range(), "type annotation on a rest target '{}...' is not supported; rest type is inferred",
                                           requiredText(*t));
                continue;
            }
            std::vector<std::pair<std::string, QualType>> tail;
            if (td != nullptr) {
                for (size_t k = idx; k < td->elements.size(); ++k) {
                    tail.emplace_back(td->elements[k].name, td->elements[k].type);
                }
            }
            const TypeId restType = m_actx.ctx().types().interner().getOrCreateTupleType(std::move(tail));
            node.m_targetTypes[i] = restType;
            declareDestructureTarget(h, /*isRest=*/true, restType);
            continue;
        }
        const TypeId elemType = (td && idx < td->elements.size()) ? td->elements[idx].type.getTypeId() : INVALID_TYPE_ID;
        const TypeId explicitType = explicitTargetType(node, i, INVALID_TYPE_ID);
        // Без явной аннотации тип цели - конкретный тип элемента кортежа (известен семантике):
        // кодоген эмитит его явно, а не `auto`.
        const TypeId targetType = (explicitType != INVALID_TYPE_ID) ? explicitType : elemType;
        node.m_targetTypes[i] = targetType;
        declareDestructureTarget(h, /*isRest=*/false, targetType);
        ++idx;
    }
}

// Проверка переиспользования имени именованной rest-цели (`rest...`). Для словаря допустима
// мутация-идиома (rest-цель == самому источнику: pop'ы идут прямо в источник, объявление не
// создаётся - см. declareDestructureTarget). Переиспользование ЛЮБОЙ другой существующей
// переменной - Error: без этой проверки кодген молча сгенерировал бы C++-redefinition
// (`trust::Dict c_x = ...` поверх уже объявленного c_x) без диагностики. Для кортежа rest
// никогда не мутация, поэтому переиспользование (в т.ч. самого источника) всегда Error.
bool DestructureAnalyzer::restTargetNameAllowed(IdentName& t, bool isSpreadDict, const AstNodeBase* source) {
    const std::string name = canonicalTargetName(m_actx, t);
    if (name.empty() || name == "_") {
        return true;
    }
    if (!m_actx.symbols().resolve(name)) {
        return true; // имя свободно - можно объявлять заново
    }
    bool isSourceReuse = false;
    if (isSpreadDict && source && source->kind() == ParserToken::Kind::Ident) {
        isSourceReuse = (canonicalTargetName(m_actx, static_cast<const IdentName&>(*source)) == name);
    }
    if (isSourceReuse) {
        return true; // мутация-идиома `item, dict... := ... dict`
    }
    if (isSpreadDict) {
        m_actx.ctx().diag().report(Severity::Error, t.range(),
                                   "rest target '{}...' reuses an existing variable; only the source itself may be reused (mutation idiom)", name);
    } else {
        m_actx.ctx().diag().report(Severity::Error, t.range(),
                                   "tuple rest target '{}...' cannot reuse an existing variable; tuple rest is not a mutation (unlike dictionary spread)",
                                   name);
    }
    return false;
}

// Объявление одной цели деструктуризации. `_` - skip. isRest + уже объявленное имя (= источник)
// → «остаток» через мутацию pop_front, отдельного объявления нет.
void DestructureAnalyzer::declareDestructureTarget(IdentName& t, bool isRest, TypeId type) {
    std::string name{t.text()};
    if (name == "_") {
        return; // skip: элемент потребляется, переменная не создаётся
    }
    const MapperRange range = t.range();
    // Нормализация сигила (единый хелпер с analyzeVarDecl): bare-имя в локальном скоупе → $name.
    const bool isLocal = m_actx.namespaces().isInLocalScope();
    name = normalizeLocalSigil(m_actx, t, range, isLocal);
    // «Остаток» (isRest): если имя уже объявлено (== источник) - это мутация pop_front, не объявляем.
    if (isRest && m_actx.symbols().resolve(name)) {
        return;
    }
    Symbol sym;
    sym.name = stableSymbolName(name);
    sym.type = QualType::fromTypeId(
        (type != INVALID_TYPE_ID) ? type : (isRest ? m_actx.ctx().readTypes().getType(type::Dict) : m_actx.ctx().readTypes().getType(type_generic::Any)));
    sym.decl = &t;
    sym.storage = Storage::Local;
    const DeclResult target_decl = m_actx.symbols().declareOrComplete(sym);
    if (target_decl == DeclResult::Duplicate) {
        m_actx.ctx().diag().report(Severity::Error, range, "duplicate declaration '{}'", name);
        return;
    }
    if (target_decl == DeclResult::AlreadyRegistered) {
        return; // идемпотентная повторная регистрация (ромбовидный/повторный импорт)
    }
    for (auto& hook : m_core.hooks()) {
        hook->onDeclare(sym);
    }
}

// Явный тип цели из аннотации (`a:Int32`, node.m_targetTypeNodes[i]); INVALID - аннотации нет
// (возвращается fallback) или тип не резолвится (диагностируется).
TypeId DestructureAnalyzer::explicitTargetType(const DestructureDecl& node, size_t i, TypeId fallback) {
    if (i >= node.m_targetTypeNodes.size() || !node.m_targetTypeNodes[i]) {
        return fallback;
    }
    auto resolved = m_actx.types().resolveTypeRef(*node.m_targetTypeNodes[i]);
    if (!resolved) {
        m_actx.ctx().diag().report(Severity::Error, node.m_targetTypeNodes[i]->range(), "unknown type '{}'", requiredText(*node.m_targetTypeNodes[i]));
        return INVALID_TYPE_ID;
    }
    return *resolved;
}

// Цель деструктуризации-ПРИСВАИВАНИЯ (`a, b = ... source`): резолв существующей переменной,
// проверка на константность; объявление не создаётся. Тип any_cast задаёт вызывающий (цикл кладёт
// natural runtime тип элемента в node.m_targetTypes[i]). `_` - skip; rest == источник - мутация
// pop_front (присвоения нет).
void DestructureAnalyzer::assignDestructureTarget(DestructureDecl& node, size_t i, IdentName& t, bool isRest) {
    (void)node;
    (void)i;
    std::string name{t.text()};
    if (name == "_") {
        return; // skip: элемент потребляется, переменная не связывается
    }
    const MapperRange range = t.range();
    const bool isLocal = m_actx.namespaces().isInLocalScope();
    name = normalizeLocalSigil(m_actx, t, range, isLocal);
    // «Остаток» (isRest): если имя уже объявлено (== источник) - это мутация pop_front, присвоения нет.
    if (isRest) {
        if (Symbol* existing = m_actx.symbols().resolveMutable(name)) {
            auto& target = static_cast<IdentName&>(t);
            target.resolvedDecl = existing->decl;
            target.valueType = existing->type;
            return;
        }
    }
    Symbol* s = m_actx.symbols().resolveMutable(name);
    if (!s) {
        m_actx.ctx().diag().report(Severity::Error, range, "destructuring assignment target '{}' is not declared (use ':=' to create a variable)", name);
        return;
    }
    // Стабильная ссылка на место для пост-проходного definite-assignment (цель `=`).
    auto& target = static_cast<IdentName&>(t);
    target.resolvedDecl = s->decl;
    target.valueType = s->type;
    if (s->type.isConst()) {
        m_actx.ctx().diag().report(Severity::Error, range, "cannot assign to constant destructuring target '{}'", name);
        return;
    }
}

} // namespace trust
