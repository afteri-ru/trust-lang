// ExprEmitter: Контейнерные литералы (массив/словарь/кортеж/тензор), доступ по индексу и
// типизированные конструкции.

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
#include "types/names.hpp"
#include "transpiler/emit/diag.hpp"
#include "utils/strings.hpp"

#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace trust {

// Динамический доступ по индексу: d[expr].
void ExprEmitter::visit_ArrayAccess(const Binary& n) {
    if (n.tupleIndex >= 0) {
        emitTupleElementAccess(n);
        return;
    }
    // Тензор `t[i]`, `t[i,j]`, `t[a..b]`: `(obj)(idx...)` / `(obj).slice(a,b,step)`.
    if (n.lhsType.getTypeId() != INVALID_TYPE_ID && m_ectx.m_services.readTypes().store().isTensorType(n.lhsType.getTypeId())) {
        std::vector<const AstNodeBase*> indices;
        for (const auto& idx : n.m_indices) {
            if (idx) {
                indices.push_back(idx.get());
            }
        }
        if (indices.empty() && n.m_right) {
            indices.push_back(n.m_right.get());
        }
        if (indices.size() == 1 && indices.front()->kind() == ParserToken::Kind::RangeExpr) {
            const auto& range = static_cast<const RangeExpr&>(*indices.front());
            if (n.m_left) {
                m_ectx.m_services.source().output_append(m_ectx.m_output.out, "(");
                m_driver.emitExpr(n.m_left.get());
                m_ectx.m_services.source().output_append(m_ectx.m_output.out, ").slice(");
            }
            m_driver.emitExpr(range.start().get());
            m_ectx.m_services.source().output_append(m_ectx.m_output.out, ", ");
            m_driver.emitExpr(range.stop().get());
            if (range.hasStep()) {
                m_ectx.m_services.source().output_append(m_ectx.m_output.out, ", ");
                m_driver.emitExpr(range.step().get());
            }
            m_ectx.m_services.source().output_append(m_ectx.m_output.out, ")");
            return;
        }
        if (n.m_left) {
            m_ectx.m_services.source().output_append(m_ectx.m_output.out, "(");
            m_driver.emitExpr(n.m_left.get());
            m_ectx.m_services.source().output_append(m_ectx.m_output.out, ")(");
        }
        bool first = true;
        for (const AstNodeBase* idx : indices) {
            if (!first) {
                m_ectx.m_services.source().output_append(m_ectx.m_output.out, ", ");
            }
            first = false;
            m_driver.emitExpr(idx);
        }
        m_ectx.m_services.source().output_append(m_ectx.m_output.out, ")");
        return;
    }
    // Оператор индексации пользовательского типа (`__getitem__`): `(obj)[idx]`. При наборе
    // перегрузок индекс приводится к объявленному типу параметра (C++ не выбирает).
    if (n.lhsType.getTypeId() != INVALID_TYPE_ID && m_ectx.m_services.readTypes().store().isRecordType(n.lhsType.getTypeId()) &&
        m_ectx.m_services.readTypes().methods().findMethodInfo(n.lhsType.getTypeId(), op::indexFunctionName()).has_value()) {
        if (n.m_left) {
            m_ectx.m_services.source().output_append(m_ectx.m_output.out, "(");
            m_driver.emitExpr(n.m_left.get());
            m_ectx.m_services.source().output_append(m_ectx.m_output.out, ")[");
        }
        if (n.m_right) {
            if (n.resolvedOperatorSignature != INVALID_TYPE_ID) {
                const auto* fd = m_ectx.m_services.readTypes().store().getTypeDataAs<FunctionTypeData>(n.resolvedOperatorSignature);
                EXPECT(fd != nullptr && !fd->paramTypes.empty() && "operator [] dispatch: expected one parameter");
                emitCastOperand(n.m_right.get(), fd->paramTypes[0]);
            } else {
                m_driver.emitExpr(n.m_right.get());
            }
        }
        m_ectx.m_services.source().output_append(m_ectx.m_output.out, "]");
        return;
    }
    // Доступ к элементу массива `a[i]` (структурный Array-тип): `(obj).at(idx)` -
    // безопасный (bounds-check) доступ, как для словаря. lhsType ставит семантика
    // (resolveArrayAccess).
    if (n.lhsType.getTypeId() != INVALID_TYPE_ID && m_ectx.m_services.readTypes().store().isArrayType(n.lhsType.getTypeId())) {
        if (n.m_left) {
            m_ectx.m_services.source().output_append(m_ectx.m_output.out, "(");
            m_driver.emitExpr(n.m_left.get());
            m_ectx.m_services.source().output_append(m_ectx.m_output.out, ").at(");
        }
        if (n.m_right) {
            m_driver.emitExpr(n.m_right.get());
        }
        m_ectx.m_services.source().output_append(m_ectx.m_output.out, ")");
        return;
    }
    emitDictElementAccess(n);
}

