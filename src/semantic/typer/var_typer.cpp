// Типизация объявлений переменных и лямбд (фасад - `ExprTyper`).
#include "types/type_read.hpp"
#include "semantic/typer/var_typer.hpp"
#include "semantic/typer/container_typer.hpp"
#include "semantic/core/name_resolution.hpp"
#include "semantic/decl/type_completeness.hpp"
#include "symbols/symbol_table.hpp"
#include "semantic/typer/type_inference.hpp"
#include "attrs/attr_builtin.hpp"
#include "ast/ref_syntax.hpp"
#include "semantic/ref/ref_kind.hpp"
#include "ast/token.hpp"
#include "diag/diag.hpp"
#include "diag/options.hpp"
#include "semantic/diag.hpp"
#include "types/call.hpp"
#include "types/type_system.hpp"
#include "types/type_id.hpp"
#include "types/names.hpp"
#include "utils/strings.hpp"
#include <format>
#include <string>

namespace trust {

void VarTyper::typeLambdaNode(FuncDecl& f) {
    if (f.isLambda()) {
        m_actx.facts().setExprType(&f, m_actx.types().buildFuncType(f));
    }
}

// Типизация объявлений переменных по трём формам: inferred / explicit / forward.
void VarTyper::typeVarDeclNode(VarDecl& v) {
    if (v.m_initializer) {
        if (v.m_type == nullptr) {
            typeInferredVarDecl(v);
        } else {
            typeExplicitVarDecl(v);
        }
    } else if (v.m_type == nullptr) {
        typeForwardVarDecl(v);
    }
    checkWithBindingCapture(v);
}

// Нетипизированная переменная (inferred): тип из типа инициализатора/последующих записей.
void VarTyper::requireInferredCompleteType(const VarDecl& v, QualType state) const {
    if (v.m_initializer == nullptr || !v.m_initializer->is<CallExpr>()) {
        return; // вывод не из вызова: объявления callee нет
    }
    const auto& call = static_cast<const CallExpr&>(*v.m_initializer);
    const AstNodeBase* calleeNode = call.m_callee.get();
    // `as<T>()` — FAULT на несовпадении kind, поэтому kind проверяется явно (callee бывает
    // строковым литералом интринсика).
    const auto* callee = (calleeNode != nullptr && calleeNode->kind() == ParserToken::Kind::Ident) ? calleeNode->as<IdentName>() : nullptr;
    const AstNodeBase* calleeDecl = (callee != nullptr) ? callee->resolvedDecl : nullptr;
    if (calleeDecl == nullptr || !calleeDecl->is<FuncDecl>()) {
        return; // метод/непрямой вызов: семантика не связывает объявление с callee
    }
    const FuncDecl& calleeFunc = *calleeDecl->as<FuncDecl>();
    if (!calleeFunc.m_type) {
        return; // тип результата без узла-аннотации проверять нечем
    }
    std::string name(v.text());
    if (!name.empty() && name.front() == '$') {
        name.erase(0, 1); // локальные имена нормализованы в '$name'; в диагностике — bare-имя
    }
    semantic::requireCompleteValueType(m_actx, calleeFunc.m_type.get(), state, v.range(), "variable '" + name + "'");
}

void VarTyper::typeInferredVarDecl(VarDecl& v) {
    QualType tstate = m_actx.facts().exprState(*v.m_initializer);
    TypeId t = tstate.getTypeId();
    if (t != INVALID_TYPE_ID) {
        // Ссылочная переменная без явного типа (`&& x := 5` / `&* u := 10` / `&? w := & x`):
        // вид ссылки - из reftype-атрибута (префиксный сигл перед именем), pointee выводится
        // из инициализатора. Если тип инициализатора УЖЕ ссылочный - он должен совпадать
        // с видом переменной (иначе - ошибка); если value - оборачиваем в вид переменной.
        const AttrPool& attrs = m_actx.ctx().attrs();
        if (const auto rid = attrs.lookup(attr::Reftype); rid.has_value() && v.has_attr(*rid)) {
            if (const auto* rargs = v.attr_args(*rid); rargs && !rargs->empty()) {
                if (const auto rk = refKindFromAttrArgs(rargs)) {
                    // Единая точка применения/проверки вида (semantic/ref/ref_kind.hpp):
                    // value -> оборачиваем; иначе - mismatch-error (текст как был).
                    // Политика синхронизации (2-й аргумент) - РЕАЛЬНЫЙ тип реестра,
                    // входит в тип (backend shared/weak). 3-й аргумент - реализация.
                    const TypeId accessPolicy = resolveAccessPolicy(m_actx.ctx(), rargs, *rk, v.range());
                    const TypeId impl = resolveImpl(m_actx.ctx(), rargs, *rk, accessPolicy, v.range());
                    tstate = applyDeclaredRefKind(m_actx.registry(), m_actx.ctx(), tstate, *rk, v.range(), v.text(), "deduced type", RefNameStyle::Full, accessPolicy, impl);
                    t = tstate.getTypeId();
                }
            }
        }
        // Тип с trust-условиями (пред/пост/утверждения) НЕ может быть выведен
        // автоматически - только явная аннотация типа (`x :MyInt := ...`). Признак -
        // бит trust в TypeId (isTrustedType). См. types/MEMORY.md.
        if (isTrustedType(t)) {
            const TypeReadModel& treg = m_actx.registry();
            const std::string tn = treg.printer().typeNameOf(t);
            m_actx.ctx().diag().report(
                Severity::Error, v.range(),
                "type '{}' carries trust conditions and cannot be auto-deduced; annotate the variable type explicitly (e.g. '{} :{} := ...')", tn, v.text(),
                tn);
            return;
        }
        // Копирование эксклюзивного владения (unique) запрещено: владелец единственный,
        // trust::Unique - move-only. Создание владельца ИЗ ЗНАЧЕНИЯ (`&* u := 10`) валидно
        // (обёртка, обработано выше); перенос между существующими владельцами - только
        // swap `a :=: b` / discard `a :=: _`.
        if (refAxisOf(t) == RefAxis::Unique) {
            const TypeId initType = m_actx.facts().exprType(*v.m_initializer);
            if (initType != INVALID_TYPE_ID && refAxisOf(initType) == RefAxis::Unique) {
                std::string varName(v.text());
                if (!varName.empty() && varName.front() == '$') {
                    varName.erase(0, 1);
                }
                m_actx.ctx().diag().report(Severity::Error, v.range(),
                                           "cannot initialize an exclusive (unique) reference '{}' from another exclusive reference; exclusive "
                                           "ownership is move-only - use swap 'a :=: b' or discard 'a :=: _'",
                                           varName);
                return;
            }
        }
        // Транспилятору нужен структурный тип (кодогенерация не различает inferred).
        v.inferredType = tstate.withoutInferred();
        requireInferredCompleteType(v, tstate);
        if (Symbol* s = m_actx.symbols().resolveMutable(v.text())) {
            s->type = tstate.withoutInferred();
            if (tstate.isInferred()) {
                s->type = s->type.withInferred();
            }
            if (v.has_attr(m_actx.ctx().attrs(), attr::ReadOnly)) {
                s->type = s->type.withConst();
            }
        }
    } else if (v.m_initializer->kind() != ParserToken::Kind::TypeName) {
        // Нетипизированная `x := _;` («объявить без значения», маркер `_`) - НЕ std::any
        // по умолчанию: тип выводится МОНОТОННО по последующим записям (первая запись
        // задаёт категорию; несовместимая последующая категория - Error, см. widenInferredTarget).
        // Чтение до записи проверяет CFG/dataflow-проход definite_assignment; VarDecl::inferredType заполняет
        // widenInferredTarget на первой записи (конкретный C++-тип, не std::any).
        if (isNoneMarker(v.m_initializer.get())) {
            if (Symbol* s = m_actx.symbols().resolveMutable(v.text()); s && s->storage != Storage::Local) {
                // Глобальная/модульная/статическая нетипизированная `:= _` (вне потока
                // функции): тип неоткуда вывести (нет единого «потока записей» скоупа
                // функции) - требуется явная аннотация. Ошибка блокирует конвейер до
                // кодогена; отдельного «cannot infer» не выдаём (см. finishUntypedUnderscoreDecls,
                // работающий только для Storage::Local).
                m_actx.ctx().diag().report(Severity::Error, v.range(),
                                           "non-local '{} := _' requires an explicit type annotation (e.g. '{} :Any := _'); "
                                           "the type cannot be inferred from assignments outside a function body",
                                           v.text(), v.text());
            } else {
                // Локальная `x := _;`: отложенный вывод. НЕ ставим Any. Символ несёт
                // Inferred на INVALID-структуре (тип заполнит первая запись); чтение до записи
                // проверяет отдельный CFG/dataflow-проход (semantic/definite_assignment).
                if (s) {
                    s->type = QualType::fromTypeId(INVALID_TYPE_ID);
                    if (v.has_attr(m_actx.ctx().attrs(), attr::ReadOnly)) {
                        s->type = s->type.withConst();
                    }
                    s->type = s->type.withInferred();
                }
                // v.inferredType остаётся INVALID; заполнит widenInferredTarget на первой
                // записи (транспилятор читает VarDecl::inferredType).
            }
        } else if (auto aid = m_actx.ctx().readTypes().findType(type_generic::Any)) {
            // Инициализатор без выводимого типа (C++-вставка `{% %}`, вызов с
            // неизвестным результатом, отрицательный литерал) - переменная по природе
            // std::any. Маркируем тип ЯВНО (Any), чтобы транспилятор НЕ угадывал тихим
            // fallback на std::any (AGENTS rule 5): INVALID у переменной с инициализатором
            // в кодогенерации - ошибка вывода. Голый `:T` сюда не попадает - это
            // невалидная запись `x := :Int32` (диагностируется в analyzeVarDecl).
            v.inferredType = QualType::fromTypeId(*aid);
            if (Symbol* s = m_actx.symbols().resolveMutable(v.text())) {
                s->type = QualType::fromTypeId(*aid);
                if (v.has_attr(m_actx.ctx().attrs(), attr::ReadOnly)) {
                    s->type = s->type.withConst();
                }
            }
        }
    }
}

// Явно-типизированная переменная: проверка сужения инициализатора в целевой тип.
void VarTyper::typeExplicitVarDecl(VarDecl& v) {
    // Явно-типизированная: проверить сужение инициализатора в целевой тип.
    auto targetOpt = m_actx.types().resolveTypeRef(*v.m_type);
    if (targetOpt.has_value()) {
        // Абстрактная контейнерная аннотация (`:Tuple`/`:Range`/`:Array` без const/ref) не имеет
        // единого C++-имени: конкретный структурный тип выводится из инициализатора (как dtype
        // тензора ниже). Фиксируем его в inferredType - кодоген эмитит конкретное имя, а не `auto`.
        if (v.m_initializer && typeRefOf(m_actx.ctx().readTypes().store().desugaredTypeId(*targetOpt)) == RefType::kValue) {
            TypeSystem& treg = m_actx.registry();
            const TypeId targetCanon = treg.read().store().desugaredTypeId(*targetOpt);
            const TypeId initType = m_actx.facts().exprType(*v.m_initializer);
            const bool concreteMatch =
                (targetCanon == treg.read().getType(type_category::Tuple) && initType != INVALID_TYPE_ID &&
                 treg.read().store().isTypeDataKind(initType, TypeDataKind::kTuple)) ||
                (targetCanon == treg.read().getType(type_category::Range) && initType != INVALID_TYPE_ID && treg.read().store().isRangeType(initType)) ||
                (targetCanon == treg.read().getType(type::Array) && initType != INVALID_TYPE_ID && treg.read().store().isArrayType(initType));
            if (concreteMatch) {
                v.inferredType = QualType::fromTypeId(initType).withoutInferred();
            }
        }
        // Литерал массива `[1,2,3,]` в типизированную Array-цель (`vector<Int32>`):
        // коэрция элемента к типу элемента цели (`std::vector<int32_t>{1,2,3}`), иначе
        // узкая разрядность литерала (int8) не сконвертируется в целевую.
        if (v.m_initializer && v.m_initializer->kind() == ParserToken::Kind::ArrayInit) {
            auto& init = static_cast<DictLiteralNode&>(*v.m_initializer);
            m_container.coerceArrayInitToTarget(init, *targetOpt);
            // `:Tensor[dims]` без dtype: конкретный тип выведен из литерала - фиксируем его в
            // символе, иначе объявление осталось бы Tensor<Any> (кодоген не может его эмитить).
            if (init.tensorType.getTypeId() != INVALID_TYPE_ID) {
                v.inferredType = init.tensorType;
                if (Symbol* ts = m_actx.symbols().resolveMutable(v.text())) {
                    ts->type = init.tensorType;
                }
            }
        }
        // `:Tensor[dims]` без dtype и инициализатор-НЕ-литерал (`:= existingTensor`): dtype
        // выводится из типа выражения; форма (если известна в обоих) обязана совпадать.
        {
            TypeSystem& reg = m_actx.registry();
            const TypeId target = *targetOpt;
            const bool targetNeedsDtype =
                reg.read().store().isTensorType(target) && reg.read().store().tensorElementType(target) == reg.read().getType(type_generic::Auto);
            if (targetNeedsDtype && v.m_initializer && v.m_initializer->kind() != ParserToken::Kind::ArrayInit) {
                const TypeId initType = m_actx.facts().exprType(*v.m_initializer);
                if (!reg.read().store().isTensorType(initType)) {
                    m_actx.ctx().diag().report(Severity::Error, v.m_initializer->range(),
                                               "cannot infer the tensor element type for '{}'; annotate the initializer", v.text());
                } else {
                    const auto& targetDims = reg.read().store().tensorDimensions(target);
                    const auto& initDims = reg.read().store().tensorDimensions(initType);
                    if (!targetDims.empty() && !initDims.empty() && targetDims != initDims) {
                        m_actx.ctx().diag().report(Severity::Error, v.m_initializer->range(), "tensor initializer shape does not match the declared shape");
                    } else {
                        v.inferredType = QualType::fromTypeId(initType);
                        if (Symbol* ts = m_actx.symbols().resolveMutable(v.text())) {
                            ts->type = QualType::fromTypeId(initType);
                        }
                    }
                }
            }
        }
        // Полный целевой тип С УЧЁТОМ reftype: resolveTypeRef материализует квалификаторы аннотации
        // в IdentType::resolvedState, а analyzeVarDecl применяет квалификаторы узла к Symbol::type.
        // Для контракта value-vs-reference важен полный тип (Symbol::type) - иначе цель-слабая
        // ссылка выглядела бы value-типом и копирование ссылки ошибочно запрещалось.
        TypeId targetFull = *targetOpt;
        if (Symbol* ts = m_actx.symbols().resolveMutable(v.text())) {
            if (ts->type.getTypeId() != INVALID_TYPE_ID) {
                targetFull = ts->type.getTypeId();
            }
        }
        // Владеющий ресурс с deleter (@[deleter(D)]): инициализатор - СЫРОЙ handle
        // (нативный указатель), захватываемый через adopt, а не значение/ссылка
        // совместимого вида. Смешение нативный×умный здесь законно (НЕ копирование
        // ссылки): семантика уже провалидировала вид/D в applyRefAttrs (см. REFType.md §9.2).
        // Атрибут может стоять и на переменной, и на аннотации типа.
        const AttrPool& dattrs = m_actx.ctx().attrs();
        const AstNodeAttr* typeAttr = v.m_type ? v.m_type->as_attr() : nullptr;
        const bool adopt_handle = v.has_attr(dattrs, attr::Deleter) || (typeAttr != nullptr && typeAttr->has_attr(dattrs, attr::Deleter));
        if (!adopt_handle) {
            checkAssignmentNarrowing(v.m_initializer.get(), m_actx.facts().exprType(*v.m_initializer), targetFull, v.text());
        }
    }
}

// Нетипизированное forward-объявление (`x := ...;`): по природе std::any.
void VarTyper::typeForwardVarDecl(VarDecl& v) {
    // Нетипизированное forward-объявление (`x := ...;`): и инициализатора, и типа нет -
    // по природе std::any. Маркируем тип ЯВНО (Any), как и для тип-less инициализаторов,
    // чтобы транспилятор единообразно эмитил `emitTypeName(inferred)` без ветки угадывания.
    if (auto aid = m_actx.ctx().readTypes().findType(type_generic::Any)) {
        v.inferredType = QualType::fromTypeId(*aid);
        if (Symbol* s = m_actx.symbols().resolveMutable(v.text())) {
            s->type = QualType::fromTypeId(*aid);
            if (v.has_attr(m_actx.ctx().attrs(), attr::ReadOnly)) {
                s->type = s->type.withConst();
            }
        }
    }
}

// Биндинг оператора `with`: ссылочный тип (Shared/Weak) без захвата `*`/`*^` - предупреждение.
void VarTyper::checkWithBindingCapture(const VarDecl& v) {
    if (v.m_inWith && v.m_initializer && v.m_initializer->kind() != ParserToken::Kind::RefTakeExpr) {
        const TypeId it = m_actx.facts().exprType(*v.m_initializer);
        if (it != INVALID_TYPE_ID) {
            const RefType rt = typeRefOf(it);
            if (rt == RefType::kShared || rt == RefType::kWeak) {
                // -Wwith-ref-without-capture (default: warning; управляется -W/-Wno).
                const Severity sev = m_actx.ctx().opts().get(semantic::DiagId::WithRefWithoutCapture);
                if (sev != Severity::Ignore) {
                    auto* entry =
                        m_actx.ctx().diag().report(sev, v.range(), semantic::DiagId::WithRefWithoutCapture,
                                                   "with binding '{}' copies a reference without capture; use '*{}' to lock access", v.text(), v.text());
                    // Fix-it: обернуть инициализатор в '*': `with(v = *ref)` (для простых ссылок).
                    m_actx.ctx().diag().fixit(entry, v.m_initializer->range(), "*" + std::string(requiredText(*v.m_initializer)));
                }
            }
        }
    }
}

} // namespace trust
