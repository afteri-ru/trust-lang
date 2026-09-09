// src/semantic/expr_node_typer.cpp
// Типизация узлов-выражений ExprTyper-а по семействам (вынесено из expr_typer.cpp как
// отдельная зона ответственности): Filling, бинарные/append, лямбда, VarDecl (inferred/
// explicit/forward + with-биндинг), литералы и вызовы. Публичный API класса не меняется -
// методы объявлены в include/semantic/expr_typer.hpp.
#include "semantic/expr_typer.hpp"
#include "semantic/name_resolution.hpp"
#include "semantic/analysis_common.hpp"
#include "semantic/ellipsis.hpp"
#include "semantic/format_check.hpp"
#include "semantic/solver.hpp"
#include "analysis/symbol_table.hpp"
#include "semantic/overload_call.hpp"
#include "types/overload_resolve.hpp"
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

namespace {

// ЕДИНСТВЕННЫЙ репортёр невалидной постфиксной аннотации литерала `literal :Type`. Вызывается
// только из авторитетного прохода typeExpr; провизионные читатели (dictElementType/exprType)
// решают тип через annotatedLiteralType, но ошибку НЕ дублируют. ann - резолвленная аннотация
// (nullopt/INVALID ⇒ «unknown type», тексты сообщений берутся из решателя/литерала).
void reportLiteralAnnotProblem(trust::Context& ctx, const Literal& lit, std::optional<TypeId> ann, const TypeRegistry& reg, LiteralAnnotProblem problem) {
    switch (problem) {
    case LiteralAnnotProblem::UnknownType:
        ctx.diag().report(Severity::Error, lit.typeAnnotation->range(), "unknown type annotation '{}' on a literal", lit.typeAnnotation->text());
        break;
    case LiteralAnnotProblem::RationalExpected:
        ctx.diag().report(Severity::Error, lit.range(), "a rational literal 'num\\den' must be typed 'Rational' (found '{}')",
                          reg.getFullTypeName(reg.getCanonicalTypeId(*ann)));
        break;
    case LiteralAnnotProblem::BoolNot01:
        ctx.diag().report(Severity::Error, lit.range(), "boolean literal must be 0 or 1 (found '{}')", lit.text());
        break;
    case LiteralAnnotProblem::IntOverflow:
        ctx.diag().report(Severity::Error, lit.range(), "integer literal '{}' does not fit the annotated type '{}'", lit.text(),
                          reg.getFullTypeName(reg.getCanonicalTypeId(*ann)));
        break;
    case LiteralAnnotProblem::NonNumeric:
        ctx.diag().report(Severity::Error, lit.range(), "cannot annotate a numeric literal with non-numeric type '{}'",
                          reg.getFullTypeName(reg.getCanonicalTypeId(*ann)));
        break;
    case LiteralAnnotProblem::None:
        break;
    }
}

} // namespace

// -- Обработчики семейств узлов-выражений (вызываются из typeExpr) --

// Типизация `... expr ...` (Filling) как элемента списка: тип = тип операнда (нужен для
// вывода элементного типа массива). Расширение списка/capacity - в analyzeArrayInit/
// analyzeCallFilling; самостоятельным выражением FILLING не является.
void ExprTyper::typeFillingNode(const Sequence& f) {
    if (!f.m_body.empty() && f.m_body[0]) {
        const TypeId t = m_actx.exprType(*f.m_body[0]);
        if (t != INVALID_TYPE_ID) {
            m_actx.setExprType(&f, t);
        }
    }
}