// Кортеж: `t.name` / `t.0` / `t[idx]` → `std::get<index>(obj)`. Индекс резолвит семантика
// (Binary::tupleIndex). std::get возвращает ссылку на конкретный элемент std::tuple.
void ExprEmitter::emitTupleElementAccess(const Binary& n) {
    m_driver.m_type.recordRequiredInclude("#include <tuple>");
    m_ectx.m_services.source().output_append(m_ectx.m_output.out, "std::get<");
    m_ectx.m_services.source().output_append(m_ectx.m_output.out, std::to_string(n.tupleIndex));
    m_ectx.m_services.source().output_append(m_ectx.m_output.out, ">(");
    if (n.m_left) {
        m_driver.emitExpr(n.m_left.get());
    }
    m_ectx.m_services.source().output_append(m_ectx.m_output.out, ")");
}

// namespace
// Литерал массива `[1,2:Int8,3,]` / `[1,2,3,]:Int32` → `std::vector<Elem>{...}` (mut)
// или `std::array<Elem,N>{...}` (константная/фиксированная форма). Тип элемента - единый
// источник семантики: результат анализа ArrayInit (структурный Array<Elem>, resolveCppTypeId).
void ExprEmitter::visit_ArrayInit(const DictLiteralNode& n) {
    if (n.tensorType.getTypeId() != INVALID_TYPE_ID) {
        emitTensorLiteral(n, n.tensorType);
        return;
    }
    if (n.arrayType.getTypeId() == INVALID_TYPE_ID) {
        m_driver.emitPlaceholderExpr(m_ectx.m_output.out);
        return;
    }
    emitArrayLiteral(n, n.arrayType);
}

// Эмитит литерал/конструкцию массива: `std::vector<Elem>{v1, v2, ...}` (mutable) либо
// `std::array<Elem,N>{v1, ...}` (константная форма). Тип контейнера берётся из структурного
// Array<Elem> через resolveCppTypeId (записывает инклуды <vector>/<array>). Элементы - ArgNode
// (значение в m_value); именованные/пустые отбрасываются.
void ExprEmitter::emitArrayLiteral(const DictLiteralNode& n, QualType arrayType) {
    const TypeId arrayId = arrayType.getTypeId();
    // Многомерный массив (несколько размерностей или вложенные литералы): генерация тензора
    // не реализована → диагностика (анализ/регистрация типа при этом работают).
    if (isMultiDimArray(arrayId, m_ectx.m_services.readTypes())) {
        m_ectx.m_services.report(n.range(), diag::DiagId::ParseError, "многомерные массивы пока не реализованы: транслируются как тензоры (LibTorch)");
        m_driver.emitPlaceholderExpr(m_ectx.m_output.out);
        return;
    }
    auto container = m_driver.m_type.emitTypeName(arrayType);
    if (!container) {
        m_driver.emitPlaceholderExpr(m_ectx.m_output.out);
        return;
    }
    // Элементы: семейство многоточия уже раскрыто (МАТЕРИАЛИЗОВАНО) семантикой - обычный список.
    m_ectx.m_services.source().output_append(m_ectx.m_output.out, *container + "{");
    std::vector<const AstNodeBase*> values;
    for (const auto& el : dictElements(n)) {
        if (el.value) {
            values.push_back(el.value);
        }
    }
    emitCommaSeparatedExprs(values);
    m_ectx.m_services.source().output_append(m_ectx.m_output.out, "}");
}

