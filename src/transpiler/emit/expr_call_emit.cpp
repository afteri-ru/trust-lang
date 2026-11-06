// ExprEmitter: Вызов функции/метода: аргументы, шаблонные аргументы, format-строки, интринсики.

#include "transpiler/emit/expr_emit.hpp"
#include "transpiler/transpiler.hpp"
#include "transpiler/emit/emit_common.hpp"
#include "ast/ast_nodes.hpp"
#include "attrs/attr_builtin.hpp"
#include "ast/ident_name.hpp"
#include "ast/kind_dispatch.hpp"
#include "ast/token_type.hpp"
#include "diag/registry.hpp"
#include "diag/base_diags.hpp"
#include "syntax/term/term.h"
#include "types/type_system.hpp"
#include "types/call.hpp"
#include "target/runtime_symbols.hpp"
#include "types/intrinsics.hpp"
#include "types/type_id.hpp"
#include "transpiler/emit/diag.hpp"
#include "utils/strings.hpp"

#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace trust {

namespace {

/// Эмитит `callee(args)` с опциональной защитой стека: `(guard(), callee(args))`.
/// ЕДИНАЯ точка формы вызова для конструкторов (нативный класс/нативный шаблон/record-шаблон/
/// второе имя типа): обёртка `(..., ...)` не дублируется в каждой ветке.
template <typename EmitArgs>
void emitGuardedCall(CppEmitContext& ectx, const std::string& guard, std::string_view callee, EmitArgs&& emitArgs) {
    if (!guard.empty()) {
        ectx.m_services.source().output_append(ectx.m_output.out, "(");
    }
    ectx.m_services.source().output_append(ectx.m_output.out, callee);
    ectx.m_services.source().output_append(ectx.m_output.out, "(");
    emitArgs();
    ectx.m_services.source().output_append(ectx.m_output.out, ")");
    if (!guard.empty()) {
        ectx.m_services.source().output_append(ectx.m_output.out, ")");
    }
}

} // namespace

void ExprEmitter::emitCastCallArgs(const CallExpr& call) {
    const size_t n = call.boundArgs.has_value() ? call.boundArgs->size() : (call.m_args ? call.m_args->size() : 0);
    // Решение о приведении принято СЕМАНТИКОЙ и записано на узле: эмиттер не пересчитывает
    // его из FunctionTypeData (иначе C++ мог бы выбрать другую перегрузку, чем анализатор).
    EXPECT(call.argCasts.size() == n && "operator dispatch: semantic must materialize argument casts");
    for (size_t i = 0; i < n; ++i) {
        if (i > 0) {
            m_ectx.m_services.source().output_append(m_ectx.m_output.out, ", ");
        }
        const AstNodeBase* arg = nullptr;
        if (call.boundArgs.has_value()) {
            arg = (*call.boundArgs)[i];
        } else {
            const auto& node = (*call.m_args)[i];
            EXPECT(node != nullptr && "operator () dispatch: missing argument");
            if (node->kind() == ParserToken::Kind::ArgNode) {
                const auto& an = static_cast<const ArgNode&>(*node);
                EXPECT(an.m_value != nullptr && "operator () dispatch: argument wrapper has no value");
                arg = an.m_value.get();
            } else {
                arg = node.get();
            }
        }
        EXPECT(arg != nullptr && "operator () dispatch: missing argument");
        // INVALID - параметр Any/неизвестен: приводить некуда (нативная функция без аннотации).
        const TypeId target = call.argCasts[i];
        if (target == INVALID_TYPE_ID) {
            m_driver.emitExpr(arg);
        } else {
            emitCastOperand(arg, target);
        }
    }
}

