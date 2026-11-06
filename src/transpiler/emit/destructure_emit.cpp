#include "transpiler/emit/stmt_emit.hpp"
#include "transpiler/transpiler.hpp"
#include "transpiler/emit/emit_common.hpp"
#include "ast/ast_nodes.hpp"
#include "attrs/attr_builtin.hpp"
#include "ast/token_base.hpp"
#include "ast/ident_name.hpp"
#include "ast/kind_dispatch.hpp"
#include "ast/token_type.hpp"
#include "diag/registry.hpp"
#include "diag/base_diags.hpp"
#include "syntax/term/term.h"
#include "types/type_system.hpp"
#include "target/runtime_symbols.hpp"
#include "types/intrinsics.hpp"
#include "types/type_id.hpp"
#include "types/names.hpp"
#include "transpiler/emit/diag.hpp"
#include "utils/strings.hpp"
#include <format>
#include <memory>

namespace trust {

void StmtEmitter::visit_DestructureDecl(const DestructureDecl& n) {
    // `t1, ..., tN := [... ]source;` - деструктуризация. Spread (`... source`) - коллекция (Dict,
    // pop_front + «остаток»); без `...` - кортеж (std::get). `_` - skip (потребляется, не связывается).
    if (n.m_targets.empty() || !n.m_source) {
        // Семантика всегда заполняет цели и источник; пустой узел - инвариантное нарушение.
        // Вместо тихого no-op (AGENTS rule 5 «no silent fallback») - явная диагностика.
        m_ectx.m_services.report(n.range(), diag::DiagId::ParseError, "destructuring requires at least one target and a source expression");
        return;
    }
    // DestructureDecl - НЕ statement-выражение (не оборачивается SemicolonStmt, который добавляет
    // mapStart/mapStop), поэтому собственный маппинг здесь обязателен: иначе оператор раскрытия
    // словаря/кортежа не имел бы записи в source map и не мапился бы на выходной .cppt. Весь
    // диапазон оператора `t1, ..., tN := [... ]source;` покрывает все эмитируемые строки
    // (temp-источник + runtime-guard + pop_front'ы / std::get + rest) - как у ControlFlowStmt.
    MapperScope scope(m_ectx.m_services.source(), n.range(), m_ectx.m_output.out);
    if (n.m_isSpread) {
        emitDestructureDict(n);
    } else {
        emitDestructureTuple(n);
    }
}

void StmtEmitter::emitDestructureDict(const DestructureDecl& n) {
    const std::string ind = m_ectx.m_output.indentPrefix();
    // C++-имя источника (для rest-мутации == источнику); для не-Ident - пусто.
    std::string srcCpp;
    if (n.m_source && n.m_source->kind() == ParserToken::Kind::Ident) {
        srcCpp = utils::cppDeclName(requiredText(*n.m_source));
    }
    // Именованный rest (`rest...`): C++-имя цели (пропускаем `_...` - отброс).
    std::string restCpp;
    for (const DestructureDecl::Target& target : n.m_targets) {
        auto* t = target.name.get();
        if (target.isRest && t && t->kind() == ParserToken::Kind::Ident && requiredText(*t) != "_") {
            restCpp = utils::cppDeclName(requiredText(*t));
        }
    }
    // Мутация источника на месте: rest-цель == источнику (идиома `item, dict... := ... dict`).
    // Используется только для rest-переменной ниже (сам temp-источник создаёт lowering, m_sourceTemp).
    const bool mutatingRest = !restCpp.empty() && restCpp == srcCpp;
    // Источник: при mutating-rest - сам источник (pop'ы идут прямо в него); иначе - временная копия
    // (создана LOWERING, m_sourceTemp: `<конкретный тип> _trust_dst_N := <source>;`; тип источника
    // вычислен семантикой и перенесён lowering'ом в VarDecl::inferredType), из которой делаются
    // pop_front и rest-копия (одна оценка источника-выражения, не N раз).
    std::string srcRef = srcCpp;
    if (n.m_sourceTemp && n.m_sourceTemp->kind() == ParserToken::Kind::VarDecl) {
        const VarDecl* tmp = static_cast<const VarDecl*>(n.m_sourceTemp.get());
        srcRef = emitSyntheticVar(*tmp, m_ectx.m_output.out);
        if (srcRef.empty()) {
            m_ectx.m_services.report(n.range(), diag::DiagId::ParseError, "unable to generate C++ type for destructuring source temporary '{}'", requiredText(*tmp));
            return;
        }
    }
    // Runtime-недостаток элементов (динамический источник - размер неизвестен на этапе сборки):
    // точная привязка требует, чтобы число элементов было не меньше числа pop'ов. Guard с понятной
    // диагностикой вместо голого std::out_of_range из pop_front (AGENTS rule 5 - без тихого дефолта
    // / None; для кортежа арность проверяется статически - guard здесь не нужен, под-кортежи pop_front
    // не делают).
    // Имя файла и строку в сообщении берём из ИСХОДНОГО .src, а не из сгенерированного C++ (как
    // @__FILE_NAME__/@__FILE_LINE__ для @assert): через SourceMap по диапазону узла деструктуризации.
    const SourceLocation nloc = sourceLocation(m_ectx.m_services.source(), n.range());
    size_t needPops = 0;
    for (const DestructureDecl::Target& target : n.m_targets) {
        if (target.isRest) {
            continue;
        }
        ++needPops; // каждый не-rest target (включая `_` skip) делает pop_front
    }
    if (needPops > 0) {
        // trust__abort__ определён в trust/assert.hpp - обязательный инклуд (как для @assert).
        // Префикс '@' означает «quoted include из пакета»: заголовок берётся из
        // `include/trust/` распакованного `stdlib.trust`, доступного через -Istdlib/include.
        m_driver.m_type.recordRequiredInclude("@trust/assert.hpp");
        m_ectx.m_services.source().output_append(m_ectx.m_output.out, ind + "if (" + srcRef + ".size() < " + std::to_string(needPops) +
                                                                     ") trust::trust__abort__(\"" + utils::escapeCppString(nloc.file) + "\", " +
                                                                     std::to_string(nloc.line) + ", \"destructuring: not enough elements in source\");\n");
    }
    // Цели-элементы (НЕ rest): pop_front. Вне цикла - per-element тип элемента (any_cast<T>, где T -
    // runtime-тип: Int8..Int64 → int64_t, Float → double, Bool, StrChar...). ВНУТРИ цикла тип расширен
    // до максимального (Integer/Double); гетерогенность (bool+int) обрабатывается runtime-конвертерами
    // (anyToInt64/anyToDouble/anyToString), а не строгим any_cast. Any → std::any.
    const TypeReadModel& reg = m_ectx.m_services.readTypes();
    const TypeId i64C = reg.store().desugaredTypeId(reg.getType(type::Int64));
    const TypeId dblC = reg.store().desugaredTypeId(reg.getType(type::Double));
    const TypeId scC = reg.store().desugaredTypeId(reg.getType(type::StrChar));
    for (const DestructureDecl::Target& target : n.m_targets) {
        auto* t = target.name.get();
        if (target.isRest) {
            continue;
        }
        if (!t || t->kind() != ParserToken::Kind::Ident) {
            continue;
        }
        m_ectx.m_services.source().output_append(m_ectx.m_output.out, ind);
        if (requiredText(*t) == "_") {
            m_ectx.m_services.source().output_append(m_ectx.m_output.out, srcRef + ".pop_front();\n");
            continue;
        }
        const std::string cppName = utils::cppDeclName(requiredText(*t));
        const TypeId et = target.anyCastType;
        const TypeId etC = (et != INVALID_TYPE_ID) ? reg.store().desugaredTypeId(et) : INVALID_TYPE_ID;
        // Префикс объявления: присваивание (`a = ...`) - без типа; объявление - тип + имя.
        const std::string anyPrefix = n.m_isAssign ? "" : "std::any ";
        if (etC == INVALID_TYPE_ID) {
            m_ectx.m_services.source().output_append(m_ectx.m_output.out, anyPrefix + cppName + " = " + srcRef + ".pop_front();\n");
            continue;
        }
        // Тип переменной: аннотация цели (target.declaredType) или выведенный (et).
        const TypeId declared = (target.declaredType != INVALID_TYPE_ID) ? target.declaredType : et;
        const auto tname = m_driver.m_type.emitTypeName(declared);
        if (!tname || tname->empty()) {
            // Тип цели уже резолвлен семантикой (naturalRuntimeType возвращает только типы с
            // C++-именем: Int64/Double/Bool/StrChar/StrWide/Any); сбой emitTypeName - инвариантное
            // нарушение. Не тихий fallback на std::any (AGENTS rule 5) - явная диагностика.
            m_ectx.m_services.report(n.range(), diag::DiagId::ParseError, "unable to emit C++ type for destructuring target '{}'", cppName);
            return;
        }
        // Тип any_cast: natural runtime тип ЭЛЕМЕНТА (target.anyCastType) - соответствует хранению Dict
        // (int → int64_t); переменная объявляется типом declared (аннотация/выведенный).
        const auto castName = m_driver.m_type.emitTypeName(et);
        const std::string castType = (castName && !castName->empty()) ? *castName : *tname;
        const std::string prefix = n.m_isAssign ? "" : (*tname + " ");
        if (n.m_inLoop) {
            if (etC == i64C) {
                m_ectx.m_services.source().output_append(m_ectx.m_output.out, prefix + cppName + " = trust::detail::anyToInt64(" + srcRef + ".pop_front());\n");
            } else if (etC == dblC) {
                m_ectx.m_services.source().output_append(m_ectx.m_output.out, prefix + cppName + " = trust::detail::anyToDouble(" + srcRef + ".pop_front());\n");
            } else if (etC == scC) {
                m_ectx.m_services.source().output_append(m_ectx.m_output.out, prefix + cppName + " = trust::detail::anyToString(" + srcRef + ".pop_front());\n");
            } else {
                // Bool и пр. - однородные: строгий any_cast безопасен (хранится как есть).
                m_ectx.m_services.source().output_append(m_ectx.m_output.out,
                                                    prefix + cppName + " = std::any_cast<" + castType + ">(" + srcRef + ".pop_front());\n");
            }
        } else {
            m_ectx.m_services.source().output_append(m_ectx.m_output.out, prefix + cppName + " = std::any_cast<" + castType + ">(" + srcRef + ".pop_front());\n");
        }
    }
    // Именованный rest: «остаток» - копия источника после pop'ов (источник не мутируется);
    // в режиме присваивания - `rest = src` (цель уже существует).
    if (!mutatingRest && !restCpp.empty()) {
        m_ectx.m_services.source().output_append(m_ectx.m_output.out, ind);
        m_ectx.m_services.source().output_append(m_ectx.m_output.out, (n.m_isAssign ? "" : "trust::Dict ") + restCpp + " = " + srcRef + ";\n");
    }
    // mutatingRest - источник уже мутирован pop'ами и является rest; присвоение не нужно.
    // `_...` (отброс остатка) - ничего не генерируем: остаток остаётся во временной переменной.
}

void StmtEmitter::emitDestructureTuple(const DestructureDecl& n) {
    m_driver.m_type.recordRequiredInclude("#include <tuple>");
    const std::string ind = m_ectx.m_output.indentPrefix();
    // Арность источника-кортежа (для rest): семантика сохранила её на узле (скоуп-стек
    // к моменту кодогенерации сброшен, локальные символы недоступны). Запасной путь - литерал.
    // `m_sourceArity == 0` здесь НЕ является инвариантным нарушением: пустой кортеж `():Tuple`
    // даёт легитимную нулевую арность (например, один rest `r... := t` → пустой make_tuple()).
    // Литерал-фолбэк покрывает прямое построение AST в unit-тестах (без прохода семантики).
    size_t elemCount = n.m_sourceArity;
    if (elemCount == 0 && n.m_source && is_collection_literal_kind(n.m_source->kind())) {
        elemCount = static_cast<const Sequence&>(*n.m_source).m_body.size();
    }
    size_t idx = 0;
    bool first = true;
    // Цели-элементы: std::get<N> (bind или skip `_`).
    for (const DestructureDecl::Target& target : n.m_targets) {
        auto* t = target.name.get();
        if (!t || t->kind() != ParserToken::Kind::Ident) {
            continue;
        }
        if (target.isRest) {
            continue; // rest обрабатывается отдельно ниже
        }
        if (requiredText(*t) == "_") {
            ++idx; // skip-элемент занимает индекс, но не связывается
            continue;
        }
        if (!first) {
            m_ectx.m_services.source().output_append(m_ectx.m_output.out, ind);
        }
        first = false;
        // Префикс объявления: присваивание (`a = ...`) - без типа; иначе - конкретный тип цели
        // (явная аннотация или тип элемента кортежа, выведенный семантикой). Тип ОБЯЗАН быть
        // известен: `auto` здесь был бы silent fallback (AGENTS п.5).
        std::string prefix;
        if (n.m_isAssign) {
            prefix.clear();
        } else {
            if (target.anyCastType == INVALID_TYPE_ID) {
                m_ectx.m_services.report(t->range(), diag::DiagId::ParseError, "internal: destructuring target '{}' has no resolved type", requiredText(*t));
                return;
            }
            const auto tname = m_driver.m_type.emitTypeName(target.anyCastType);
            if (!tname || tname->empty()) {
                m_ectx.m_services.report(t->range(), diag::DiagId::ParseError, "unable to generate C++ type for destructuring target '{}'", requiredText(*t));
                return;
            }
            prefix = *tname + " ";
        }
        m_ectx.m_services.source().output_append(m_ectx.m_output.out, prefix + utils::cppDeclName(requiredText(*t)) + " = std::get<" + std::to_string(idx) + ">(");
        m_driver.emitExpr(n.m_source.get());
        m_ectx.m_services.source().output_append(m_ectx.m_output.out, ");\n");
        ++idx;
    }
    // Именованный rest (`rest...`): остаток как make_tuple оставшихся элементов (std::get<k>).
    // `_...` (отброс остатка) - ничего не генерируем.
    for (const DestructureDecl::Target& target : n.m_targets) {
        auto* t = target.name.get();
        if (!t || t->kind() != ParserToken::Kind::Ident) {
            continue;
        }
        if (!target.isRest || requiredText(*t) == "_") {
            continue;
        }
        if (!first) {
            m_ectx.m_services.source().output_append(m_ectx.m_output.out, ind);
        }
        first = false;
        std::string restPrefix;
        if (!n.m_isAssign) {
            // Конкретный тип rest-кортежа выведен семантикой (std::tuple<Elem...>); `auto` здесь
            // был бы silent fallback.
            if (target.anyCastType == INVALID_TYPE_ID) {
                m_ectx.m_services.report(t->range(), diag::DiagId::ParseError, "internal: rest target '{}' has no resolved tuple type", requiredText(*t));
                return;
            }
            const auto tname = m_driver.m_type.emitTypeName(target.anyCastType);
            if (!tname || tname->empty()) {
                m_ectx.m_services.report(t->range(), diag::DiagId::ParseError, "unable to generate C++ type for rest target '{}'", requiredText(*t));
                return;
            }
            restPrefix = *tname + " ";
        }
        m_ectx.m_services.source().output_append(m_ectx.m_output.out, restPrefix + utils::cppDeclName(requiredText(*t)) + " = std::make_tuple(");
        for (size_t k = idx; k < elemCount; ++k) {
            if (k != idx) {
                m_ectx.m_services.source().output_append(m_ectx.m_output.out, ", ");
            }
            m_ectx.m_services.source().output_append(m_ectx.m_output.out, "std::get<" + std::to_string(k) + ">(");
            m_driver.emitExpr(n.m_source.get());
            m_ectx.m_services.source().output_append(m_ectx.m_output.out, ")");
        }
        m_ectx.m_services.source().output_append(m_ectx.m_output.out, ");\n");
    }
}

} // namespace trust
