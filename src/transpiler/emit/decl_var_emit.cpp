#include "transpiler/emit/decl_emit.hpp"
#include "transpiler/transpiler.hpp"
#include "transpiler/emit/emit_common.hpp"
#include "ast/ast_nodes.hpp"
#include "attrs/attr_builtin.hpp"
#include "ast/kind_dispatch.hpp"
#include "session/context.hpp"
#include "diag/registry.hpp"
#include "diag/base_diags.hpp"
#include "syntax/term/term.h"
#include "types/type_system.hpp"
#include "types/types.hpp"
#include "types/runtime_symbols.hpp"
#include "types/intrinsics.hpp"
#include "types/type_id.hpp"
#include "types/names.hpp"
#include "transpiler/emit/diag.hpp"
#include "utils/strings.hpp"
#include <cstdint>
#include <format>
#include <limits>
#include <memory>
#include <optional>

namespace trust {

void DeclEmitter::generateVarDeclToFile(const VarDecl& var_node, MapperFile output_idx) {
    // Флаг линковки нативной библиотеки из @[link("имя")].
    m_driver.m_type.collectLinkLib(var_node);
    // Зависимый C++-заголовок из @[include(\"header\")@] на нативной переменной/типе.
    m_driver.m_type.collectInclude(var_node);

    // Манглинг trust-имени: объявления эмитятся КОРОТКИМ именем внутри автоматического
    // namespace модуля (изоляция — конверт, не префикс; срез '%' у нативных имён).
    std::string var_name = utils::cppDeclName(var_node.text());
    if (var_name.empty()) {
        return;
    }

    // Семантический тип переменной материализован анализатором (VarDecl::inferredType);
    // кодоген не переприменяет квалификаторы аннотации/узла.
    const QualType declared = var_node.inferredType;
    const RefType var_rt = declared.isValid() ? getRefType(getTypeKind(declared.getTypeId())) : RefType::kValue;
    const bool ref_wrapper = (var_rt == RefType::kShared || var_rt == RefType::kWeak || var_rt == RefType::kLocker);
    const bool ref_unique = (var_rt == RefType::kUnique);
    const AttrPool& attrs = m_ectx.m_ctx.attrs();

    // Deleter внешнего ресурса: C++-имя D для `<Wrapper>::adopt(handle, D{})`.
    std::string deleter_cpp;
    const AstNodeAttr* attrSources[2] = {var_node.m_type ? var_node.m_type->as_attr() : nullptr, &var_node};
    for (const AstNodeAttr* source : attrSources) {
        const auto did = attrs.lookup(attr::Deleter);
        if (source == nullptr || !did.has_value() || !source->has_attr(*did)) {
            continue;
        }
        const auto* args = source->attr_args(*did);
        if (!args || args->size() != 1 || args->front().empty()) {
            continue;
        }
        if (auto tid = m_driver.m_type.findRegisteredType(args->front())) {
            if (auto nm = m_driver.m_type.emitTypeName(*tid)) {
                deleter_cpp = *nm;
            }
        }
    }

    // Determine type: типизированное имя → тип аннотации; нетипизированное → выведенный
    // анализатором конкретный тип (inferred join по истории присвоений), иначе std::any.
    std::string cpp_type;
    // Базовый (pointee) TypeId и его C++-имя для ссылочных переменных (shared/weak/unique):
    // нужны для trust::make_unique<PoT> у эксклюзивного владения и для уникальной эмиссии инициализатора.
    std::optional<TypeId> ref_base_id;
    std::string ref_pointee_cpp;
    const bool typed =
        var_node.m_type && (var_node.m_type->kind() == ParserToken::Kind::TypeName || var_node.m_type->kind() == ParserToken::Kind::RefMakeExpr ||
                            var_node.m_type->kind() == ParserToken::Kind::RefTakeExpr);
    if (typed) {
        if (ref_wrapper || ref_unique) {
            if (!declared.isValid()) {
                m_ectx.m_ctx.report(var_node.range(), diag::DiagId::ParseError, "reference type of '{}' is not materialized by semantic analysis",
                                    var_node.text());
                return;
            }
            auto nm = m_driver.m_type.emitTypeName(declared.getTypeId());
            if (!nm || nm->empty()) {
                m_ectx.m_ctx.report(var_node.range(), diag::DiagId::ParseError, "unable to generate C++ type for reference variable '{}'", var_node.text());
                return;
            }
            cpp_type = std::move(*nm);
            const QualType pointee = m_ectx.m_ctx.readTypes().store().getPointeeType(declared);
            if (!pointee.isValid()) {
                m_ectx.m_ctx.report(var_node.range(), diag::DiagId::ParseError, "reference variable '{}' has no pointee type", var_node.text());
                return;
            }
            ref_base_id = pointee.getTypeId();
            // Pointee C++-имя (для trust::make_unique<PoT> у эксклюзивного владения).
            auto pn = m_driver.m_type.emitTypeName(*ref_base_id);
            if (!pn || pn->empty()) {
                m_ectx.m_ctx.report(var_node.range(), diag::DiagId::ParseError, "unable to generate C++ pointee type for reference variable '{}'",
                                    var_node.text());
                return;
            }
            ref_pointee_cpp = std::move(*pn);
        } else if (isNativeRefKind(var_rt)) {
            if (!declared.isValid()) {
                m_ectx.m_ctx.report(var_node.range(), diag::DiagId::ParseError, "reference type of '{}' is not materialized by semantic analysis",
                                    var_node.text());
                return;
            }
            auto nm = m_driver.m_type.emitTypeName(declared.getTypeId());
            if (!nm || nm->empty()) {
                m_ectx.m_ctx.report(var_node.range(), diag::DiagId::ParseError, "unable to generate C++ type for reference variable '{}'", var_node.text());
                return;
            }
            cpp_type = std::move(*nm);
        } else {
            // Для явной аннотации конкретный тип может быть вычислен семантикой: dtype тензора
            // (`:Tensor[dims]`) или сужение абстрактного `:Tuple`/`:Range`/`:Array` по
            // инициализатору. Если он есть - эмитим его; иначе - обычное имя аннотации.
            if (var_node.inferredType.getTypeId() != INVALID_TYPE_ID) {
                if (auto n = m_driver.m_type.emitTypeName(var_node.inferredType)) {
                    cpp_type = std::move(*n);
                }
            } else {
                cpp_type = m_driver.m_type.emitTypeNameForNode(var_node.m_type.get());
            }
            if (cpp_type.empty()) {
                return;
            }
        }
    } else {
        // Нетипизированная переменная: выведенный семантикой конкретный тип, либо ЯВНО помеченный
        // std::any (семантика маркирует Any для тип-less инициализаторов - тип-имя, embed, вызов
        // с неизвестным результатом, отрицательный литерал - и для forward-объявлений без типа).
        // Any - обычный выводимый тип → эмитим единообразно emitTypeName(inferred). INVALID у
        // переменной - ошибка вывода: тихий fallback на std::any запрещён (AGENTS rule 5).
        QualType inferred = var_node.inferredType;
        // Fallback на таблицу символов запрещён: тип обязан быть материализован семантикой на
        // узле (включая forward-объявления, завершённые определением). Отсутствие - дефект
        // материализации; ниже выдаётся диагностика (тихий std::any недопустим, AGENTS п.5).
        if (!inferred.isValid()) {
            if (var_node.m_initializer) {
                m_ectx.m_ctx.report(var_node.range(), diag::DiagId::ParseError, "unable to infer type for variable '{}'", var_node.text());
            } else {
                m_ectx.m_ctx.report(var_node.range(), diag::DiagId::ParseError, "unable to generate C++ type '{}'", type_generic::Any);
            }
            return;
        }
        // Ссылочная переменная без явного типа (`&& x := 5`): inferred уже несёт полный ссылочный тип
        // (semantic обернул pointee видом из префиксного сигла). Базу (pointee) берём из типа;
        // отсутствие pointee — нарушение инварианта анализа, а не повод подставить сам тип.
        if (ref_wrapper || ref_unique) {
            const TypeReadModel& reg = m_ectx.m_ctx.readTypes();
            const QualType pointee = reg.store().getPointeeType(inferred);
            if (!pointee.isValid()) {
                m_ectx.m_ctx.report(var_node.range(), diag::DiagId::ParseError, "reference variable '{}' has no pointee type", var_node.text());
                return;
            }
            ref_base_id = pointee.getTypeId();
            auto pn = m_driver.m_type.emitTypeName(*ref_base_id);
            if (!pn || pn->empty()) {
                m_ectx.m_ctx.report(var_node.range(), diag::DiagId::ParseError, "unable to generate C++ pointee type for reference variable '{}'",
                                    var_node.text());
                return;
            }
            ref_pointee_cpp = std::move(*pn);
        }
        std::optional<std::string> name = m_driver.m_type.emitTypeName(inferred);
        if (!name || name->empty()) {
            m_ectx.m_ctx.report(var_node.range(), diag::DiagId::ParseError, "unable to generate C++ type '{}'", type_generic::Any);
            return;
        }
        cpp_type = std::move(*name);
    }

    // Инклуды типа не нужны здесь: emitTypeName отметил тип (m_ectx.m_accumulators.usedTypes), инклуды будут
    // сформированы из них ПОСЛЕ обхода AST (collectTypeIncludes).

    MapperScope scope(m_ectx.m_ctx.source(), var_node.range(), output_idx);
    // Константность ОБЪЯВЛЕНИЯ переменной - attr::ReadOnly на узле ('^' на имени или
    // @[readonly@]). Became-const (`x := 42; x^ += 1;`) — flow-факт DA-прохода; декларация
    // остаётся не-const. Признак на узле = const «в типе» объявления.
    // Префикс влияет на смещение имени в выводе (source-map): имя идёт после "<prefix> <cpp_type> ".
    std::string prefix;
    if (var_node.has_attr(m_ectx.m_ctx.attrs(), attr::ReadOnly)) {
        prefix += "const ";
    }
    if (var_node.has_attr(m_ectx.m_ctx.attrs(), attr::ThreadLocal)) {
        prefix += "thread_local ";
    }
    // Forward-объявление `x:Type := ...;` → C++ extern-декларация переменной (объявление без
    // определения); иначе - определение с инициализатором. Смещение имени в выводе зависит
    // от наличия префикса "extern " (source-map).
    // Маркер `_` в позиции инициализатора (`x := _;` / `x:Type := _;`) - «объявить без значения»,
    // не инициализатор-значение: кодоген эмитит определение без инициализатора `T c_x;`
    // (запись до чтения гарантирует анализатор). Проверяем по kind (Ident) ДО text(), т.к.
    // text() требует source-терм, а ручные тестовые узлы (RangeExpr/CallExpr и т.п.) его не имеют.
    const bool noneInit = isNoneMarker(var_node.m_initializer.get());
    uint32_t namePrefixLen = static_cast<uint32_t>(prefix.length()) + static_cast<uint32_t>(cpp_type.length()) + 1;
    if (!var_node.m_initializer || m_ectx.m_mode.forwardDeclOnly) {
        m_ectx.m_ctx.source().output_append(output_idx, "extern " + prefix + cpp_type + " " + var_name + ";");
        namePrefixLen += 7; // strlen("extern ")
    } else {
        // Reference-wrapper (trust::Shared/Weak/Take): explicit-конструкторы, поэтому инициализируем
        // конструкторным стилем `cpp_type name(<init>);` (покрывает и создание нового объекта
        // `Shared<int>(5)`, и копирование из существующей ссылки). Прочие типы - обычной
        // инициализацией.
        // Владеющий ресурс с deleter (@[deleter(D)]): создание через `<Wrapper>::adopt(handle, D{})`
        // (перенос владения сырым handle). Для unique D - часть типа, для shared D стирается.
        const bool adopt_init = !deleter_cpp.empty() && (ref_wrapper || ref_unique) && !noneInit;
        if (adopt_init) {
            m_ectx.m_ctx.source().output_append(output_idx, prefix + cpp_type + " " + var_name + " = " + cpp_type + "::adopt(");
            m_driver.emitExpr(var_node.m_initializer.get());
            m_ectx.m_ctx.source().output_append(output_idx, ", " + deleter_cpp + "{});");
        } else if (ref_wrapper) {
            // Инициализатор - адресная операция `& shared_var` (RefMakeExpr, даёт weak-временное
            // `trust::Weak<...>(c_x)`): конструкторный стиль `Weak<...> c_w(Weak<...>(c_x))` дал бы
            // most-vexing-parse (тип-внутри-типа) → используем копирующую `cpp_type name = <init>;`
            // (с временного Weak это валидно). Прочие - конструкторный стиль `name(<init>);`.
            const bool addrInit = var_node.m_initializer && var_node.m_initializer->kind() == ParserToken::Kind::RefMakeExpr;
            if (addrInit) {
                m_ectx.m_ctx.source().output_append(output_idx, prefix + cpp_type + " " + var_name + " = ");
                m_driver.emitExpr(var_node.m_initializer.get());
                m_ectx.m_ctx.source().output_append(output_idx, ";");
            } else {
                m_ectx.m_ctx.source().output_append(output_idx, prefix + cpp_type + " " + var_name + "(");
                m_driver.emitExpr(var_node.m_initializer.get());
                m_ectx.m_ctx.source().output_append(output_idx, ");");
            }
        } else if (ref_unique) {
            // Эксклюзивное владение (монопольное). Две формы:
            //  * unique без deleter'а -> trust::StaticUnique<PoT> (inline, zero-cost):
            //    создаётся конструктором со значением `trust::StaticUnique<PoT> name(<init>);`.
            //  * unique с deleter'ом -> trust::Unique<PoT, D> (указательная обёртка).
            // Если инициализатор - сырая C++-вставка `{% %}` (напр. конверсия `$s.to_unique()`),
            // НЕ оборачиваем: вставка уже даёт значение нужного типа.
            const bool embedInit = var_node.m_initializer && var_node.m_initializer->kind() == ParserToken::Kind::EmbedExpr;
            // Имя обёртки StaticUnique - из реестра (не хардкод строкой).
            std::string staticUniqueName;
            if (const auto sid = m_ectx.m_ctx.readTypes().findType(type::StaticUnique); sid.has_value()) {
                if (auto nm = m_ectx.m_ctx.readTypes().printer().getCppTypeName(*sid)) {
                    staticUniqueName = *nm;
                }
            }
            const bool staticUnique = !staticUniqueName.empty() && cpp_type.rfind(staticUniqueName, 0) == 0;
            if (embedInit) {
                m_ectx.m_ctx.source().output_append(output_idx, prefix + cpp_type + " " + var_name + " = ");
                m_driver.emitExpr(var_node.m_initializer.get());
                m_ectx.m_ctx.source().output_append(output_idx, ";");
            } else if (staticUnique) {
                m_ectx.m_ctx.source().output_append(output_idx, prefix + cpp_type + " " + var_name + "(");
                m_driver.emitExpr(var_node.m_initializer.get());
                m_ectx.m_ctx.source().output_append(output_idx, ");");
            } else {
                m_ectx.m_ctx.source().output_append(output_idx, prefix + cpp_type + " " + var_name + " = trust::make_unique<" + ref_pointee_cpp + ">(");
                m_driver.emitExpr(var_node.m_initializer.get());
                m_ectx.m_ctx.source().output_append(output_idx, ");");
            }
        } else {
            // Локальная переменная без инициализатора (`x:Type := _;`): маркер `_` в позиции
            // инициализатора означает «объявить без значения». Эмитим определение без
            // инициализатора `T c_x;` (НЕ extern). Анализатор гарантирует запись до чтения,
            // иначе - диагностика Error (чтение неинициализированной переменной).
            const bool noInitDef = noneInit; // var_node.m_initializer - маркер `_`
            if (noInitDef) {
                m_ectx.m_ctx.source().output_append(output_idx, prefix + cpp_type + " " + var_name + ";");
            } else {
                m_ectx.m_ctx.source().output_append(output_idx, prefix + cpp_type + " " + var_name + " = ");
                m_driver.emitExpr(var_node.m_initializer.get());
                m_ectx.m_ctx.source().output_append(output_idx, ";");
            }
        }
        // Trust-условия переменной (--solver-mode=assert): проверка сразу после объявления/инициализации.
        if (!var_node.m_trust.empty()) {
            m_ectx.m_ctx.source().output_append(output_idx, "\n"); // проверка - на отдельной строке
            m_driver.m_contract.emitTrustChecks(var_node.m_trust);
        }
        // Тип-условия (тип с trust_assert): при создании значения типа (объявление переменной
        // этого типа) проверяем значение - имя типа подставляется как значение переменной.
        // Источник условий - узел декларации типа (VarDecl::m_typeDecl, ставит семантика);
        // trust-имя типа - из аннотации переменной. Без копий и без карт.
        if (var_node.m_typeDecl && var_node.m_initializer && !noneInit && !var_node.m_typeDecl->m_trust.empty()) {
            m_driver.m_contract.emitTypeTrustChecks(var_node.m_typeDecl->m_trust, requiredText(*var_node.m_type), var_name);
        }
    }

    // Добавляем маппинг имени переменной для hover-ссылок.
    // Диапазон trust-имени: nameRange() хранит диапазон реального имени (важно при
    // макро-раскрытии, где range() самого узла - оператор). Fallback - имя в начале range().
    MapperRange trustNameRange = var_node.nameRange();
    // Fallback на range() узла (имя в начале range) - только если range() валиден, иначе
    // makeLoc с невалидным fileIdx упадёт. Финальную проверку делает mapDeclaredName.
    if (trustNameRange.isInvalid() && !var_node.range().isInvalid()) {
        MapperLocation trustNameBegin = m_ectx.m_ctx.source().makeLoc(var_node.range().begin.fileIdx(), var_node.range().begin.offset());
        MapperLocation trustNameEnd = m_ectx.m_ctx.source().makeLoc(
            trustNameBegin.fileIdx(), trustNameBegin.offset() + static_cast<uint32_t>(utils::stripNativePrefix(var_node.text()).size()));
        trustNameRange = MapperRange(trustNameBegin, trustNameEnd);
    }
    // Имя выводится сразу после префикса "<cpp_type> " (или "extern <cpp_type> " для forward).
    // trust-имя в маппинге - исходное (var_node.text(), для нативных с '%'), cpp-имя - манглированное.
    mapDeclaredName(output_idx, trustNameRange, namePrefixLen, var_node.text(), var_name);
}

} // namespace trust