// Тензорный литерал/конструкция: `trust::Tensor<Elem>({d1,d2}, {v1,v2,...})`.
// Вложенные литералы `[[...],[...]]` разворачиваются row-major в плоский список значений.
void ExprEmitter::emitTensorLiteral(const DictLiteralNode& n, QualType tensorType) {
    auto typeName = m_driver.m_type.emitTypeName(tensorType);
    if (!typeName) {
        m_driver.emitPlaceholderExpr(m_ectx.m_output.out);
        return;
    }
    const auto& dims = m_ectx.m_services.readTypes().store().tensorDimensions(tensorType.getTypeId());
    m_ectx.m_services.source().output_append(m_ectx.m_output.out, *typeName + "({");
    for (size_t i = 0; i < dims.size(); ++i) {
        if (i > 0) {
            m_ectx.m_services.source().output_append(m_ectx.m_output.out, ", ");
        }
        m_ectx.m_services.source().output_append(m_ectx.m_output.out, std::to_string(dims[i]));
    }
    m_ectx.m_services.source().output_append(m_ectx.m_output.out, "}, {");
    std::vector<const AstNodeBase*> values;
    const auto collectLeaves = [&](const auto& self, const DictLiteralNode& node) -> void {
        for (const auto& el : dictElements(node)) {
            if (!el.value) {
                continue;
            }
            if (el.value->kind() == ParserToken::Kind::ArrayInit || el.value->kind() == ParserToken::Kind::DictLiteral) {
                self(self, static_cast<const DictLiteralNode&>(*el.value));
                continue;
            }
            values.push_back(el.value);
        }
    };
    collectLeaves(collectLeaves, n);
    emitCommaSeparatedExprs(values);
    m_ectx.m_services.source().output_append(m_ectx.m_output.out, "})");
}

// Литерал словаря/кортежа: `(1, two="2", name=3,)` → trust::Dict{ {"", expr}, {"two", expr}, ... }.
// Контракт: все элементы m_body - Binary(AssignOp) (left=Ident-метка или пустой, right=значение),
// строятся из канонических пар грамматики `args` (term_to_ast::visit_DICT). Тип значения -
// единый источник семантики: Binary::resultType (из exprType), см. emitTypedDictValue.
// Литерал словаря/конструкция/каст и кортеж. kind==Tuple → visit_Tuple (std::tuple);
// типизированный `:Type(...)`/`(...):Type` (не Tuple) → emitTypedConstruction (каст/конструктор);
// голый `(...)` → emitDictLiteralBody (trust::Dict). Контракт элементов - Binary(AssignOp)
// (term_to_ast::visit_DICT); тип значения - единый источник Binary::resultType.
void ExprEmitter::visit_DictLiteral(const DictLiteralNode& n) {
    if (n.tensorType.getTypeId() != INVALID_TYPE_ID) {
        emitTensorLiteral(n, n.tensorType);
        return;
    }
    if (n.m_type) {
        emitTypedConstruction(n);
        return;
    }
    emitDictLiteralBody(n);
}

// Кортеж `:Tuple(...)` / `(...):Tuple` (kind==Tuple, выставлен анализатором по типу из реестра)
// → std::tuple (именованные элементы конвертируются в индексы по порядку; имена в C++ не попадают).
void ExprEmitter::visit_Tuple(const DictLiteralNode& n) {
    m_driver.m_type.recordRequiredInclude("#include <tuple>");
    m_ectx.m_services.source().output_append(m_ectx.m_output.out, "std::make_tuple(");
    std::vector<const AstNodeBase*> values;
    for (const auto& el : dictElements(n)) {
        if (el.value) {
            values.push_back(el.value);
        }
    }
    emitCommaSeparatedExprs(values);
    m_ectx.m_services.source().output_append(m_ectx.m_output.out, ")");
}

