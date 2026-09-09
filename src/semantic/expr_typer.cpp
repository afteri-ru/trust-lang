// Generated: src/semantic/expr_typer.cpp
#include "semantic/expr_typer.hpp"
#include "semantic/name_resolution.hpp"
#include "semantic/analysis_common.hpp"
#include "semantic/format_check.hpp"
#include "semantic/solver.hpp"
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

// -- Типизация выражений (post-order): диспетчеризация по семействам --
// Реализация обработчиков семейств - в expr_node_typer.cpp (отдельная зона ответственности).
void ExprTyper::typeExpr(AstNodeBase* node) {
    if (!node) {
        return;
    }
    if (node->kind() == ParserToken::Kind::Filling) {
        typeFillingNode(static_cast<const Sequence&>(*node));
        return;
    }
    if (is_binary_expr_kind(node->kind())) {
        typeBinaryNode(static_cast<Binary&>(*node));
        return;
    }
    if (node->is<FuncDecl>()) {
        typeLambdaNode(*node->as<FuncDecl>());
        return;
    }
    if (node->is<VarDecl>()) {
        typeVarDeclNode(*node->as<VarDecl>());
        return;
    }
    if (is_literal_kind(node->kind())) {
        typeLiteralNode(*node->as<Literal>());
        return;
    }
    if (node->is<CallExpr>()) {
        typeCallNode(*node->as<CallExpr>());
        return;
    }
    // Ссылочные выражения (address-of/borrow, take, нативные операторы) - отдельная зона
    // ответственности, реализована в ref_expr_typer.cpp (метод typeRefExpr).
    if (node->is<RefMakeExpr>() || node->is<RefTakeExpr>()) {
        typeRefExpr(node);
    }
}


// -- Компиляйт-тайм проверка printf-формата (атрибут @[format("printf", ...)]) --
void ExprTyper::checkFormatArgs(CallExpr& call) {
    if (!call.m_callee || call.m_callee->kind() != ParserToken::Kind::Ident) {
        return;
    }
    const Symbol* sym = m_core.resolveSimple(call.m_callee.get(), call.m_callee->text());
    if (!sym || !sym->decl || sym->decl->kind() != ParserToken::Kind::FuncDecl) {
        return;
    }
    const auto& f = static_cast<const FuncDecl&>(*sym->decl);
    const AttrPool& attrs = m_actx.ctx().attrs();
    auto fmt_id = attrs.lookup(attr::Format);
    if (!fmt_id.has_value() || !f.has_attr(*fmt_id)) {
        return;
    }
    const std::vector<std::string>* fargs = f.attr_args(*fmt_id);
    // @[format("printf", string_index, first_to_check)] - ровно три параметра.
    if (!fargs || fargs->size() != 3 || fargs->at(0) != attr::FormatPrintf) {
        return; // поддерживается только printf-архетип; параметры валидирует matches_params
    }
    int stringIdx = 0;
    int firstToCheck = 0;
    try {
        stringIdx = std::stoi(fargs->at(1));
        firstToCheck = std::stoi(fargs->at(2));
    } catch (...) {
        return;
    }
    if (stringIdx < 1 || firstToCheck < 1 || !call.m_args || static_cast<int>(call.m_args->size()) < stringIdx) {
        return;
    }
    // Формат-строка - аргумент stringIdx-1 (индексы 1-based); обязана быть строковым литералом.
    const auto& fmtArg = (*call.m_args)[stringIdx - 1];
    if (!fmtArg || fmtArg->kind() != ParserToken::Kind::StrChar) {
        m_actx.ctx().report(fmtArg ? fmtArg->range() : call.range(), semantic::DiagId::Format, "format string is not a string literal");
        return;
    }
    const std::string fmt(fmtArg->text());
    std::vector<format_check::Conversion> convs;
    if (!format_check::parse_printf_format(fmt, convs)) {
        m_actx.ctx().report(fmtArg->range(), semantic::DiagId::Format, "invalid printf format string '{}'", fmt);
        return;
    }
    const TypeRegistry& reg = m_actx.ctx().types();
    for (std::size_t j = 0; j < convs.size(); ++j) {
        const int argPos = firstToCheck - 1 + static_cast<int>(j);
        if (argPos >= static_cast<int>(call.m_args->size())) {
            m_actx.ctx().report(call.range(), semantic::DiagId::Format,
                                "format string requires more arguments than provided (missing argument for conversion '%{}')", std::string(1, convs[j].conv));
            return;
        }
        const auto& arg = (*call.m_args)[argPos];
        if (!arg) {
            continue;
        }
        const TypeId argType = m_actx.exprType(*arg);
        if (argType == INVALID_TYPE_ID) {
            continue;
        }
        bool ok = format_check::arg_matches_expect(reg, argType, convs[j].expect);
        // %s: StrChar-ЛИТЕРАЛ уже const char* в C++ (emitExpr → "..."), поэтому допустим,
        // хотя тип StrChar (std::string). Переменная StrChar требует .c_str() → CString.
        if (!ok && convs[j].expect == format_check::Expect::StrChar && reg.getCanonicalTypeId(argType) == reg.getType(type::StrChar) &&
            arg->kind() == ParserToken::Kind::StrChar) {
            ok = true;
        }
        if (!ok) {
            const TypeId c = reg.getCanonicalTypeId(argType);
            std::string typeName = (c != INVALID_TYPE_ID) ? std::string(reg.getFullTypeName(c)) : "?";
            m_actx.ctx().report(arg->range(), semantic::DiagId::Format, "format argument {} expects {} (conversion '{}') but argument has type '{}'",
                                argPos + 1, format_expect_name(convs[j].expect), std::string(1, convs[j].conv), typeName);
        }
    }
}