// AppendStmt (`X []= v`) - append к контейнеру, не обычное бинарное выражение: типы ложатся
// специально (lhsType=тип контейнера, rhsType/resultType=тип значения), сужение/расширение
// целевой переменной не применяются (append не меняет тип цели).
void ExprTyper::typeAppendStmt(Binary& b) {
    const TypeId lt = b.m_left ? m_actx.exprType(*b.m_left) : INVALID_TYPE_ID;
    const TypeId rt = b.m_right ? m_actx.exprType(*b.m_right) : INVALID_TYPE_ID;
    // Spread-merge `X []= ... dict`: правая часть - маркер распаковки (Ellipsis),
    // единичным элементом НЕ является, тип результата не выводится (INVALID).
    const bool spread = b.m_right && b.m_right->kind() == ParserToken::Kind::Ellipsis;
    b.lhsType = lt;
    b.rhsType = spread ? INVALID_TYPE_ID : rt;
    b.resultType = spread ? INVALID_TYPE_ID : rt;
    b.commonType = spread ? INVALID_TYPE_ID : rt;
    // Строковые контейнеры: ширина RHS должна быть совместима с единичным элементом
    // контейнера (StrChar ↔ std::string, StrWide ↔ std::wstring). Потерянное сужение
    // (широкая строка в узкий контейнер) - ошибка; обратное (char→wide) кодогенерация
    // сама расширяет узкий литерал в wide.
    const TypeRegistry& reg = m_actx.ctx().types();
    const TypeId strChar = reg.getType(type::StrChar);
    const TypeId strWide = reg.getType(type::StrWide);
    const TypeId ltC = (lt != INVALID_TYPE_ID) ? reg.getCanonicalTypeId(lt) : INVALID_TYPE_ID;
    const TypeId rtC = (rt != INVALID_TYPE_ID) ? reg.getCanonicalTypeId(rt) : INVALID_TYPE_ID;
    if (spread) {
        // Распаковка `[]= ...` допустима только для контейнера-словаря (merge/extend).
        if (ltC != INVALID_TYPE_ID && ltC != reg.getType(type::Dict)) {
            m_actx.ctx().diag().report(Severity::Error, b.range(), "spread append '[]= ...' is only supported for a dictionary container");
        }
        // Вложенный LHS (`d['x'] []= ...`, `d[0] []= ...`, `d.field []= ...`) - отложено.
        if (b.m_left && (b.m_left->kind() == ParserToken::Kind::ArrayAccess || b.m_left->kind() == ParserToken::Kind::MemberAccess)) {
            m_actx.ctx().diag().report(Severity::Error, b.m_left->range(),
                                       "вложенный append '[]=' пока не реализован: append допустим только к простому контейнеру");
        }
        m_actx.setExprType(&b, INVALID_TYPE_ID);
        return;
    }

    if (ltC == strChar && rtC == strWide) {
        m_actx.ctx().diag().report(
            Severity::Error, b.range(),
            "append '[]=': wide string cannot be appended to a narrow string container; use matching quotes (narrow '...' or wide \"...\")");
    }
    // Вложенный LHS (`d['x'] []= v`, `d[0] []= v`, `d.field []= v`) - отложено.
    if (b.m_left && (b.m_left->kind() == ParserToken::Kind::ArrayAccess || b.m_left->kind() == ParserToken::Kind::MemberAccess)) {
        m_actx.ctx().diag().report(Severity::Error, b.m_left->range(),
                                   "вложенный append '[]=' пока не реализован: append допустим только к простому контейнеру");
    }
    m_actx.setExprType(&b, rt);
    return;
}