// CallExpr - только выражение: callee(args). Statement-позицию оборачивает SemicolonStmt.
void ExprEmitter::visit_CallExpr(const CallExpr& n) {
    // Интринсик языка (например `trust::intrinsic_assert`): разворачивается на этапе генерации
    // (emitIntrinsic), а не эмитится как обычный вызов (см. types/intrinsics.hpp).
    if (n.m_callee) {
        // Интринсики и явные функции контроля стека (`trust_stack_check*`) разворачиваются
        // на этапе генерации (emitIntrinsic), а не эмитятся как обычный вызов.
        if (const auto id = findIntrinsicByName(requiredText(*n.m_callee))) {
            emitIntrinsic(*id, n);
            return;
        }
    }
    // Строка-формат: `"{}"(args)` / `'{}'(args)` - callee строковый литерал → std::format.
    if (n.m_callee && (n.m_callee->kind() == ParserToken::Kind::StrWide || n.m_callee->kind() == ParserToken::Kind::StrChar)) {
        emitFormatCall(n);
        return;
    }
    // Рантайм-функции (print/assert): заголовки и C++-имя по callee (источник-имя или C++-имя).
    // C++-имя печатается из реестра (`runtimeSymbolName`), trust-имя не манглируется.
    std::optional<RuntimeSymbolId> runtimeSymbol;
    if (n.m_callee && n.m_callee->kind() == ParserToken::Kind::Ident) {
        runtimeSymbol = findRuntimeSymbolByName(requiredText(*n.m_callee));
        if (runtimeSymbol) {
            m_driver.m_type.recordRuntimeSymbolHeaders(*runtimeSymbol);
        }
    }
    // Контроль переполнения стека: если callee - защищаемая функция (@[stack_check]*), оборачиваем
    // вызов в comma-выражение `(check(), call)`, чтобы вставка работала и в выражении, и в statement.
    const std::string guard = stackCheckExpr(n);
    if (!guard.empty()) {
        m_driver.m_type.recordRequiredInclude("@trust/stack_check.hpp");
        m_ectx.m_services.source().output_append(m_ectx.m_output.out, "(");
        m_ectx.m_services.source().output_append(m_ectx.m_output.out, guard);
        m_ectx.m_services.source().output_append(m_ectx.m_output.out, ", ");
    }
    // Перегруженный оператор `()` (member): `(obj)(static_cast<...>(args...))`. Выбор сделан
    // семантикой; C++ не должен разрешать набор повторно.
    if (n.resolvedOperatorSignature != INVALID_TYPE_ID) {
        m_ectx.m_services.source().output_append(m_ectx.m_output.out, "(");
        m_driver.emitExpr(n.m_callee.get());
        m_ectx.m_services.source().output_append(m_ectx.m_output.out, ")(");
        emitCastCallArgs(n);
        m_ectx.m_services.source().output_append(m_ectx.m_output.out, ")");
        if (!guard.empty()) {
            m_ectx.m_services.source().output_append(m_ectx.m_output.out, ")");
        }
        return;
    }
    // Конструктор НАТИВНОГО шаблона-типа (`Iterator<T>(...)`): callee - абстрактный native-шаблон,
    // а n.resultType - конкретная инстанциация (Iterator<Int32>). Эмитим C++-имя инстанциации
    // (trust::Iterator<int32_t>) + on-use инклуд; без манглинга `c_Iterator`.
    const auto calleeType = [&]() -> TypeId {
        if (!n.m_callee || n.m_callee->kind() != ParserToken::Kind::Ident) {
            return INVALID_TYPE_ID;
        }
        return n.m_callee->as<IdentName>()->resolvedTypeId;
    }();
    if (calleeType != INVALID_TYPE_ID && n.resultType == INVALID_TYPE_ID && m_ectx.m_services.readTypes().store().isNativeTemplateType(calleeType)) {
        m_ectx.m_services.report(n.range(), diag::DiagId::ParseError,
                            "constructor of native template '{}' requires an explicit target type (e.g. 'it : Iterator<Int32> := {}(...);')",
                            requiredText(*n.m_callee), requiredText(*n.m_callee));
        return;
    }
    if (calleeType != INVALID_TYPE_ID && n.resultType != INVALID_TYPE_ID) {
        if (m_ectx.m_services.readTypes().store().isNativeTemplateType(calleeType) && !m_ectx.m_services.readTypes().store().isNativeTemplateType(n.resultType)) {
            m_ectx.m_services.report(n.range(), diag::DiagId::ParseError, "constructor of native template '{}' must produce a native template instance, got '{}'",
                                requiredText(*n.m_callee), m_ectx.m_services.readTypes().printer().typeNameOf(n.resultType));
            return;
        }
        if (m_ectx.m_services.readTypes().store().isNativeTemplateType(calleeType) && m_ectx.m_services.readTypes().store().isNativeTemplateType(n.resultType)) {
            auto cpp = m_driver.m_type.emitTypeName(getUnqualifiedType(n.resultType));
            if (cpp && !cpp->empty()) {
                m_driver.m_type.recordUsedType(n.resultType);
                emitGuardedCall(m_ectx, guard, *cpp, [&] { emitCallArgValues(n); });
                return;
            }
        }
    }
    // Конструктор НАТИВНОГО класса: `MyStr(...)` - callee - имя класса, зарегистрированного как
    // нативный класс (ClassDecl, Group::kNativeClass). Эмитим нативное C++-имя `std::string(...)`
    // (без мэнглинга `c_MyStr`, т.к. C++-struct НЕ генерируется) + инклуд класса on-use.
    if (calleeType != INVALID_TYPE_ID) {
        if (m_ectx.m_services.readTypes().store().isNativeClassType(calleeType)) {
            const std::string_view cpp = m_ectx.cppFactsView().declaredCppName(calleeType);
            if (!cpp.empty()) {
                m_driver.m_type.recordUsedType(calleeType); // инклуд @[include] on-use
                emitGuardedCall(m_ectx, guard, cpp, [&] { emitCallArgValues(n); });
                return;
            }
        }
    }
    // Конструктор пользовательского record-шаблона (`Box(...)`): семантика записала инстанциацию
    // в `CallExpr::resultType` (по типу-цели объявления/присваивания) → эмитим `c_Box<int32_t>(args)`
    // (в т.ч. ПУСТОЙ список). Без типа-цели - обычный путь (CTAD `c_Box(5)` для непустых).
    if (n.resultType != INVALID_TYPE_ID) {
        auto cpp = m_driver.m_type.emitTypeName(getUnqualifiedType(n.resultType));
        if (cpp && !cpp->empty()) {
            emitGuardedCall(m_ectx, guard, *cpp, [&] { emitCallArgValues(n); });
            return;
        }
    }
    // Конструктор по ВТОРОМУ имени типа (`:A ::- T;` → `A(...)`): собственного C++-типа у алиаса
    // нет, поэтому имя берётся из РАЗРЕШЁННОГО типа, а не из spelling callee (`c_A` не существует).
    if (n.resultType == INVALID_TYPE_ID && calleeType != INVALID_TYPE_ID && m_ectx.m_services.readTypes().store().fieldsOf(calleeType) != nullptr) {
        auto cpp = m_driver.m_type.emitTypeName(getUnqualifiedType(calleeType));
        if (cpp && !cpp->empty()) {
            emitGuardedCall(m_ectx, guard, *cpp, [&] { emitCallArgValues(n); });
            return;
        }
    }
    // Перегруженный вызов: эмитим ВЫБРАННУЮ семантикой перегрузку по её уникальному C++-имени
    // (`c_f_P5Int32_`), БЕЗ опоры на C++-разрешение (иначе литералы/конверсии дают другую перегрузку).
    // Прямой вызов свободной системной функции (`__eq__(a,b)`): эмитится как C++-оператор
    // (`operator==(a,b)`), а не как несуществующая C++-функция `__eq__` (идентификаторы с `__`
    // зарезервированы). При перегрузке аргументы приводятся к выбранной сигнатуре.
    const bool calleeIsIdent = n.m_callee && n.m_callee->kind() == ParserToken::Kind::Ident;
    if (calleeIsIdent) {
        const std::string_view calleeName = requiredText(*n.m_callee);
        if (const op::OperatorInfo* opinfo = op::findByFunctionName(calleeName); opinfo != nullptr && opinfo->allowFree) {
            if (!guard.empty()) {
                m_ectx.m_services.source().output_append(m_ectx.m_output.out, "(");
            }
            m_ectx.m_services.source().output_append(m_ectx.m_output.out, m_ectx.cppName(calleeName, CppEntityKind::kOperator) + "(");
            EXPECT(n.resolvedSignature != INVALID_TYPE_ID && "free operator call: semantic must resolve a signature");
            emitCastCallArgs(n);
            m_ectx.m_services.source().output_append(m_ectx.m_output.out, ")");
            if (!guard.empty()) {
                m_ectx.m_services.source().output_append(m_ectx.m_output.out, ")");
            }
            return;
        }
    }
    // Нативный импорт-алиас: ключ — ОБЪЯВЛЕНИЕ callee (`resolvedDecl`), а не текст (одноимённые
    // импорты разных модулей дают разные записи).
    const IdentName* calleeIdentName = calleeIsIdent ? n.m_callee->as<IdentName>() : nullptr;
    // Статический член нативного класса: C++-имя печатает проекция владельца (visit_Ident),
    // а не манглинг trust-имени; overload-приведения аргументов остаются «нативными».
    const bool nativeStaticCallee = calleeIdentName != nullptr && nativeStaticOwner(*calleeIdentName) != INVALID_TYPE_ID;
    const bool nativeAlias = calleeIdentName != nullptr && !m_ectx.nativeCppNameOf(calleeIdentName->resolvedDecl).empty();
    const bool nativeCall = calleeIsIdent && (nativeStaticCallee || nativeAlias);
    // Имя callee: базовое C++-имя (emitExpr) + identity-суффикс выбранного объявления.
    // Суффикс дописывается напрямую в output: emitExprText пишет только при m_mode.exprDepth > 0
    // (вызов-инструкция `f(...);` имеет depth 0 и потерял бы callee). Нативные имена (символ
    // фиксирован внешней библиотекой) НЕ манглируются.
    // Trust-имя, квалифицированное namespace-контейнером (`ns::f`), эмитится через `cppName`
    // (`ns::f`), а не nameToCpp по всему тексту (иначе `::` попал бы в один сегмент).
    if (calleeIsIdent && !nativeCall && !nativeAlias && calleeType == INVALID_TYPE_ID && utils::isQualifiedName(requiredText(*n.m_callee))) {
        m_ectx.m_services.source().output_append(m_ectx.m_output.out, m_ectx.cppName(requiredText(*n.m_callee), CppEntityKind::kFunction));
    } else if (runtimeSymbol) {
        // C++-имя рантайм-символа — из единого реестра; суффикс/манглинг не применяются.
        m_ectx.m_services.source().output_append(m_ectx.m_output.out, runtimeSymbolName(*runtimeSymbol));
    } else {
        m_driver.emitExpr(n.m_callee.get());
    }
    if (!nativeCall && !runtimeSymbol && calleeIsIdent) {
        std::string_view suffix = n.resolvedCalleeSuffix;
        // Доступ через namespace-алиас/using идёт путём статического члена и не заполняет
        // `resolvedCalleeSuffix`; identity-суффикс функции всё равно берём из объявления.
        if (suffix.empty() && calleeIdentName != nullptr && calleeType == INVALID_TYPE_ID && calleeIdentName->resolvedDecl != nullptr &&
            calleeIdentName->resolvedDecl->is<FuncDecl>()) {
            suffix = m_ectx.m_services.declCppFacts().cppSuffixOf(calleeIdentName->resolvedDecl->as<FuncDecl>()->declId());
        }
        if (!suffix.empty()) {
            m_ectx.m_services.source().output_append(m_ectx.m_output.out, suffix);
        }
    }
    // Generic Trust-функция: явные типовые аргументы (native-import оставляем на C++ deduction).
    if (calleeIsIdent && !nativeAlias && !n.resolvedTemplateArgs.empty()) {
        m_ectx.m_services.source().output_append(m_ectx.m_output.out, "<");
        for (size_t i = 0; i < n.resolvedTemplateArgs.size(); ++i) {
            if (i > 0) {
                m_ectx.m_services.source().output_append(m_ectx.m_output.out, ", ");
            }
            const TypeId tid = n.resolvedTemplateArgs[i];
            auto cpp = m_driver.m_type.emitTypeName(tid);
            if (!cpp) {
                m_ectx.m_services.report(n.range(), diag::DiagId::ParseError, "unable to generate C++ type for template argument");
                return;
            }
            m_ectx.m_services.source().output_append(m_ectx.m_output.out, *cpp);
        }
        m_ectx.m_services.source().output_append(m_ectx.m_output.out, ">");
    }
    m_ectx.m_services.source().output_append(m_ectx.m_output.out, "(");
    // Нативный C++-вызов: имя символа не манглируется, поэтому выбор перегрузки в C++ надо
    // зафиксировать явными приведениями аргументов к объявленным типам (иначе C++ может выбрать
    // другую перегрузку, а тип результата анализатор уже взял из своей). C-линковка/`Any`-
    // параметры приводятся безопасно (Any/INVALID пропускаются).
    if (nativeCall && n.resolvedNativeOverload && n.resolvedSignature != INVALID_TYPE_ID) {
        emitCastCallArgs(n);
        m_ectx.m_services.source().output_append(m_ectx.m_output.out, ")");
    } else {
        emitCallArgs(n);
    }
    if (!guard.empty()) {
        m_ectx.m_services.source().output_append(m_ectx.m_output.out, ")");
    }
}

