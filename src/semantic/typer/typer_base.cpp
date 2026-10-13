// src/semantic/typer/typer_base.cpp
// Общая база типизаторов выражений: сервисы драйвера и проверки присваивания.
#include "types/type_read.hpp"
#include "semantic/typer/literal_annotation.hpp"
#include "semantic/typer/typer_base.hpp"
#include "semantic/core/name_resolution.hpp"
#include "semantic/core/pass.hpp"
#include "semantic/typer/type_inference.hpp"
#include "symbols/symbol_table.hpp"
#include "ast/token.hpp"
#include "diag/diag.hpp"
#include "types/group.hpp"
#include "types/call.hpp"
#include "types/type_system.hpp"
#include "types/type_id.hpp"
#include "types/names.hpp"
#include "types/typekind.hpp"
#include <format>
#include <string>

namespace trust {

TyperBase::TyperBase(AnalysisContext& actx, NameResolutionPass& core)
: m_actx(actx)
, m_core(core) {
}

void TyperBase::widenInferredTarget(const AstNodeBase* lhs, TypeId result) {
    if (!lhs || lhs->kind() != ParserToken::Kind::Ident || result == INVALID_TYPE_ID) {
        return;
    }
    Symbol* s = m_actx.symbols().resolveMutable(requiredText(*lhs));
    if (!s || !s->type.isInferred()) {
        return;
    }
    // Нетипизированная `x := _;` (deferred-вывод по записям): первая запись задаёт категорию типа.
    // Последующая запись НЕСОВМЕСТИМОЙ категории (напр. число потом строка) не может быть выведена
    // монотонно → явная Error с требованием аннотации `x:Any := _` (никакого тихого std::any-fallback).
    // Совместимые категории (числовые промоции / одинаковый тип) ведут тип как у обычного inferred.
    if (s->decl && s->decl->kind() == ParserToken::Kind::VarDecl) {
        const auto* vd = static_cast<const VarDecl*>(s->decl);
        if (!vd->m_type && vd->m_initializer && isNoneMarker(vd->m_initializer.get())) {
            const TypeId cur = s->type.getTypeId();
            const TypeId nw = getUnqualifiedType(result);
            if (cur != INVALID_TYPE_ID && nw != INVALID_TYPE_ID && cur != nw) {
                const TypeReadModel& reg = m_actx.ctx().types();
                const TypeId cc = reg.store().desugaredTypeId(cur);
                const TypeId nc = reg.store().desugaredTypeId(nw);
                const bool compatible = (cc == nc) || (isArithmeticGroup(getGroup(getTypeKind(cc))) && isArithmeticGroup(getGroup(getTypeKind(nc))));
                if (!compatible) {
                    std::string disp{requiredText(*lhs)};
                    if (!disp.empty() && disp.front() == '$') {
                        disp.erase(0, 1); // DSL-сигил: показываем имя без служебного '$'
                    }
                    m_actx.ctx().diag().report(Severity::Error, lhs->range(),
                                               "cannot assign type '{}' to '{}' whose type was inferred as '{}' from an earlier assignment to the untyped "
                                               "'{} := _' declaration; use explicit '{} :Any := _' to allow values of different types",
                                               reg.printer().getTypeName(nc), disp, reg.printer().getTypeName(cc), disp, disp);
                    return; // тип категории не перетираем; ошибка останавливает конвейер
                }
            }
        }
    }
    // Живой type-state расширяется; ранее установленный const сохраняется, inferred взводится.
    const bool wasConst = s->type.isConst();
    s->type = QualType::fromTypeId(result).withInferred();
    if (wasConst) {
        s->type = s->type.withConst();
    }
    // Обновить выведенный тип на узле объявления (VarDecl), чтобы декларация использовала
    // финальный join после сброса скоуп-стека (транспилятор читает VarDecl::inferredType).
    if (s->decl && s->decl->kind() == ParserToken::Kind::VarDecl) {
        static_cast<VarDecl*>(s->decl)->inferredType = QualType::fromTypeId(result).withoutInferred();
    }
}

void TyperBase::checkAssignmentNarrowing(const AstNodeBase* valueNode, TypeId sourceType, TypeId targetType, std::string_view targetName) {
    (void)targetName;
    if (valueNode == nullptr || sourceType == INVALID_TYPE_ID || targetType == INVALID_TYPE_ID) {
        return;
    }
    const TypeReadModel& reg = m_actx.ctx().types();
    const TypeId sc = reg.store().desugaredTypeId(sourceType);
    const TypeId dc = reg.store().desugaredTypeId(targetType);
    const TypeKind sKind = getTypeKind(sc);
    const TypeKind dKind = getTypeKind(dc);
    const Group sg = getGroup(sKind);
    const Group dg = getGroup(dKind);
    // Контракт value-vs-reference (см. types/type/REFType.md «двухосевая модель»): источник или цель -
    // ссылочный тип. Правила:
    //   * цель-ссылка + источник-значение: создание владеющей обёртки (Shared/Unique) - ВАЛИДНО
    //     (`x : &Int32 := 5`); Weak из значения невозможен (нужна существующая shared-ссылка);
    //   * источник-ссылка + цель-значение: копирование ссылки в значение - нарушение контракта
    //     (диагностика + fixit `*<name>` - локер);
    //   * обе ссылки: оси владения (shared/weak vs unique) и pointee-типы обязаны совпадать.
    const RefType srt = getRefType(sKind);
    const RefType drt = getRefType(dKind);
    const bool sRef = srt != RefType::kValue;
    const bool dRef = drt != RefType::kValue;
    if (sRef || dRef) {
        std::string srcName(reg.printer().getTypeName(sc));
        std::string dstName(reg.printer().getTypeName(dc));
        if (dRef && !sRef) {
            if (drt == RefType::kWeak) {
                m_actx.ctx().diag().report(
                    Severity::Error, valueNode->range(),
                    "cannot initialize a weak reference from a value '{}'; weak requires an existing shared reference (use '& <shared_var>')", srcName);
            }
            return; // shared/unique из значения - валидно (владеющая обёртка), числовое сужение неприменимо
        }
        if (sRef && !dRef) {
            auto* entry = m_actx.ctx().diag().report(Severity::Error, valueNode->range(),
                                                     "cannot copy a reference '{}' into a value variable '{}'; use '*{}' (locker) to access the value", srcName,
                                                     targetName, requiredText(*valueNode));
            if (entry != nullptr && !valueNode->range().isInvalid()) {
                m_actx.ctx().diag().fixit(entry, valueNode->range(), "*" + std::string(requiredText(*valueNode)));
            }
            return;
        }
        // Обе ссылки: запрещено смешение нативных (сырых) и умных (shared/weak/unique) ссылок
        // (D8): нельзя положить нативную ссылку в умную и наоборот - разная идеология владения.
        // Классификатор вида - единый (isNativeRefKind, types/typekind.hpp).
        if (isNativeRefKind(srt) != isNativeRefKind(drt) && (isNativeRefKind(srt) || isNativeRefKind(drt))) {
            m_actx.ctx().diag().report(Severity::Error, valueNode->range(), "cannot mix native and smart references: cannot assign '{}' to '{}'", srcName,
                                       dstName);
            return;
        }
        // Совпадение осей ссылки (двухосевая модель, единая классификация refAxisOf):
        // shared/weak vs unique - разные оси владения; native (сырой) и locker - отдельные оси.
        if (refAxisOf(srt) != refAxisOf(drt)) {
            m_actx.ctx().diag().report(Severity::Error, valueNode->range(),
                                       "reference ownership mismatch: cannot assign '{}' to '{}' (shared/weak and unique are different ownership axes)",
                                       srcName, dstName);
            return;
        }
        // Эксклюзивное владение (unique) НЕ копируется: владелец единственный, trust::Unique -
        // move-only. Создание из значения (`unique := value`) допустимо (обработано выше);
        // перенос владения - только swap `a :=: b` / discard `a :=: _`.
        if (refAxisOf(srt) == RefAxis::Unique) {
            m_actx.ctx().diag().report(
                Severity::Error, valueNode->range(),
                "cannot copy an exclusive (unique) reference '{}' into '{}'; exclusive ownership is move-only - use swap 'a :=: b' or discard 'a :=: _'",
                srcName, dstName);
            return;
        }
        // И совпадение pointee-типов (канонические).
        const TypeId spo = reg.store().getPointeeType(sc);
        const TypeId dpo = reg.store().getPointeeType(dc);
        if (spo != INVALID_TYPE_ID && dpo != INVALID_TYPE_ID && reg.store().desugaredTypeId(spo) != reg.store().desugaredTypeId(dpo)) {
            m_actx.ctx().diag().report(Severity::Error, valueNode->range(), "reference pointee mismatch: cannot assign '{}' to '{}'", srcName, dstName);
        }
        return;
    }
    // Сверхразрядный целочисленный литерал (BigInteger-источник): валиден как BigInteger.
    // В фиксированную целую цель (`x:Int64 := <huge>`) не влезает → ошибка переполнения.
    const TypeId bi = reg.store().desugaredTypeId(reg.getType(type::BigInteger));
    if (sc == bi && valueNode->kind() == ParserToken::Kind::IntLiteral) {
        const Literal& intLit = static_cast<const Literal&>(*valueNode);
        // Влезает ли текст литерала в знаковый Int64 (единый предикат intFitsTarget, Int64-вид).
        const TypeKind int64Kind = getTypeKind(reg.store().desugaredTypeId(reg.getType(type::Int64)));
        if (!intFitsTarget(intLit.text(), int64Kind)) {
            // Цель BigInteger - ок (литерал остаётся BigInteger); фикс. целое/беззнаковое - ошибка.
            if (dg == Group::kIntegers || dg == Group::kUnsigned) {
                m_actx.ctx().diag().report(Severity::Error, valueNode->range(),
                                           "integer literal '{}' exceeds the range of Integer (Int64); use BigInteger or a Double (floating point)",
                                           intLit.text());
            }
            return;
        }
    }
    // Строки: сужение StrWide (широкая) → StrChar (узкая) - всегда ошибка (значение литерала
    // не влияет: любой '…' уже широкий; безопасного сужения и строкового cast нет).
    if (sg == Group::kStrWide && dg == Group::kStrChar) {
        std::string srcName(reg.printer().getTypeName(sc));
        m_actx.ctx().diag().report(Severity::Error, valueNode->range(),
                                   "value of type '{}' cannot be narrowed to 'StrChar' (wide string into narrow); use double-quoted \"…\" for a narrow string",
                                   srcName);
        return;
    }
    // Проверка только для целых групп одной категории: сужение по ширине.
    const bool sNum = (sg == Group::kIntegers || sg == Group::kUnsigned);
    const bool dNum = (dg == Group::kIntegers || dg == Group::kUnsigned);
    if (!sNum || !dNum || sg != dg) {
        return;
    }
    const uint8_t sw = getData(sKind);
    const uint8_t dw = getData(dKind);
    if (sw <= dw) {
        return; // не сужение (шире или тот же размер)
    }
    // Литерал, влезающий в целевой тип → безопасное сужение (без диагностики).
    if (valueNode->kind() == ParserToken::Kind::IntLiteral && intFitsTarget(requiredText(*valueNode), dKind)) {
        return;
    }
    // Сужение (переменная/неизвестное шире цели) → ошибка + fixit «use cast :Type(expr)».
    std::string dstName(reg.printer().getTypeName(dc));
    std::string srcName(reg.printer().getTypeName(sc));
    auto* entry = m_actx.ctx().diag().report(Severity::Error, valueNode->range(), "value of type '{}' cannot be narrowed to '{}' (use cast :{}(expr))", srcName,
                                             dstName, dstName);
    if (entry != nullptr && !valueNode->range().isInvalid()) {
        std::string replacement = std::format(":{}({})", dstName, requiredText(*valueNode));
        m_actx.ctx().diag().fixit(entry, valueNode->range(), replacement);
    }
}

} // namespace trust