// Типизация бинарных выражений (арифметика/сравнение/присваивание) без ветки AppendStmt.
void ExprTyper::typeBinaryNode(Binary& b) {
    // AppendStmt (`X []= v`) - append к контейнеру, отдельная ветка.
    if (b.kind() == ParserToken::Kind::AppendStmt) {
        typeAppendStmt(b);
        return;
    }
    const TypeId result = typeBinaryResult(b);
    const bool isAssignOp = (b.kind() == ParserToken::Kind::AssignOp);
    // Составное присваивание ("+=", "//=" - диапазон AddAssign..XorAssign по BinaryOp);
    // простые операторы ("//", "+") не расширяют целевую переменную. Оператор - по m_op,
    // НЕ по text() (см. include/ast/MEMORY.md).
    const bool compound = isCompoundAssignOp(b.m_op);
    // Сужение в ЯВНО-типизированную цель (inferred-цели расширяются ниже).
    if (b.m_left && b.m_left->kind() == ParserToken::Kind::Ident) {
        if (Symbol* s = m_actx.symbols().resolveMutable(b.m_left->text())) {
            // Конструктор record-шаблона в RHS присваивания (`b = Box()`): тип-цель = тип LHS.
            if (isPlainAssignOp(b.m_op)) {
                m_actx.coerceRecordTemplateCtor(b.m_right.get(), s->type);
            }
            // Ссылочная ЦЕЛЬ проверяется ВСЕГДА (в т.ч. inferred): контракт value-vs-reference и
            // запрет копирования unique не зависят от «выведенности» типа. Числовые inferred-цели -
            // как раньше (их расширяет widenInferredTarget). Swap `:=:` - НЕ присваивание
            // (обмен/перемещение владения), проверяется отдельно в ветке swap.
            const bool targetIsRef = s->type != INVALID_TYPE_ID && refAxisOf(getRefType(getKindFromId(s->type))) != RefAxis::Value;
            if (!isSwapOp(b.m_op) && (!testFlag(s->type, SymbolFlag::Inferred) || (isPlainAssignOp(b.m_op) && targetIsRef)) && (isAssignOp || compound)) {
                const TypeId assigned = isPlainAssignOp(b.m_op) ? b.rhsType : result;
                checkAssignmentNarrowing(b.m_right.get(), assigned, s->type, s->name);
            }
        }
    }
    // Константность (kConstFlag): запись в константную переменную - ошибка; LHS с `^`
    // (attr::ReadOnly на узле Ident) - финальная запись, делающая переменную константой
    // (became-const: `x := 42; x^ += 1;` → x неизменяема со значением 44).
    if (b.m_left && b.m_left->kind() == ParserToken::Kind::Ident && (isAssignOp || compound)) {
        if (Symbol* s = m_actx.symbols().resolveMutable(b.m_left->text())) {
            const bool makeConst = b.m_left->as_attr() && b.m_left->as_attr()->has_attr(m_actx.ctx().attrs(), attr::ReadOnly);
            if (testFlag(s->type, SymbolFlag::Const)) {
                if (!makeConst) {
                    m_actx.ctx().diag().report(Severity::Error, b.range(), "cannot assign to constant variable '{}'", s->name);
                }
            } else if (makeConst) {
                // Финальная запись `x^ = ...`: переменная становится константной (бит
                // kConstFlag на Symbol::type). Декларация при этом остаётся не-const (см.
                // transpiler::generateVarDeclToFile - const объявления берётся из атрибута узла).
                s->type = setFlag(s->type, SymbolFlag::Const);
            }
            // Признак инициализации - ортогональный бит kUninitFlag на Symbol::type (как
            // пер-переменная константность; НЕ то же, что m_initializer). `_` здесь -
            // ФОРМАЛЬНЫЙ инициализатор (`x = _;`), который СБРАСЫВАЕТ «инициализирована»
            // (значение более не определено); обычная запись значения (`x = <expr>` /
            // `x += …`) признак снимает.
            s->type = isNoneMarker(b.m_right.get()) ? setFlag(s->type, SymbolFlag::Uninit) : clearFlag(s->type, SymbolFlag::Uninit);
        }
    }
    // Расширение выводимой цели по истории присвоений - только для присваиваний
    // (AssignOp "=", "+=" или составной MathOp "+=").
    if (isAssignOp || compound) {
        TypeId widen = result;
        // Автоматически выведенный Bool (неТипизированная переменная, напр.
        // `b := 1 :Bool; b += 1;`), используемый в составной числовой арифметике,
        // расширяется до максимального Int (Int64): Bool - вырожденное целое, а в
        // однопроходной типизации нет «оператора в цикле», поэтому расширяем по самому
        // факту составного присваивания. Явный `:Bool` (в т.ч. из голого `1`/`0`, которые
        // теперь Int8) так НЕ расширяется - для него это ошибка (явный тип фиксирован).
        if (!isPlainAssignOp(b.m_op) && b.m_left && b.m_left->kind() == ParserToken::Kind::Ident) {
            if (Symbol* s = m_actx.symbols().resolveMutable(b.m_left->text())) {
                const TypeRegistry& reg = m_actx.ctx().types();
                if (s->type != INVALID_TYPE_ID && reg.getCanonicalTypeId(s->type) == reg.getType(type::Bool)) {
                    if (testFlag(s->type, SymbolFlag::Inferred)) {
                        widen = reg.getType(type::Int64);
                    } else {
                        m_actx.ctx().diag().report(Severity::Error, b.range(),
                                                   "explicitly typed Bool cannot be widened to Int64 by numeric compound assignment '{}'; "
                                                   "use an inferred int variable or an explicit integer type",
                                                   b.text());
                    }
                }
            }
        }
        widenInferredTarget(b.m_left.get(), widen);
    }
}

// Лямбда-выражение как значение: тип - функциональный (returnType + типы параметров).
void ExprTyper::typeLambdaNode(FuncDecl& f) {
    if (f.isLambda()) {
        m_actx.setExprType(&f, m_actx.buildFuncType(f));
    }
}

