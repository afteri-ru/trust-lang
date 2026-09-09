// Generated: src/transpiler/stmt_emit.cpp
#include "transpiler/stmt_emit.hpp"
#include "transpiler/transpiler.hpp"
#include "transpiler/emit_common.hpp"
#include "ast/ast_nodes.hpp"
#include "attrs/attr_builtin.hpp"
#include "ast/token_base.hpp"
#include "ast/ident_name.hpp"
#include "ast/kind_visitor.hpp"
#include "ast/token_type.hpp"
#include "session/context.hpp"
#include "diag/registry.hpp"
#include "diag/base_diags.hpp"
#include "analysis/symbol_table.hpp"
#include "syntax/term.h"
#include "types/registry.hpp"
#include "types/runtime_symbols.hpp"
#include "types/intrinsics.hpp"
#include "types/type_id.hpp"
#include "types/type_names.hpp"
#include "transpiler/diag.hpp"
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
        m_ectx.m_ctx.report(n.range(), diag::DiagId::ParseError, "destructuring requires at least one target and a source expression");
        return;
    }
    // DestructureDecl - НЕ statement-выражение (не оборачивается SemicolonStmt, который добавляет
    // mapStart/mapStop), поэтому собственный маппинг здесь обязателен: иначе оператор раскрытия
    // словаря/кортежа не имел бы записи в source map и не мапился бы на выходной .cppt. Весь
    // диапазон оператора `t1, ..., tN := [... ]source;` покрывает все эмитируемые строки
    // (temp-источник + runtime-guard + pop_front'ы / std::get + rest) - как у ControlFlowStmt.
    MapperScope scope(m_ectx.m_ctx.source(), n.range(), m_ectx.m_out);
    if (n.m_isSpread) {
        emitDestructureDict(n);
    } else {
        emitDestructureTuple(n);
    }
}