// Литерал диапазона `start..stop` / `start..stop..step` → `trust::Range<Elem>(start, stop[, step])`.
// Универсальный тип `:Range` (как `:Dict`): элементный тип Elem - join типов start/stop/step,
// вычисленный семантикой (RangeExpr::elementType из analyzeRangeExpr) и параметризующий шаблон
// trust::Range<T> при кодогенерации. Рациональные значения оборачиваются trust::Rational(...)
// самим visit_RationalLiteral. Записывает @stdlib/builtin/range.hpp (механизм как у visit_Tuple/<tuple>).
void ExprEmitter::visit_RangeExpr(const RangeExpr& node) {
    // range.hpp самодостаточен, но для toDict/элементов нужны dict.hpp и rational.hpp - их
    // пайплайн извлекает из рантайма ТОЛЬКО по прямому запросу (транзитивные инклуды не
    // отслеживаются), поэтому перечисляем весь транзитивный набор (как у Dict-типа).
    m_driver.m_type.recordRequiredInclude("@stdlib/builtin/range.hpp");
    m_driver.m_type.recordRequiredInclude("@stdlib/builtin/dict.hpp");
    m_driver.m_type.recordRequiredInclude("@stdlib/builtin/rational.hpp");
    // Элементный C++-тип - из семантики (join start/stop/step). INVALID → универсальный Any.
    TypeId elemTid = node.elementType.getTypeId();
    if (elemTid == INVALID_TYPE_ID) {
        elemTid = m_ectx.m_services.readTypes().getType(type_generic::Any);
    }
    std::string elemCpp;
    if (auto n = m_driver.m_type.emitTypeName(elemTid)) {
        elemCpp = std::move(*n);
    } else {
        elemCpp = m_driver.m_type.anyTypeName();
    }
    m_ectx.m_services.source().output_append(m_ectx.m_output.out, "trust::Range<" + elemCpp + ">(");
    m_driver.emitExpr(node.start().get());
    m_ectx.m_services.source().output_append(m_ectx.m_output.out, ", ");
    m_driver.emitExpr(node.stop().get());
    if (node.hasStep()) {
        m_ectx.m_services.source().output_append(m_ectx.m_output.out, ", ");
        m_driver.emitExpr(node.step().get());
    }
    m_ectx.m_services.source().output_append(m_ectx.m_output.out, ")");
}

// Тело словаря trust::Dict{ {"name", TypedValue}, ... } - для голого `(...)` и для типизированного
// с аннотацией, резолвящейся в сам Dict.
void ExprEmitter::emitDictLiteralBody(const DictLiteralNode& n) {
    // Имя и заголовки Dict-типа - через emitTypeName (единая точка сбора; без хардкода пути).
    auto dictId = m_ectx.m_services.readTypes().findType(type::Dict);
    if (!dictId) {
        m_driver.emitPlaceholderExpr(m_ectx.m_output.out);
        return;
    }
    auto dictName = m_driver.m_type.emitTypeName(*dictId);
    if (!dictName) {
        m_driver.emitPlaceholderExpr(m_ectx.m_output.out);
        return;
    }
    m_ectx.m_services.source().output_append(m_ectx.m_output.out, *dictName + "{");
    bool first = true;
    for (const auto& el : dictElements(n)) {
        if (!el.value) {
            continue;
        }
        if (!first) {
            m_ectx.m_services.source().output_append(m_ectx.m_output.out, ", ");
        }
        first = false;
        // Имя поля (у безымянного - пустая строка). Экранируем для C++-строки.
        m_ectx.m_services.source().output_append(m_ectx.m_output.out, "{\"" + utils::escapeCppString(el.name) + "\", ");
        // Элемент: TypedValue{kind, значение} - конструктор размещает в быструю ветку variant.
        emitTypedDictValue(el.value, el.resultType);
        m_ectx.m_services.source().output_append(m_ectx.m_output.out, "}");
    }
    m_ectx.m_services.source().output_append(m_ectx.m_output.out, "}");
}