// Типизация объявлений переменных по трём формам: inferred / explicit / forward.
void ExprTyper::typeVarDeclNode(VarDecl& v) {
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
void ExprTyper::typeInferredVarDecl(VarDecl& v) {
    TypeId t = m_actx.exprType(*v.m_initializer);
    if (t != INVALID_TYPE_ID) {
        // Ссылочная переменная без явного типа (`&& x := 5` / `&* u := 10` / `&? w := & x`):
        // вид ссылки - из reftype-атрибута (префиксный сигл перед именем), pointee выводится
        // из инициализатора. Если тип инициализатора УЖЕ ссылочный - он должен совпадать
        // с видом переменной (иначе - ошибка); если value - оборачиваем в вид переменной.
        const AttrPool& attrs = m_actx.ctx().attrs();
        if (const auto rid = attrs.lookup(attr::Reftype); rid.has_value() && v.has_attr(*rid)) {
            if (const auto* rargs = v.attr_args(*rid); rargs && !rargs->empty()) {
                if (const auto rk = refKindFromAttrArgs(rargs)) {
                    // Единая точка применения/проверки вида (semantic/ref_kind.hpp):
                    // value -> оборачиваем; иначе - mismatch-error (текст как был).
                    // Политика синхронизации (2-й аргумент) - РЕАЛЬНЫЙ тип реестра,
                    // входит в тип (backend shared/weak). 3-й аргумент - реализация.
                    const TypeId accessPolicy = resolveAccessPolicy(m_actx, rargs, *rk, v.range());
                    const TypeId impl = resolveImpl(m_actx, rargs, *rk, accessPolicy, v.range());
                    t = applyDeclaredRefKind(m_actx, t, *rk, v.range(), v.text(), "deduced type", RefNameStyle::Full, accessPolicy, impl);
                }
            }
        }
        // Тип с trust-условиями (пред/пост/утверждения) НЕ может быть выведен
        // автоматически - только явная аннотация типа (`x :MyInt := ...`). Признак -
        // бит trust в TypeId (typeIsTrusted). См. types/MEMORY.md.
        if (typeIsTrusted(t)) {
            const TypeRegistry& treg = m_actx.ctx().types();
            const std::string tn = treg.getFullTypeName(t);
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
        if (refAxisOf(getRefType(getKindFromId(t))) == RefAxis::Unique) {
            const TypeId initType = m_actx.exprType(*v.m_initializer);
            if (initType != INVALID_TYPE_ID && refAxisOf(getRefType(getKindFromId(initType))) == RefAxis::Unique) {
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
        v.inferredType = clearFlag(t, SymbolFlag::Inferred);
        if (Symbol* s = m_actx.symbols().resolveMutable(v.text())) {
            // Живой тип символа несёт бит «выведен» (для join/продвижения) и, при
            // константности ('^' → attr::ReadOnly), бит «константность» (kConstFlag) -
            // источник префикса `const ` в кодогенерации переменной.
            s->type = v.has_attr(m_actx.ctx().attrs(), attr::ReadOnly) ? setFlag(t, SymbolFlag::Const) : t;
        }
    } else if (v.m_initializer->kind() != ParserToken::Kind::TypeName) {
        // Нетипизированная `x := _;` («объявить без значения», маркер `_`) - НЕ std::any
        // по умолчанию: тип выводится МОНОТОННО по последующим записям (первая запись
        // задаёт категорию; несовместимая последующая категория - Error, см. widenInferredTarget).
        // Чтение до записи уже Error через Uninit-бит; VarDecl::inferredType заполняет
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
                // Inferred+Uninit на INVALID-структуре (тип заполнит первая запись);
                // чтение до записи даст Error «read before it is initialized».
                if (s) {
                    TypeId st = INVALID_TYPE_ID;
                    if (v.has_attr(m_actx.ctx().attrs(), attr::ReadOnly)) {
                        st = setFlag(st, SymbolFlag::Const);
                    }
                    st = setFlag(st, SymbolFlag::Inferred);
                    st = setFlag(st, SymbolFlag::Uninit);
                    s->type = st;
                }
                // v.inferredType остаётся INVALID; заполнит widenInferredTarget на первой
                // записи (транспилятор читает VarDecl::inferredType).
            }
        } else if (auto aid = m_actx.ctx().types().findType(type_generic::Any)) {
            // Инициализатор без выводимого типа (C++-вставка `{% %}`, вызов с
            // неизвестным результатом, отрицательный литерал) - переменная по природе
            // std::any. Маркируем тип ЯВНО (Any), чтобы транспилятор НЕ угадывал тихим
            // fallback на std::any (AGENTS rule 5): INVALID у переменной с инициализатором
            // в кодогенерации - ошибка вывода. Голый `:T` сюда не попадает - это
            // невалидная запись `x := :Int32` (диагностируется в analyzeVarDecl).
            v.inferredType = *aid;
            if (Symbol* s = m_actx.symbols().resolveMutable(v.text())) {
                s->type = v.has_attr(m_actx.ctx().attrs(), attr::ReadOnly) ? setFlag(*aid, SymbolFlag::Const) : *aid;
            }
        }
    }
}

// Явно-типизированная переменная: проверка сужения инициализатора в целевой тип.
void ExprTyper::typeExplicitVarDecl(VarDecl& v) {
    // Явно-типизированная: проверить сужение инициализатора в целевой тип.
    auto targetOpt = m_actx.resolveTypeRef(*v.m_type);
    if (targetOpt.has_value()) {
        // Литерал массива `[1,2,3,]` в типизированную Array-цель (`vector<Int32>`):
        // коэрция элемента к типу элемента цели (`std::vector<int32_t>{1,2,3}`), иначе
        // узкая разрядность литерала (int8) не сконвертируется в целевую.
        if (v.m_initializer && v.m_initializer->kind() == ParserToken::Kind::ArrayInit) {
            coerceArrayInitToTarget(static_cast<DictLiteralNode&>(*v.m_initializer), *targetOpt);
        }
        // Полный целевой тип С УЧЁТОМ reftype-атрибута (`@[reftype(...)@]` применяется в
        // analyzeVarDecl к Symbol::type, а resolveTypeRef(*m_type) даёт только базовый тип без
        // reftype). Для контракта value-vs-reference важен полный тип (Symbol::type) -
        // иначе цель-слабая ссылка выглядела бы value-типом и копирование ссылки
        // ошибочно запрещалось. Символический сигл (`:&? Int32`) reftype уже несёт.
        TypeId targetFull = *targetOpt;
        if (Symbol* ts = m_actx.symbols().resolveMutable(v.text())) {
            if (ts->type != INVALID_TYPE_ID) {
                targetFull = ts->type;
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
            checkAssignmentNarrowing(v.m_initializer.get(), m_actx.exprType(*v.m_initializer), targetFull, v.text());
        }
    }
}

// Нетипизированное forward-объявление (`x := ...;`): по природе std::any.
void ExprTyper::typeForwardVarDecl(VarDecl& v) {
    // Нетипизированное forward-объявление (`x := ...;`): и инициализатора, и типа нет -
    // по природе std::any. Маркируем тип ЯВНО (Any), как и для тип-less инициализаторов,
    // чтобы транспилятор единообразно эмитил `emitTypeName(inferred)` без ветки угадывания.
    if (auto aid = m_actx.ctx().types().findType(type_generic::Any)) {
        v.inferredType = *aid;
        if (Symbol* s = m_actx.symbols().resolveMutable(v.text())) {
            s->type = v.has_attr(m_actx.ctx().attrs(), attr::ReadOnly) ? setFlag(*aid, SymbolFlag::Const) : *aid;
        }
    }
}

// Биндинг оператора `with`: ссылочный тип (Shared/Weak) без захвата `*`/`*^` - предупреждение.
void ExprTyper::checkWithBindingCapture(const VarDecl& v) {
    if (v.m_inWith && v.m_initializer && v.m_initializer->kind() != ParserToken::Kind::RefTakeExpr) {
        const TypeId it = m_actx.exprType(*v.m_initializer);
        if (it != INVALID_TYPE_ID) {
            const RefType rt = getRefType(getKindFromId(it));
            if (rt == RefType::kShared || rt == RefType::kWeak) {
                // -Wwith-ref-without-capture (default: warning; управляется -W/-Wno).
                const Severity sev = m_actx.ctx().opts().get(semantic::DiagId::WithRefWithoutCapture);
                if (sev != Severity::Ignore) {
                    auto* entry =
                        m_actx.ctx().diag().report(sev, v.range(), semantic::DiagId::WithRefWithoutCapture,
                                                   "with binding '{}' copies a reference without capture; use '*{}' to lock access", v.text(), v.text());
                    // Fix-it: обернуть инициализатор в '*': `with(v = *ref)` (для простых ссылок).
                    m_actx.ctx().diag().fixit(entry, v.m_initializer->range(), "*" + std::string(v.m_initializer->text()));
                }
            }
        }
    }
}

// Типизация литерала: постфиксная аннотация `literal :Type` либо вывод по виду/тексту.
void ExprTyper::typeLiteralNode(Literal& lit) {
    const TypeRegistry& reg = m_actx.ctx().types();
    // Литерал: тип задаёт постфиксная аннотация `literal :Type` (0 :Bool, 5 :Rational,
    // 5 :BigInteger, 256 :Int16, 1.5 :Float32; RationalLiteral `num\den` - только :Rational)
    // либо выводится по виду/тексту (literalType). Единые правила и единственный репортёр
    // ошибок - annotatedLiteralType / reportLiteralAnnotProblem.
    if (lit.typeAnnotation) {
        const auto ann = m_actx.resolveTypeRef(*lit.typeAnnotation);
        const LiteralAnnotResult r = annotatedLiteralType(lit, ann, reg);
        if (r.problem == LiteralAnnotProblem::None) {
            lit.typeId = setFlag(r.type, SymbolFlag::Inferred);
            m_actx.setExprType(&lit, lit.typeId);
        } else {
            // Единственный репортёр невалидной аннотации (typeExpr - авторитетный проход);
            // кешируем INVALID, чтобы exprType не возвращал текст-тип/не пересчитывал.
            reportLiteralAnnotProblem(m_actx.ctx(), lit, ann, reg, r.problem);
            m_actx.setExprType(&lit, INVALID_TYPE_ID);
        }
        return;
    }
    // Кешируем выведенный тип (literalType), чтобы exprType не пересчитывал его; тип с
    // битом inferred (для join/расширения; см. typeBinaryResult). Транспилятор литерала
    // словаря читает lit.typeId, не пересчитывая диапазоны.
    const TypeId t = literalType(lit, reg);
    if (t != INVALID_TYPE_ID) {
        const TypeId vt = setFlag(t, SymbolFlag::Inferred);
        lit.typeId = vt;
        m_actx.setExprType(&lit, vt);
    }
    return;
}

// Типизация вызова: проверка формата, результат функции/лямбды, чтение аргументов, многоточие.
void ExprTyper::typeCallNode(CallExpr& call) {
    // Строка-формат `"{}"(args)` / `'{}'(args)`: callee - строковый литерал. Результат -
    // строка той же ширины (StrWide/StrChar), как у литерала. + компиляйт-тайм проверка.
    if (call.m_callee && (call.m_callee->kind() == ParserToken::Kind::StrWide || call.m_callee->kind() == ParserToken::Kind::StrChar)) {
        const bool wide = call.m_callee->kind() == ParserToken::Kind::StrWide;
        const TypeId t = m_actx.ctx().types().getType(wide ? type::StrWide : type::StrChar);
        if (t != INVALID_TYPE_ID) {
            m_actx.setExprType(&call, setFlag(t, SymbolFlag::Inferred));
        }
        checkFormatStringArgs(call);
        return;
    }
    // printf-формат (атрибут @[format]) - проверка типов аргументов (пост-порядково).
    checkFormatArgs(call);
    // Обычный вызов пользовательской функции: типизируем результат возвращаемым типом
    // сигнатуры (как handleMethodCall для методов). Это чинит `p := f(10)` → int32_t
    // (ранее результат вызова был std::any). Метод-вызов obj.method(args) обрабатывается
    // отдельно (analyzeAccess/handleMethodCall). Если тип не резолвится - остаётся Any.
    if (call.m_callee && call.m_callee->kind() == ParserToken::Kind::Ident) {
        if (const Symbol* s = m_core.resolveSimple(call.m_callee.get(), call.m_callee->text())) {
            const FuncDecl* fdecl = (s->decl && s->decl->is<FuncDecl>()) ? s->decl->as<FuncDecl>() : nullptr;
            // Нативная функция (`%name` / импорт-алиас `name := %native` / нативный шаблон-тип):
            // перегрузку и конверсии аргументов разрешает C++-слой - типы заданы явно нативными,
            // имя символа фиксировано внешней библиотекой (не манглируется).
            const bool nativeFunc =
                fdecl != nullptr && (fdecl->m_isNativeImport || fdecl->m_isNativeTemplateCtor || (!s->name.empty() && s->name.front() == '%'));
            if (fdecl != nullptr && !nativeFunc) {
                // TrustLang-функция: перегрузку разрешает АНАЛИЗАТОР ВСЕГДА (в т.ч. при единственной
                // сигнатуре) - выбор сигнатуры, тип результата и допустимость аргументов определяет
                // компилятор, а не C++-слой. Набор из одной сигнатуры - вырожденный случай.
                std::vector<TypeId> sigs = s->overloads.empty() ? std::vector<TypeId>{structuralType(s->type)} : s->overloads;
                const EllipsisInfo einfo = scanCallEllipsis(call);
                if (einfo.form != EllipsisForm::None) {
                    // Многоточие в аргументах: позиции заполняются/повторяются - разрешать по типам
                    // аргументов нельзя. Допустимо только при ЕДИНСТВЕННОЙ сигнатуре; материализацию
                    // списка выполняет analyzeCallFilling (по resolvedSignature ниже).
                    if (sigs.size() == 1) {
                        call.resolvedSignature = sigs.front();
                        if (const auto* fd = m_actx.ctx().types().getTypeDataAs<FunctionTypeData>(sigs.front())) {
                            m_actx.setExprType(&call, fd->returnType);
                        }
                    } else {
                        m_actx.ctx().diag().report(Severity::Error, call.range(), "function '{}' is overloaded; ellipsis in arguments is not supported",
                                                   call.m_callee->text());
                        discardEllipsisElements(*call.m_args, false);
                        m_actx.setExprType(&call, INVALID_TYPE_ID);
                    }
                } else {
                    const std::vector<TypeId> argTypes = callArgTypes(m_actx, call);
                    const TypeId chosen =
                        resolveCallOverload(m_actx, sigs, argTypes, call.range(), std::format("function '{}'", call.m_callee->text()));
                    if (chosen != INVALID_TYPE_ID) {
                        call.resolvedSignature = chosen;
                        if (sigs.size() > 1) {
                            // Уникальное C++-имя перегрузки (кодоген не полагается на C++-разрешение).
                            call.resolvedCalleeSuffix = overloadCppSuffix(m_actx.ctx().types(), chosen);
                        }
                        if (const auto* fd = m_actx.ctx().types().getTypeDataAs<FunctionTypeData>(chosen)) {
                            m_actx.setExprType(&call, fd->returnType);
                        }
                    } else {
                        m_actx.setExprType(&call, INVALID_TYPE_ID);
                    }
                }
            } else if (const auto* fd = m_actx.ctx().types().getTypeDataAs<FunctionTypeData>(s->type)) {
                // Нативная функция ИЛИ вызов ЗНАЧЕНИЯ функционального типа (переменная-лямбда):
                // результат - returnType (перегрузку/конверсии нативного - C++).
                m_actx.setExprType(&call, fd->returnType);
            }
        }
    }
    // Немедленный вызов лямбды `( lambda )(args)`: callee - не идентификатор, а лямбда-выражение.
    // Тип результата - returnType функционального типа лямбды (типизирована пост-порядково).
    if (call.m_callee && call.m_callee->kind() == ParserToken::Kind::FuncDecl) {
        const TypeId ct = m_actx.exprType(*call.m_callee);
        if (const auto* fd = m_actx.ctx().types().getTypeDataAs<FunctionTypeData>(ct)) {
            m_actx.setExprType(&call, fd->returnType);
        }
    }
    // Вызов ЗНАЧЕНИЯ record/native-класса с объявленным оператором `()`: результат = его
    // returnType, арность/многоточие проверяются как у метода. Без `()` - явная ошибка
    // (не откладываем на C++). Не перехватываем: (1) именованные функции/значения
    // функционального типа (обработаны выше), (2) вызов-КОНСТРУКТОР по имени типа (`MyStr(...)`).
    if (call.m_callee && call.m_callee->kind() == ParserToken::Kind::Ident) {
        TypeRegistry& reg = m_actx.ctx().types();
        const Symbol* calleeSym = m_core.resolveSimple(call.m_callee.get(), call.m_callee->text());
        const bool isTypeName = reg.findType(call.m_callee->text()).has_value();
        const bool isNamedCallable = calleeSym != nullptr && ((calleeSym->decl != nullptr && calleeSym->decl->kind() == ParserToken::Kind::FuncDecl) ||
                                                              reg.getTypeDataAs<FunctionTypeData>(calleeSym->type) != nullptr);
        const TypeId ct = (calleeSym != nullptr) ? structuralType(calleeSym->type) : INVALID_TYPE_ID;
        const TypeId ctc = (ct != INVALID_TYPE_ID) ? reg.getCanonicalTypeId(ct) : INVALID_TYPE_ID;
        const bool userLike = ctc != INVALID_TYPE_ID && (reg.isRecordType(ctc) || reg.isNativeClassType(ctc));
        if (userLike && !isNamedCallable && !isTypeName) {
            const auto opInfo = reg.findMethodInfo(ctc, "()");
            if (!opInfo) {
                m_actx.ctx().diag().report(Severity::Error, call.range(), "type '{}' has no operator '()'", reg.getFullTypeName(ctc));
                m_actx.setExprType(&call, INVALID_TYPE_ID);
                return;
            }
            // `()`: НАТИВНЫЙ оператор - C++-путь; TrustLang - резолвер ВСЕГДА. Многоточие допустимо
            // только при единственной сигнатуре (позиции не разрешаются по типам аргументов).
            const bool nativeCallOp = utils::is_native_name(opInfo->key);
            const bool hasEllipsisOp = scanCallEllipsis(call).form != EllipsisForm::None;
            TypeId opFuncType = INVALID_TYPE_ID;
            if (opInfo->signatures.size() == 1 && (nativeCallOp || hasEllipsisOp)) {
                opFuncType = opInfo->signatures.front();
            } else {
                const std::vector<TypeId> argTypes = callArgTypes(m_actx, call);
                const OverloadResolution r = resolveOverload(reg, opInfo->signatures, argTypes);
                if (r.chosen == INVALID_TYPE_ID) {
                    // Единственная сигнатура: сохраняем точную диагностику арности.
                    const auto* only = (opInfo->signatures.size() == 1) ? reg.getTypeDataAs<FunctionTypeData>(opInfo->signatures.front()) : nullptr;
                    if (only != nullptr && argTypes.size() != only->paramTypes.size()) {
                        m_actx.ctx().diag().report(Severity::Error, call.range(), "operator '()' of type '{}' expects {} argument(s), got {}",
                                                   reg.getFullTypeName(ctc), only->paramTypes.size(), argTypes.size());
                    } else {
                        m_actx.ctx().diag().report(Severity::Error, call.range(), "no matching overload for operator '()' of type '{}'", reg.getFullTypeName(ctc));
                    }
                    m_actx.setExprType(&call, INVALID_TYPE_ID);
                    return;
                }
                opFuncType = r.chosen;
            }
            const auto* fd = reg.getTypeDataAs<FunctionTypeData>(opFuncType);
            EXPECT(fd != nullptr && "call-operator: '()' must have a function type");
            const EllipsisInfo einfo = scanCallEllipsis(call);
            const size_t nargs = call.m_args ? call.m_args->size() : 0;
            if (einfo.form != EllipsisForm::None) {
                if (!validateEllipsis(einfo, call, m_actx, "аргументы вызова")) {
                    discardEllipsisElements(*call.m_args, false);
                    m_actx.setExprType(&call, INVALID_TYPE_ID);
                    return;
                }
                if (!expandCallEllipsis(call, m_actx, einfo, fd->paramTypes, fd->variadicType != INVALID_TYPE_ID)) {
                    m_actx.setExprType(&call, INVALID_TYPE_ID);
                    return;
                }
            } else if (opInfo->signatures.size() == 1 && nargs != fd->paramTypes.size()) {
                m_actx.ctx().diag().report(Severity::Error, call.range(), "operator '()' of type '{}' expects {} argument(s), got {}", reg.getFullTypeName(ctc),
                                           fd->paramTypes.size(), nargs);
                m_actx.setExprType(&call, INVALID_TYPE_ID);
                return;
            }
            m_actx.setExprType(&call, fd->returnType);
        }
    }
    // Чтение значения в АРГУМЕНТЕ вызова: вариативные/слабо-типизированные цели (напр. `@print`)
    // не проверяют типы аргументов, поэтому чтение неинициализированной переменной-аргумента не
    // пришло бы через типизацию аргумента. Принудительно резолвим каждый аргумент-Ident как
    // value-read (репорт-дедуп на узле внутри exprType). Составные аргументы уже прочитаны
    // при своей типизации (вложенные операнды) — здесь только верхний слой-Ident.
    if (call.m_args) {
        for (const auto& a : *call.m_args) {
            if (a && a->kind() == ParserToken::Kind::Ident) {
                const TypeId _argRead = m_actx.exprType(*a);
                (void)_argRead; // [[nodiscard]]; результат не нужен — важна проверка чтения
            }
        }
    }
    // Многоточие `f(a, ... expr ...)` / `f(a, ...)`: структурные правила + разворот списка
    // аргументов ЕДИНЫМ примитивом (semantic/ellipsis) - кодоген многоточия не видит.
    analyzeCallFilling(call);
    return;
}

} // namespace trust