void StmtEmitter::emitDestructureDict(const DestructureDecl& n) {
    const std::string ind = m_ectx.indentPrefix();
    const size_t cnt = n.m_targets.size();
    // C++-имя источника (для rest-мутации == источнику); для не-Ident - пусто.
    std::string srcCpp;
    if (n.m_source && n.m_source->kind() == ParserToken::Kind::Ident) {
        srcCpp = utils::name_to_cpp(n.m_source->text());
    }
    // Именованный rest (`rest...`): C++-имя цели (пропускаем `_...` - отброс).
    std::string restCpp;
    for (size_t i = 0; i < cnt; ++i) {
        auto* t = n.m_targets[i].get();
        if (i < n.m_targetIsRest.size() && n.m_targetIsRest[i] && t && t->kind() == ParserToken::Kind::Ident && t->text() != "_") {
            restCpp = utils::name_to_cpp(t->text());
        }
    }
    // Мутация источника на месте: rest-цель == источнику (идиома `item, dict... := ... dict`).
    // Используется только для rest-переменной ниже (сам temp-источник создаёт lowering, m_sourceTemp).
    const bool mutatingRest = !restCpp.empty() && restCpp == srcCpp;
    // Источник: при mutating-rest - сам источник (pop'ы идут прямо в него); иначе - временная копия
    // (создана LOWERING, m_sourceTemp: `auto _trust_dst_N := <source>;`), из которой делаются
    // pop_front и rest-копия (одна оценка источника-выражения, не N раз).
    std::string srcRef = srcCpp;
    if (n.m_sourceTemp && n.m_sourceTemp->kind() == ParserToken::Kind::VarDecl) {
        const VarDecl* tmp = static_cast<const VarDecl*>(n.m_sourceTemp.get());
        srcRef = emitSyntheticVar(*tmp, m_ectx.m_out);
        if (srcRef.empty()) {
            m_ectx.m_ctx.report(n.range(), diag::DiagId::ParseError, "unable to generate C++ type for destructuring source temporary '{}'", tmp->text());
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
    const SourceLocation nloc = sourceLocation(m_ectx.m_ctx.source(), n.range());
    size_t needPops = 0;
    for (size_t i = 0; i < cnt; ++i) {
        if (i < n.m_targetIsRest.size() && n.m_targetIsRest[i]) {
            continue;
        }
        ++needPops; // каждый не-rest target (включая `_` skip) делает pop_front
    }
    if (needPops > 0) {
        // trust__abort__ определён в trust/assert.hpp - обязательный инклуд (как для @assert).
        // Префикс '@' направляет заголовок в механизм извлечения рантайм-заголовков (extractRuntimeHeader):
        // он извлекается в <build_dir>/trust/assert.hpp + добавляется `-I<build_dir>`, поэтому
        // доступен и в изолированной сборке (--run из произвольного каталога), а не только при
        // запуске из корня проекта с `-I<проект>/include`.
        m_driver.m_type.recordRequiredInclude("@trust/assert.hpp");
        m_ectx.m_ctx.source().output_append(m_ectx.m_out, ind + "if (" + srcRef + ".size() < " + std::to_string(needPops) + ") trust::trust__abort__(\"" +
                                                              utils::escape_cpp_string(nloc.file) + "\", " + std::to_string(nloc.line) +
                                                              ", \"destructuring: not enough elements in source\");\n");
    }
    // Цели-элементы (НЕ rest): pop_front. Вне цикла - per-element тип элемента (any_cast<T>, где T -
    // runtime-тип: Int8..Int64 → int64_t, Float → double, Bool, StrChar...). ВНУТРИ цикла тип расширен
    // до максимального (Integer/Double); гетерогенность (bool+int) обрабатывается runtime-конвертерами
    // (anyToInt64/anyToDouble/anyToString), а не строгим any_cast. Any → std::any.
    const TypeRegistry& reg = m_ectx.m_ctx.types();
    const TypeId i64C = reg.getCanonicalTypeId(reg.getType(type::Int64));
    const TypeId dblC = reg.getCanonicalTypeId(reg.getType(type::Double));
    const TypeId scC = reg.getCanonicalTypeId(reg.getType(type::StrChar));
    for (size_t i = 0; i < cnt; ++i) {
        auto* t = n.m_targets[i].get();
        if (i < n.m_targetIsRest.size() && n.m_targetIsRest[i]) {
            continue;
        }
        if (!t || t->kind() != ParserToken::Kind::Ident) {
            continue;
        }
        m_ectx.m_ctx.source().output_append(m_ectx.m_out, ind);
        if (t->text() == "_") {
            m_ectx.m_ctx.source().output_append(m_ectx.m_out, srcRef + ".pop_front();\n");
            continue;
        }
        const std::string cppName = utils::name_to_cpp(t->text());
        const TypeId et = (i < n.m_targetTypes.size()) ? n.m_targetTypes[i] : INVALID_TYPE_ID;
        const TypeId etC = (et != INVALID_TYPE_ID) ? reg.getCanonicalTypeId(et) : INVALID_TYPE_ID;
        // Префикс объявления: присваивание (`a = ...`) - без типа; объявление - тип + имя.
        const std::string anyPrefix = n.m_isAssign ? "" : "std::any ";
        if (etC == INVALID_TYPE_ID) {
            m_ectx.m_ctx.source().output_append(m_ectx.m_out, anyPrefix + cppName + " = " + srcRef + ".pop_front();\n");
            continue;
        }
        // Тип переменной: аннотация цели (m_targetDeclaredTypes) или выведенный (et).
        const TypeId declared = (i < n.m_targetDeclaredTypes.size() && n.m_targetDeclaredTypes[i] != INVALID_TYPE_ID) ? n.m_targetDeclaredTypes[i] : et;
        const auto tname = m_driver.m_type.emitTypeName(declared, t->text());
        if (!tname || tname->empty()) {
            // Тип цели уже резолвлен семантикой (naturalRuntimeType возвращает только типы с
            // C++-именем: Int64/Double/Bool/StrChar/StrWide/Any); сбой emitTypeName - инвариантное
            // нарушение. Не тихий fallback на std::any (AGENTS rule 5) - явная диагностика.
            m_ectx.m_ctx.report(n.range(), diag::DiagId::ParseError, "unable to emit C++ type for destructuring target '{}'", cppName);
            return;
        }
        // Тип any_cast: natural runtime тип ЭЛЕМЕНТА (m_targetTypes) - соответствует хранению Dict
        // (int → int64_t); переменная объявляется типом declared (аннотация/выведенный).
        const auto castName = m_driver.m_type.emitTypeName(et, t->text());
        const std::string castType = (castName && !castName->empty()) ? *castName : *tname;
        const std::string prefix = n.m_isAssign ? "" : (*tname + " ");
        if (n.m_inLoop) {
            if (etC == i64C) {
                m_ectx.m_ctx.source().output_append(m_ectx.m_out, prefix + cppName + " = trust::detail::anyToInt64(" + srcRef + ".pop_front());\n");
            } else if (etC == dblC) {
                m_ectx.m_ctx.source().output_append(m_ectx.m_out, prefix + cppName + " = trust::detail::anyToDouble(" + srcRef + ".pop_front());\n");
            } else if (etC == scC) {
                m_ectx.m_ctx.source().output_append(m_ectx.m_out, prefix + cppName + " = trust::detail::anyToString(" + srcRef + ".pop_front());\n");
            } else {
                // Bool и пр. - однородные: строгий any_cast безопасен (хранится как есть).
                m_ectx.m_ctx.source().output_append(m_ectx.m_out, prefix + cppName + " = std::any_cast<" + castType + ">(" + srcRef + ".pop_front());\n");
            }
        } else {
            m_ectx.m_ctx.source().output_append(m_ectx.m_out, prefix + cppName + " = std::any_cast<" + castType + ">(" + srcRef + ".pop_front());\n");
        }
    }
    // Именованный rest: «остаток» - копия источника после pop'ов (источник не мутируется);
    // в режиме присваивания - `rest = src` (цель уже существует).
    if (!mutatingRest && !restCpp.empty()) {
        m_ectx.m_ctx.source().output_append(m_ectx.m_out, ind);
        m_ectx.m_ctx.source().output_append(m_ectx.m_out, (n.m_isAssign ? "" : "trust::Dict ") + restCpp + " = " + srcRef + ";\n");
    }
    // mutatingRest - источник уже мутирован pop'ами и является rest; присвоение не нужно.
    // `_...` (отброс остатка) - ничего не генерируем: остаток остаётся во временной переменной.
}

void StmtEmitter::emitDestructureTuple(const DestructureDecl& n) {
    m_driver.m_type.recordRequiredInclude("#include <tuple>");
    const std::string ind = m_ectx.indentPrefix();
    const size_t cnt = n.m_targets.size();
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
    for (size_t i = 0; i < cnt; ++i) {
        auto* t = n.m_targets[i].get();
        if (!t || t->kind() != ParserToken::Kind::Ident) {
            continue;
        }
        if (i < n.m_targetIsRest.size() && n.m_targetIsRest[i]) {
            continue; // rest обрабатывается отдельно ниже
        }
        if (t->text() == "_") {
            ++idx; // skip-элемент занимает индекс, но не связывается
            continue;
        }
        if (!first) {
            m_ectx.m_ctx.source().output_append(m_ectx.m_out, ind);
        }
        first = false;
        // Префикс объявления: присваивание (`a = ...`) - без типа; явная аннотация (`a:Int32`,
        // m_targetTypes[i] заполнена семантикой) - фиксированный тип; иначе `auto` (элемент кортежа).
        std::string prefix;
        if (n.m_isAssign) {
            prefix.clear();
        } else if (i < n.m_targetTypes.size() && n.m_targetTypes[i] != INVALID_TYPE_ID) {
            const auto tname = m_driver.m_type.emitTypeName(n.m_targetTypes[i], t->text());
            prefix = (tname && !tname->empty()) ? (*tname + " ") : "auto ";
        } else {
            prefix = "auto ";
        }
        m_ectx.m_ctx.source().output_append(m_ectx.m_out, prefix + utils::name_to_cpp(t->text()) + " = std::get<" + std::to_string(idx) + ">(");
        m_driver.emitExpr(n.m_source.get());
        m_ectx.m_ctx.source().output_append(m_ectx.m_out, ");\n");
        ++idx;
    }
    // Именованный rest (`rest...`): остаток как make_tuple оставшихся элементов (std::get<k>).
    // `_...` (отброс остатка) - ничего не генерируем.
    for (size_t i = 0; i < cnt; ++i) {
        auto* t = n.m_targets[i].get();
        if (!t || t->kind() != ParserToken::Kind::Ident) {
            continue;
        }
        if (!(i < n.m_targetIsRest.size() && n.m_targetIsRest[i]) || t->text() == "_") {
            continue;
        }
        if (!first) {
            m_ectx.m_ctx.source().output_append(m_ectx.m_out, ind);
        }
        first = false;
        const std::string restPrefix = n.m_isAssign ? "" : "auto ";
        m_ectx.m_ctx.source().output_append(m_ectx.m_out, restPrefix + utils::name_to_cpp(t->text()) + " = std::make_tuple(");
        for (size_t k = idx; k < elemCount; ++k) {
            if (k != idx) {
                m_ectx.m_ctx.source().output_append(m_ectx.m_out, ", ");
            }
            m_ectx.m_ctx.source().output_append(m_ectx.m_out, "std::get<" + std::to_string(k) + ">(");
            m_driver.emitExpr(n.m_source.get());
            m_ectx.m_ctx.source().output_append(m_ectx.m_out, ")");
        }
        m_ectx.m_ctx.source().output_append(m_ectx.m_out, ");\n");
    }
}

} // namespace trust