// -- Компиляйт-тайм проверка строки-формата `"{}"(args)` / `'{}'(args)` --
// callee - строковый литерал (StrWide/StrChar). Сверяем число плейсхолдеров `{}` с числом
// аргументов ({{ / }} - литеральные скобки, аргумент не потребляют) и баланс фигурных скобок.
void ExprTyper::checkFormatStringArgs(CallExpr& call) {
    const auto* fmtNode = call.m_callee.get();
    if (!fmtNode || (fmtNode->kind() != ParserToken::Kind::StrChar && fmtNode->kind() != ParserToken::Kind::StrWide)) {
        return;
    }
    const std::string fmt(fmtNode->text());
    const size_t nArgs = call.m_args ? call.m_args->size() : 0;
    size_t placeholders = 0;
    int depth = 0;
    for (size_t i = 0; i < fmt.size(); ++i) {
        const char c = fmt[i];
        if (c == '{') {
            if (i + 1 < fmt.size() && fmt[i + 1] == '{') { // `{{` - литеральная скобка
                ++i;
                continue;
            }
            ++placeholders;
            ++depth;
            // Явный индекс `{N}` / `{N:spec}`: сверяем N с числом аргументов (иначе
            // std::format сгенерирует сломанный C++, а не понятную диагностику).
            size_t j = i + 1;
            if (j < fmt.size() && fmt[j] >= '0' && fmt[j] <= '9') {
                size_t idx = 0;
                while (j < fmt.size() && fmt[j] >= '0' && fmt[j] <= '9') {
                    idx = idx * 10 + static_cast<size_t>(fmt[j] - '0');
                    ++j;
                }
                if (idx >= nArgs) {
                    m_actx.ctx().report(fmtNode->range(), semantic::DiagId::Format,
                                        "format string '{}' references argument index {} but only {} argument(s) provided", fmt, idx, nArgs);
                }
            }
        } else if (c == '}') {
            if (i + 1 < fmt.size() && fmt[i + 1] == '}') { // `}}` - литеральная скобка
                ++i;
                continue;
            }
            --depth;
        }
    }
    if (depth != 0) {
        m_actx.ctx().report(fmtNode->range(), semantic::DiagId::Format, "unbalanced braces in format string '{}'", fmt);
        return;
    }
    if (placeholders != nArgs) {
        m_actx.ctx().report(fmtNode->range(), semantic::DiagId::Format, "format string '{}' has {} placeholder(s) but {} argument(s) provided", fmt,
                            placeholders, nArgs);
    }
}

void ExprTyper::widenInferredTarget(const AstNodeBase* lhs, TypeId result) {
    if (!lhs || lhs->kind() != ParserToken::Kind::Ident || result == INVALID_TYPE_ID) {
        return;
    }
    Symbol* s = m_actx.symbols().resolveMutable(lhs->text());
    if (!s || !testFlag(s->type, SymbolFlag::Inferred)) {
        return;
    }
    // Нетипизированная `x := _;` (deferred-вывод по записям): первая запись задаёт категорию типа.
    // Последующая запись НЕСОВМЕСТИМОЙ категории (напр. число потом строка) не может быть выведена
    // монотонно → явная Error с требованием аннотации `x:Any := _` (никакого тихого std::any-fallback).
    // Совместимые категории (числовые промоции / одинаковый тип) ведут тип как у обычного inferred.
    if (s->decl && s->decl->kind() == ParserToken::Kind::VarDecl) {
        const auto* vd = static_cast<const VarDecl*>(s->decl);
        if (!vd->m_type && vd->m_initializer && isNoneMarker(vd->m_initializer.get())) {
            const TypeId cur = structuralType(s->type);
            const TypeId nw = structuralType(result);
            if (cur != INVALID_TYPE_ID && nw != INVALID_TYPE_ID && cur != nw) {
                const TypeRegistry& reg = m_actx.ctx().types();
                const TypeId cc = reg.getCanonicalTypeId(cur);
                const TypeId nc = reg.getCanonicalTypeId(nw);
                const bool compatible = (cc == nc) || (isArithmeticGroup(getGroup(getKindFromId(cc))) && isArithmeticGroup(getGroup(getKindFromId(nc))));
                if (!compatible) {
                    std::string disp{lhs->text()};
                    if (!disp.empty() && disp.front() == '$') {
                        disp.erase(0, 1); // DSL-сигил: показываем имя без служебного '$'
                    }
                    m_actx.ctx().diag().report(Severity::Error, lhs->range(),
                                               "cannot assign type '{}' to '{}' whose type was inferred as '{}' from an earlier assignment to the untyped "
                                               "'{} := _' declaration; use explicit '{} :Any := _' to allow values of different types",
                                               reg.getFullTypeName(nc), disp, reg.getFullTypeName(cc), disp, disp);
                    return; // тип категории не перетираем; ошибка останавливает конвейер
                }
            }
        }
    }
    // Живой тип расширяется и сохраняет бит «выведен» (переменная остаётся inferred) и, если
    // переменная константна, бит «константность» (kConstFlag) не теряется при join.
    const bool wasConst = testFlag(s->type, SymbolFlag::Const);
    s->type = setFlag(result, SymbolFlag::Inferred);
    if (wasConst) {
        s->type = setFlag(s->type, SymbolFlag::Const);
    }
    // Обновить выведенный тип на узле объявления (VarDecl), чтобы декларация использовала
    // финальный join после сброса скоуп-стека (транспилятор читает VarDecl::inferredType).
    if (s->decl && s->decl->kind() == ParserToken::Kind::VarDecl) {
        static_cast<VarDecl*>(s->decl)->inferredType = clearFlag(result, SymbolFlag::Inferred);
    }
}