// Строка-формат `"{}"(args)` / `'{}'(args)` → `std::format(fmt, args...)`. Ширина строки
// задаётся Kind callee: StrWide → L"…" (std::format<wchar_t>), StrChar → "…". Требуется
// #include <format> в генерируемом C++ (записываем через recordRequiredInclude).
void ExprEmitter::emitFormatCall(const CallExpr& n) {
    m_driver.m_type.recordRequiredInclude("#include <format>");
    m_ectx.m_services.source().output_append(m_ectx.m_output.out, "std::format(");
    m_driver.emitExpr(n.m_callee.get());
    if (n.m_args) {
        for (const auto& arg : *n.m_args) {
            m_ectx.m_services.source().output_append(m_ectx.m_output.out, ", ");
            m_driver.emitExpr(arg.get());
        }
    }
    m_ectx.m_services.source().output_append(m_ectx.m_output.out, ")");
}

// ЕДИНАЯ эмиссия аргументов вызова (функции и метода): `a, b, c` + закрывающая ')'. Семейство
// многоточия (`... expr ...`/`...`) раскрыто (материализовано) СЕМАНТИКОЙ (semantic/ellipsis),
// поэтому кодоген видит обычный список аргументов - без логики заполнения.
void ExprEmitter::emitCallArgValues(const CallExpr& call) {
    std::vector<const AstNodeBase*> values;
    if (call.boundArgs.has_value()) {
        values.assign(call.boundArgs->begin(), call.boundArgs->end());
    } else if (call.m_args) {
        values.reserve(call.m_args->size());
        for (const auto& arg : *call.m_args) {
            if (!arg) {
                continue;
            }
            if (arg->kind() == ParserToken::Kind::ArgNode) {
                const auto& an = static_cast<const ArgNode&>(*arg);
                EXPECT(an.m_value != nullptr && "call argument wrapper has no value");
                values.push_back(an.m_value.get());
            } else {
                values.push_back(arg.get());
            }
        }
    }
    emitCommaSeparatedExprs(values);
}

