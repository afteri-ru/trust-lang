// Generated: src/transpiler/decl_emit.cpp
#include "transpiler/decl_emit.hpp"
#include "transpiler/transpiler.hpp"
#include "transpiler/emit_common.hpp"
#include "ast/ast_nodes.hpp"
#include "attrs/attr_builtin.hpp"
#include "ast/ref_syntax.hpp"
#include "ast/ident_name.hpp"
#include "ast/kind_visitor.hpp"
#include "ast/token_type.hpp"
#include "session/context.hpp"
#include "diag/registry.hpp"
#include "diag/base_diags.hpp"
#include "analysis/symbol_table.hpp"
#include "syntax/term.h"
#include "types/registry.hpp"
#include "types/ref_type.hpp"
#include "types/runtime_symbols.hpp"
#include "types/intrinsics.hpp"
#include "types/type_id.hpp"
#include "types/type_names.hpp"
#include "transpiler/diag.hpp"
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

    // Манглинг trust-имени в C++-идентификатор (срез '%' у нативных имён).
    std::string var_name = utils::name_to_cpp(var_node.text());
    if (var_name.empty()) {
        return;
    }

    // Семантический тип переменной и его RefType. Единый источник для C++-имени и формы
    // инициализации reference-wrapper (trust::Shared/Weak/Take): семантика применила reftype
    // и с узла переменной (ведущий @[reftype(...)@]), и с аннотации типа (`x : @[reftype(...)@] T`).
    // Локальные символы в транспиляторе НЕ резолвятся (скоуп-стек сброшен к глобальному),
    // поэтому RefType читаем из атрибутов узла переменной и аннотации типа (как emitTypeNameForNode).
    RefType var_rt = RefType::kValue;
    std::string sync_policy;                 // 2-й аргумент reftype: имя класса синхронизации (пусто = обычный shared)
    TypeId sync_policy_id = INVALID_TYPE_ID; // РЕАЛЬНЫЙ тип политики в реестре (для типа/бэкенда)
    TypeId impl_id = INVALID_TYPE_ID;        // 3-й аргумент reftype: класс реализации механизма доступа (входит в тип)
    const AttrPool& attrs = m_ectx.m_ctx.attrs();
    const auto apply_reftype_attr = [&](const AstNodeAttr* a) {
        if (!a) {
            return;
        }
        const auto rid = attrs.lookup(attr::Reftype);
        if (!rid.has_value() || !a->has_attr(*rid)) {
            return;
        }
        const auto* args = a->attr_args(*rid);
        if (!args || args->empty()) {
            return;
        }
        if (auto rk = refKindFromAttrArgs(args)) {
            var_rt = *rk;
        }
        // Расширенная форма @[reftype("shared", <policy>)]. Политика - РЕАЛЬНЫЙ тип реестра
        // (резолв по имени); в тип попадает через applyRefType (backend shared/weak).
        if (args->size() > 1) {
            sync_policy = args->at(1);
            if (auto pid = m_driver.m_type.resolveTypeIdByName(sync_policy)) {
                sync_policy_id = *pid;
            }
        }
        // 3-й аргумент (опциональный) - класс реализации механизма доступа (часть типа).
        if (args->size() > 2) {
            if (auto iid = m_driver.m_type.resolveTypeIdByName(args->at(2))) {
                impl_id = *iid;
            }
        }
    };
    apply_reftype_attr(var_node.m_type ? var_node.m_type->as_attr() : nullptr);
    apply_reftype_attr(&var_node);
    // Символический сигл ссылочного типа (`x : &Int32` / `*Int32` / `&?Int32`): аннотация - узел
    // RefMakeExpr (`&`/`&?`) или RefTakeExpr (`*`, грамматика COLON STAR NAME → TAKE) с единственным
    // ребёнком - pointee-типом. Вид - из сигла (единый разбор ref-вида, см. ast/ref_syntax.hpp),
    // базовое имя типа - из ребёнка (для resolveTypeIdByName). Покрывает и `@[reftype(...)@]`-форму.
    const AstNodeBase* sigil_pointee = nullptr;
    if (var_node.m_type && (var_node.m_type->kind() == ParserToken::Kind::RefMakeExpr || var_node.m_type->kind() == ParserToken::Kind::RefTakeExpr)) {
        if (const auto rk = refKindOfTypeNode(var_node.m_type.get())) {
            var_rt = *rk;
            const auto& seq = static_cast<const Sequence&>(*var_node.m_type);
            if (!seq.m_body.empty()) {
                sigil_pointee = seq.m_body[0].get();
            }
        }
    }
    const bool ref_wrapper = (var_rt == RefType::kShared || var_rt == RefType::kWeak || var_rt == RefType::kLocker);
    const bool ref_unique = (var_rt == RefType::kUnique);

    // Deleter внешнего ресурса (@[deleter(D)]): C++-имя D (функтор). При наличии создание идёт
    // через `<Wrapper>::adopt(handle, D{})` (см. types/REFType.md §9.2). Источник - узел переменной
    // ИЛИ аннотация типа (как reftype). Семантика уже провалидировала вид/D (unique/shared).
    std::string deleter_cpp;
    std::optional<TypeId> deleter_id;
    const auto apply_deleter_attr = [&](const AstNodeAttr* a) {
        if (!a) {
            return;
        }
        const auto did = attrs.lookup(attr::Deleter);
        if (!did.has_value() || !a->has_attr(*did)) {
            return;
        }
        const auto* args = a->attr_args(*did);
        if (!args || args->size() != 1 || args->front().empty()) {
            return;
        }
        if (auto tid = m_driver.m_type.resolveTypeIdByName(args->front())) {
            if (auto nm = m_driver.m_type.emitTypeName(*tid, args->front())) {
                deleter_cpp = *nm;
                deleter_id = *tid;
            }
        }
    };
    apply_deleter_attr(var_node.m_type ? var_node.m_type->as_attr() : nullptr);
    apply_deleter_attr(&var_node);

    // Determine type: типизированное имя → тип аннотации; нетипизированное → выведенный
    // анализатором конкретный тип (inferred join по истории присвоений), иначе std::any.
    std::string cpp_type;
    // Базовый (pointee) TypeId и его C++-имя для ссылочных переменных (shared/weak/unique):
    // нужны для trust::make_unique<PoT> у эксклюзивного владения и для уникальной эмиссии инициализатора.
    std::optional<TypeId> ref_base_id;
    std::string ref_pointee_cpp;
    const bool typed = var_node.m_type && (var_node.m_type->kind() == ParserToken::Kind::TypeName || sigil_pointee != nullptr);
    if (typed) {
        // Reference-wrapper (shared/weak/take): C++-имя из базового типа аннотации + применённый
        // reftype. Базовый тип берём из реестра (resolveTypeIdByName по имени), reftype - из
        // атрибутов (аннотации m_type ИЛИ ведущего @[reftype(...)@] на узле переменной) -
        // единообразно для обеих форм. Для остальных типов - emitTypeNameForNode (нативные
        // шаблоны, размерности массивов, const/reftype атрибуты самой аннотации).
        if (ref_wrapper || ref_unique) {
            const AstNodeBase* base_node = sigil_pointee ? sigil_pointee : var_node.m_type.get();
            ref_base_id = m_driver.m_type.resolveTypeIdByName(base_node->text());
            if (!ref_base_id.has_value()) {
                m_ectx.m_ctx.report(base_node->range(), diag::DiagId::ParseError, "unable to generate C++ type '{}'", base_node->text());
                return;
            }
            // Deleter внешнего ресурса участвует в ТИПЕ только для unique (D - часть типа):
            // shared сохраняет тип `trust::Shared<T>` (D стирается, применяется при adopt).
            const bool typed_deleter = deleter_id.has_value() && var_rt == RefType::kUnique;
            // Deleter (unique) и sync-политика (shared/weak) - ЧАСТЬ типа: применяем их как
            // backend (структурный узел с реальными TypeId-детьми). Без них - обычный вид.
            const TypeId applied =
                m_ectx.m_ctx.types().applyRefType(*ref_base_id, var_rt, typed_deleter ? *deleter_id : INVALID_TYPE_ID, sync_policy_id, impl_id);
            // displayName - имя ТИПА (не переменной): для ссылочного узла над алиасом/перечислением
            // C++-имя строится по написанному имени типа.
            auto nm = m_driver.m_type.emitTypeName(applied, base_node->text());
            if (!nm || nm->empty()) {
                m_ectx.m_ctx.report(base_node->range(), diag::DiagId::ParseError, "unable to generate C++ type '{}'", base_node->text());
                return;
            }
            cpp_type = std::move(*nm);
            // Pointee C++-имя (для trust::make_unique<PoT> у эксклюзивного владения).
            // displayName - имя типа pointee (для алиасов C++-имя строится «как написано»).
            if (auto pn = m_driver.m_type.emitTypeName(*ref_base_id, base_node->text())) {
                ref_pointee_cpp = *pn;
            } else {
                ref_pointee_cpp = "std::any";
            }
        } else if (isNativeRefKind(var_rt)) {
            // Нативные (сырые) ссылки (`%&`/`%*`): аннотация `%& Int32`/`%* Int32` - узел
            // RefMakeExpr (не TypeName), который emitTypeNameForNode не рендерит. Имя строим как
            // в wrapper-ветке: база из pointee (resolveTypeIdByName) + применённый вид (applyRefType):
            // kRef → "int32_t&", kPtr → "int32_t*".
            const AstNodeBase* base_node = sigil_pointee ? sigil_pointee : var_node.m_type.get();
            ref_base_id = m_driver.m_type.resolveTypeIdByName(base_node->text());
            if (!ref_base_id.has_value()) {
                m_ectx.m_ctx.report(base_node->range(), diag::DiagId::ParseError, "unable to generate C++ type '{}'", base_node->text());
                return;
            }
            auto nm = m_driver.m_type.emitTypeName(m_ectx.m_ctx.types().applyRefType(*ref_base_id, var_rt), base_node->text());
            if (!nm || nm->empty()) {
                m_ectx.m_ctx.report(base_node->range(), diag::DiagId::ParseError, "unable to generate C++ type '{}'", base_node->text());
                return;
            }
            cpp_type = std::move(*nm);
        } else {
            cpp_type = m_driver.m_type.emitTypeNameForNode(var_node.m_type.get());
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
        TypeId inferred = var_node.inferredType;
        // Страховка: forward-объявление, чей тип семантика не пометила (напр. stdlib/Any не
        // зарегистрирован), мог быть завершён последующим определением - добиваем по символу.
        if (inferred == INVALID_TYPE_ID && m_ectx.m_resolvedTypes) {
            if (const Symbol* s = m_ectx.m_resolvedTypes->resolve(var_node.text())) {
                inferred = s->type;
            }
        }
        if (inferred == INVALID_TYPE_ID) {
            if (var_node.m_initializer) {
                m_ectx.m_ctx.report(var_node.range(), diag::DiagId::ParseError, "unable to infer type for variable '{}'", var_node.text());
            } else {
                m_ectx.m_ctx.report(var_node.range(), diag::DiagId::ParseError, "unable to generate C++ type '{}'", type_generic::Any);
            }
            return;
        }
        // Ссылочная переменная без явного типа (`&& x := 5`): inferred уже несёт полный ссылочный тип
        // (semantic обернул pointee видом из префиксного сигла). Базу (pointee) берём из типа.
        if (ref_wrapper || ref_unique) {
            const TypeRegistry& reg = m_ectx.m_ctx.types();
            const TypeId pointee = reg.getPointeeType(inferred);
            if (pointee != INVALID_TYPE_ID) {
                ref_base_id = pointee;
            } else {
                ref_base_id = inferred; // fallback: inferred и есть pointee
            }
            if (auto pn = m_driver.m_type.emitTypeName(*ref_base_id, reg.getFullTypeName(*ref_base_id))) {
                ref_pointee_cpp = *pn;
            } else {
                ref_pointee_cpp = "std::any";
            }
        }
        std::optional<std::string> name = m_driver.m_type.emitTypeName(inferred, var_node.text());
        if (!name || name->empty()) {
            m_ectx.m_ctx.report(var_node.range(), diag::DiagId::ParseError, "unable to generate C++ type '{}'", type_generic::Any);
            return;
        }
        cpp_type = std::move(*name);
    }

    // Синхронизированная ссылка (backend): @[reftype("shared"/"weak", <policy>)].
    // Анализ УЖЕ сохранил политику в ТИПЕ (RefTypeData::accessPolicyType), поэтому C++-имя
    // (`trust::AccessShared<T,Policy>` / `trust::Weak<...>`) строит getCppTypeName по РЕАЛЬНЫМ типам
    // реестра. Здесь политика нужна только для КОНСТРУИРОВАНИЯ (2-й аргумент AccessShared).
    // Пер-объектный таймаут детектора в атрибуте НЕ задаётся (настройка глобальная: дефолт +
    // -fsync-deadlock= + --trust:fsync-deadlock=); при необходимости - разный impl или конструктор.
    const bool is_sync = (var_rt == RefType::kShared || var_rt == RefType::kWeak) && !sync_policy.empty();
    if (is_sync && sync_policy_id == INVALID_TYPE_ID) {
        // Политику валидирует семантика (applyRefAttrs); сюда попадаем лишь при рассогласовании -
        // явная диагностика (без тихого fallback).
        m_ectx.m_ctx.report(var_node.range(), diag::DiagId::ParseError, "unknown access policy type '{}'", sync_policy);
        return;
    }

    // Инклуды типа не нужны здесь: emitTypeName отметил тип (m_ectx.m_usedTypes), инклуды будут
    // сформированы из них ПОСЛЕ обхода AST (collectTypeIncludes).

    MapperScope scope(m_ectx.m_ctx.source(), var_node.range(), output_idx);
    // Константность ОБЪЯВЛЕНИЯ переменной - attr::ReadOnly на узле ('^' на имени или
    // @[readonly@]). НЕ берётся из бита Symbol::type: переменная может стать константной
    // позже (became-const, `x := 42; x^ += 1;`), но её ДЕКЛАРАЦИЯ обязана остаться не-const
    // (переменная мутировалась до финализации). Признак на узле = const «в типе» объявления.
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
    if (!var_node.m_initializer || m_ectx.m_forwardDeclOnly) {
        m_ectx.m_ctx.source().output_append(output_idx, "extern " + prefix + cpp_type + " " + var_name + ";");
        namePrefixLen += 7; // strlen("extern ")
    } else {
        // Reference-wrapper (trust::Shared/Weak/Take): explicit-конструкторы, поэтому инициализируем
        // конструкторным стилем `cpp_type name(<init>);` (покрывает и создание нового объекта
        // `Shared<int>(5)`, и копирование из существующей ссылки). Прочие типы - как раньше.
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
            if (const auto sid = m_ectx.m_ctx.types().findType(type::StaticUnique); sid.has_value()) {
                if (auto nm = m_ectx.m_ctx.types().getCppTypeName(*sid)) {
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
            m_driver.m_contract.emitTypeTrustChecks(var_node.m_typeDecl->m_trust, var_node.m_type->text(), var_name);
        }
    }

    // Добавляем маппинг имени переменной для hover-ссылок.
    // Диапазон trust-имени: nameRange() берёт диапазон реального имени из m_term->m_left
    // (важно при макро-раскрытии, где range() самого узла - оператор). Fallback - имя
    // в начале range() (когда range() покрывает всю строку, m_term->m_left отсутствует).
    MapperRange trustNameRange = var_node.nameRange();
    // Fallback на range() узла (имя в начале range) - только если range() валиден, иначе
    // makeLoc с невалидным fileIdx упадёт. Финальную проверку делает mapDeclaredName.
    if (trustNameRange.isInvalid() && !var_node.range().isInvalid()) {
        MapperLocation trustNameBegin = m_ectx.m_ctx.source().makeLoc(var_node.range().begin.fileIdx(), var_node.range().begin.offset());
        MapperLocation trustNameEnd = m_ectx.m_ctx.source().makeLoc(
            trustNameBegin.fileIdx(), trustNameBegin.offset() + static_cast<uint32_t>(utils::strip_native_prefix(var_node.text()).size()));
        trustNameRange = MapperRange(trustNameBegin, trustNameEnd);
    }
    // Имя выводится сразу после префикса "<cpp_type> " (или "extern <cpp_type> " для forward).
    // trust-имя в маппинге - исходное (var_node.text(), для нативных с '%'), cpp-имя - манглированное.
    mapDeclaredName(output_idx, trustNameRange, namePrefixLen, var_node.text(), var_name);

    // Экспортируются ОПРЕДЕЛЕНИЯ на верхнем уровне модуля в НЕ анонимной области имён
    // (глобальная '::' и именованные 'ns::' - с квалификацией, напр. "ns::x").
    // Локальные (внутри функций/блоков кода), скрытая область '_' и forward-объявления
    // (нет инициализатора → нет определения, `&::name` не связался бы) - не экспортируются.
    if (var_node.m_initializer && !var_name.empty() && !m_ectx.m_inCppBlock && m_ectx.m_hiddenNamespaceDepth == 0) {
        m_ectx.m_exports.push_back({std::string(var_node.text()), m_ectx.qualifiedCppName(var_name), buildTrustForwardDecl(var_node)});
    }
}

} // namespace trust