// Типизированная конструкция/каст `:Type(...)`/`(...):Type` (не Tuple). Решение по типу из реестра:
//   - если аннотация резолвится в сам универсальный Dict → обычный словарь (emitDictLiteralBody);
//   - один элемент → каст trust::checked_cast<Type>(v) / trust::any_to<Type>(v);
//   - несколько элементов → конструктор Type(v1, ..., vN).
void ExprEmitter::emitTypedConstruction(const DictLiteralNode& n) {
    const AstNodeBase* typeNode = n.m_type.get();
    if (!typeNode || typeNode->kind() != ParserToken::Kind::TypeName) {
        m_driver.emitPlaceholderExpr(m_ectx.m_output.out);
        return;
    }
    // Конструкция массива `:Array(...)` / `:Array^(...)`: семантика интернировала структурный
    // Array<Elem> (DictLiteralNode::arrayType) → эмитим std::vector<Elem>{...}/std::array<Elem,N>{...}.
    if (n.arrayType.getTypeId() != INVALID_TYPE_ID) {
        emitArrayLiteral(n, n.arrayType);
        return;
    }
    // Аннотация = сам Dict → словарь (не конструктор). Решение — материализованный анализом
    // тип узла аннотации (имя типа в кодогене не резолвится).
    const auto* typeIdent = typeNode->as<IdentType>();
    const TypeId annotationId = typeIdent != nullptr ? typeIdent->resolvedTypeId : INVALID_TYPE_ID;
    if (annotationId != INVALID_TYPE_ID) {
        if (auto dictId = m_ectx.m_services.readTypes().findType(type::Dict);
            dictId && m_ectx.m_services.readTypes().store().desugaredTypeId(annotationId) == m_ectx.m_services.readTypes().store().desugaredTypeId(*dictId)) {
            emitDictLiteralBody(n);
            return;
        }
    }
    // Тип-цель → C++ имя (и запись инклудов типа через emitTypeNameForNode). None/Void → "void".
    std::string typeCpp;
    const std::string_view tt = requiredText(*typeNode);
    if (tt == type::Void || tt == type::None) {
        typeCpp = "void";
    } else {
        typeCpp = m_driver.m_type.emitTypeNameForNode(typeNode);
    }
    if (typeCpp.empty()) {
        m_driver.emitPlaceholderExpr(m_ectx.m_output.out);
        return; // emitTypeNameForNode уже вывел диагностику
    }
    // Операнды (значения элементов) в порядке m_body.
    std::vector<const AstNodeBase*> values;
    for (const auto& el : dictElements(n)) {
        if (el.value) {
            values.push_back(el.value);
        }
    }
    // Явная конструкция record-шаблона `:Box<Int32>(...)`: эмитим `typeCpp(args...)` напрямую -
    // в т.ч. ПУСТОЙ список (default-инициализация), а не каст/placeholder.
    if (m_driver.m_type.isRecordTemplateAnnotation(typeNode)) {
        m_ectx.m_services.source().output_append(m_ectx.m_output.out, typeCpp + "(");
        emitCommaSeparatedExprs(values);
        m_ectx.m_services.source().output_append(m_ectx.m_output.out, ")");
        return;
    }
    if (values.empty()) {
        m_driver.emitPlaceholderExpr(m_ectx.m_output.out);
        return;
    }
    if (values.size() == 1) {
        // Явная типизированная конверсия одного значения:
        //   - источник Any (`std::any` или элемент словаря TypedValue) → `trust::any_to<T>`
        //     (runtime-проверка, без silent fallback; `:Any(expr)` — identity/toAny);
        //   - иначе → `trust::checked_cast<T>` (numeric range check / static_cast).
        const AstNodeBase* operand = values[0];
        const TypeReadModel& reg = m_ectx.m_services.readTypes();
        bool operandIsAny = false;
        if (n.castSourceType.getTypeId() != INVALID_TYPE_ID) {
            // Тип операнда вычислила семантика (analyzeDictLiteral) - единственный надёжный путь
            // для переменной/параметра/результата вызова типа Any.
            operandIsAny = isAnyType(n.castSourceType.getTypeId(), reg);
        } else if (operand && (operand->kind() == ParserToken::Kind::MemberAccess || operand->kind() == ParserToken::Kind::ArrayAccess)) {
            const auto& b = static_cast<const Binary&>(*operand);
            // Вызов метода (obj.m(...)) возвращает КОНКРЕТНОЕ C++-значение нативного члена
            // (напр. Range.at(i) → int64), а НЕ trust::TypedValue: для него any_to неприменим.
            const bool isMethodCall = (b.kind() == ParserToken::Kind::MemberAccess && b.m_right && b.m_right->kind() == ParserToken::Kind::CallExpr);
            if (!isMethodCall) {
                operandIsAny = (b.resultType.getTypeId() == INVALID_TYPE_ID || isAnyType(b.resultType.getTypeId(), reg));
            }
        }
        if (operandIsAny) {
            m_driver.m_type.recordRuntimeSymbolHeaders(RuntimeSymbolId::kAnyTo);
            m_ectx.m_services.source().output_append(m_ectx.m_output.out, "trust::any_to<" + typeCpp + ">(");
            m_driver.emitExpr(operand);
            m_ectx.m_services.source().output_append(m_ectx.m_output.out, ")");
            return;
        }
        m_driver.m_type.recordRuntimeSymbolHeaders(RuntimeSymbolId::kCheckedCast);
        m_ectx.m_services.source().output_append(m_ectx.m_output.out, "trust::checked_cast<" + typeCpp + ">(");
        m_driver.emitExpr(operand);
        m_ectx.m_services.source().output_append(m_ectx.m_output.out, ")");
        return;
    }
    // Конструктор нескольких значений: Type(v1, ..., vN).
    m_ectx.m_services.source().output_append(m_ectx.m_output.out, typeCpp + "(");
    emitCommaSeparatedExprs(values);
    m_ectx.m_services.source().output_append(m_ectx.m_output.out, ")");
}