void ExprTyper::checkAssignmentNarrowing(const AstNodeBase* valueNode, TypeId sourceType, TypeId targetType, std::string_view targetName) {
    (void)targetName;
    if (valueNode == nullptr || sourceType == INVALID_TYPE_ID || targetType == INVALID_TYPE_ID) {
        return;
    }
    const TypeRegistry& reg = m_actx.ctx().types();
    const TypeId sc = reg.getCanonicalTypeId(sourceType);
    const TypeId dc = reg.getCanonicalTypeId(targetType);
    const TypeKind sKind = getKindFromId(sc);
    const TypeKind dKind = getKindFromId(dc);
    const Group sg = getGroup(sKind);
    const Group dg = getGroup(dKind);
    // Контракт value-vs-reference (см. types/REFType.md «двухосевая модель»): источник или цель -
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
        std::string srcName(reg.getFullTypeName(sc));
        std::string dstName(reg.getFullTypeName(dc));
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
                                                     targetName, valueNode->text());
            if (entry != nullptr && !valueNode->range().isInvalid()) {
                m_actx.ctx().diag().fixit(entry, valueNode->range(), "*" + std::string(valueNode->text()));
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
        const TypeId spo = reg.getPointeeType(sc);
        const TypeId dpo = reg.getPointeeType(dc);
        if (spo != INVALID_TYPE_ID && dpo != INVALID_TYPE_ID && reg.getCanonicalTypeId(spo) != reg.getCanonicalTypeId(dpo)) {
            m_actx.ctx().diag().report(Severity::Error, valueNode->range(), "reference pointee mismatch: cannot assign '{}' to '{}'", srcName, dstName);
        }
        return;
    }
    // Сверхразрядный целочисленный литерал (BigInteger-источник): валиден как BigInteger.
    // В фиксированную целую цель (`x:Int64 := <huge>`) не влезает → ошибка переполнения.
    const TypeId bi = reg.getCanonicalTypeId(reg.getType(type::BigInteger));
    if (sc == bi && valueNode->kind() == ParserToken::Kind::IntLiteral) {
        const Literal& intLit = static_cast<const Literal&>(*valueNode);
        // Влезает ли текст литерала в знаковый Int64 (единый предикат intFitsTarget, Int64-вид).
        const TypeKind int64Kind = getKindFromId(reg.getCanonicalTypeId(reg.getType(type::Int64)));
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
    // не влияет: любой "…" уже широкий; безопасного сужения и строкового cast нет).
    if (sg == Group::kStrWide && dg == Group::kStrChar) {
        std::string srcName(reg.getFullTypeName(sc));
        m_actx.ctx().diag().report(Severity::Error, valueNode->range(),
                                   "value of type '{}' cannot be narrowed to 'StrChar' (wide string into narrow); use single-quoted '…' for a narrow string",
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
    if (valueNode->kind() == ParserToken::Kind::IntLiteral && intFitsTarget(valueNode->text(), dKind)) {
        return;
    }
    // Сужение (переменная/неизвестное шире цели) → ошибка + fixit «use cast :Type(expr)».
    std::string dstName(reg.getFullTypeName(dc));
    std::string srcName(reg.getFullTypeName(sc));
    auto* entry = m_actx.ctx().diag().report(Severity::Error, valueNode->range(), "value of type '{}' cannot be narrowed to '{}' (use cast :{}(expr))", srcName,
                                             dstName, dstName);
    if (entry != nullptr && !valueNode->range().isInvalid()) {
        std::string replacement = std::format(":{}({})", dstName, valueNode->text());
        m_actx.ctx().diag().fixit(entry, valueNode->range(), replacement);
    }
}

} // namespace trust