void ExprEmitter::emitCallArgs(const CallExpr& call) {
    emitCallArgValues(call);
    m_ectx.m_services.source().output_append(m_ectx.m_output.out, ")");
}

void ExprEmitter::emitCommaSeparatedExprs(const std::vector<const AstNodeBase*>& values) {
    bool first = true;
    for (const AstNodeBase* value : values) {
        if (value == nullptr) {
            continue;
        }
        if (!first) {
            m_ectx.m_services.source().output_append(m_ectx.m_output.out, ", ");
        }
        first = false;
        m_driver.emitExpr(value);
    }
}

void ExprEmitter::emitIntrinsic(IntrinsicId id, const CallExpr& call) {
    // Требуемые заголовки - единый источник (реестр интринсиков, types/intrinsics.hpp).
    for (const auto h : intrinsicHeaders(id)) {
        m_driver.m_type.recordRequiredInclude(h);
    }
    switch (id) {
    case IntrinsicId::kTrustAssert:
        m_driver.m_contract.emitRuntimeAssertCheck(call);
        return;
    case IntrinsicId::kTrustStackCheck:
        // trust_stack_check(N) -> trust::stack_check::check_overflow(N) (free >= N + reserve).
        if (analysis::stackCheckActive(m_ectx.m_behavioral)) {
            m_ectx.m_services.source().output_append(m_ectx.m_output.out, "trust::stack_check::check_overflow(");
            if (call.m_args && !call.m_args->empty()) {
                m_driver.emitExpr((*call.m_args)[0].get());
            }
            m_ectx.m_services.source().output_append(m_ectx.m_output.out, ")");
        }
        return;
    case IntrinsicId::kTrustStackCheckLimit:
        // trust_stack_check() -> trust::stack_check::check_stack_limit() (free >= m_stack_limit + reserve).
        if (analysis::stackCheckActive(m_ectx.m_behavioral)) {
            m_ectx.m_services.source().output_append(m_ectx.m_output.out, "trust::stack_check::check_stack_limit()");
        }
        return;
    case IntrinsicId::kTrustStackCheckReserve:
        // trust_stack_check_set_reserve(N) -> trust::stack_check::set_reserve(N) (мин. резерв для всех функций).
        if (analysis::stackCheckActive(m_ectx.m_behavioral)) {
            m_ectx.m_services.source().output_append(m_ectx.m_output.out, "trust::stack_check::set_reserve(");
            if (call.m_args && !call.m_args->empty()) {
                m_driver.emitExpr((*call.m_args)[0].get());
            }
            m_ectx.m_services.source().output_append(m_ectx.m_output.out, ")");
        }
        return;
    case IntrinsicId::kStackCheckCall:
    case IntrinsicId::kStackCheckSetReserve:
    case IntrinsicId::kStackCheckGetReserve:
    case IntrinsicId::kStackCheckGetLimit:
    case IntrinsicId::kStackCheckSetLimit:
        // Явные функции контроля стека: раскрытие — в handleStackCheckNative (единый список имён).
        handleStackCheckNative(call);
        return;
    case IntrinsicId::kCount:
        break;
    }
}

} // namespace trust