// Эмитит trust::TypedValue{QualType, значение} для элемента словаря. Тип значения (identity +
// квалификаторы const/inferred) вычислен семантикой и сохранён на элементе.
// Конструктор TypedValue сам размещает значение в быструю ветку std::variant (по группе типа:
// числа/bool/строки) либо в std::any-ветку (открытые типы, вложенный Dict).
// C++-имя выводится из identity того же QualType без дублирования логики диапазонов/маппинга.
// Единый предикат литералов - ast::is_literal_kind.
void ExprEmitter::emitTypedDictValue(const AstNodeBase* valueNode, QualType state) {
    // C++-имя - через emitTypeName (единая точка: резолв + запись инклудов типа). Печатается
    // identity-форма: квалификаторы относятся к типу значения, а не к C++-типу выражения-
    // конструкции (`const T(...)` не нужен).
    auto cpp = m_driver.m_type.emitTypeName(state.getTypeId());

    // toRuntimeType приводит identity к рантайм-форме (у встроенных типов registry_index не
    // входит в ABI-identity: рантайм строит TypeId как TypeId::fromKind). Печатаем полный raw
    // в hex (0x…), чтобы были наглядны разряды.
    const QualType tag = m_driver.m_type.toRuntimeType(state);
    m_ectx.m_services.source().output_append(m_ectx.m_output.out, std::format("trust::TypedValue{{trust::QualType::fromRaw(0x{:x}), ", tag.raw()));
    // Точный C++-тип значения из реестра для литералов. RationalLiteral уже эмитится как
    // trust::Rational(...) - не оборачиваем. Не-литералы (вложенный Dict, переменная, вызов) - как есть.
    if (cpp && !cpp->empty() && is_literal_kind(valueNode->kind()) && valueNode->kind() != ParserToken::Kind::RationalLiteral) {
        m_ectx.m_services.source().output_append(m_ectx.m_output.out, *cpp + "(");
        m_driver.emitExpr(valueNode);
        m_ectx.m_services.source().output_append(m_ectx.m_output.out, ")");
    } else {
        m_driver.emitExpr(valueNode);
    }
    m_ectx.m_services.source().output_append(m_ectx.m_output.out, "}");
}

} // namespace trust
