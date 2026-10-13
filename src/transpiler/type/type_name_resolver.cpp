// src/transpiler/type/type_name_resolver.cpp
#include "transpiler/type/type_name_resolver.hpp"
#include "transpiler/type/type_include_collector.hpp"
#include "transpiler/emit/emit_common.hpp"
#include "ast/ast_nodes.hpp"
#include "attrs/attr_builtin.hpp"
#include "ast/ref_syntax.hpp"
#include "ast/ident_name.hpp"
#include "ast/kind_dispatch.hpp"
#include "ast/token_type.hpp"
#include "session/context.hpp"
#include "diag/registry.hpp"
#include "diag/base_diags.hpp"
#include "syntax/term/term.h"
#include "types/type_system.hpp"
#include "types/types.hpp"
#include "types/runtime_symbols.hpp"
#include "types/intrinsics.hpp"
#include "types/call.hpp"
#include "types/type_id.hpp"
#include "types/names.hpp"
#include "transpiler/emit/diag.hpp"
#include "utils/strings.hpp"
#include <format>
#include <memory>
#include <set>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace trust {

bool TypeNameResolver::isRecordTemplateAnnotation(const AstNodeBase* type_node) const {
    if (!type_node || !type_node->is<IdentType>()) {
        return false;
    }
    const auto* it = type_node->as<IdentType>();
    if (!it->isTemplate() || it->resolvedTypeId == INVALID_TYPE_ID) {
        return false;
    }
    // Аннотация с типовыми аргументами, разрешённая в record-тип, — конструкция record-шаблона.
    return m_ectx.m_ctx.readTypes().store().isRecordType(it->resolvedTypeId);
}

std::optional<std::string> TypeNameResolver::emitTypeName(TypeId type_id) {
    // МЕХАНИЗМ №1 - ПО ТИПУ: resolveCppTypeId только отмечает тип как использованный
    // (recordUsedType → m_ectx.m_accumulators.usedTypes); инклуды из типов формируются ПОСЛЕ обхода AST.
    auto resolved = resolveCppTypeId(type_id);
    if (!resolved) {
        return std::nullopt;
    }
    return std::move(resolved->first);
}

std::optional<std::string> TypeNameResolver::emitTypeName(QualType state) {
    auto resolved = resolveCppTypeId(state);
    if (!resolved) {
        return std::nullopt;
    }
    return std::move(resolved->first);
}

std::string TypeNameResolver::emitTypeNameForNode(const AstNodeBase* type_node) {
    if (!type_node || type_node->kind() != ParserToken::Kind::TypeName) {
        return ""; // нет типа-аннотации - caller решает (напр. параметр без типа → auto)
    }
    const auto* it = type_node->as<IdentType>();
    // Факт анализа материализован семантикой (`resolveTypeRef`): полная инстанциация шаблонов,
    // размерности, алиасы с учётом shadowing и активных типовых параметров. Кодоген НЕ резолвит
    // имя типа повторно (единственный источник решения - анализ): он применяет квалификаторы
    // узла и строит C++-представление.
    QualType applied = QualType::fromTypeId(it->resolvedTypeId);
    if (applied.getTypeId() == INVALID_TYPE_ID) {
        // Аннотацию отвергла семантика (диагностика уже выдана) либо факт не материализован.
        // Тихий fallback на резолв по имени запрещён: это второй источник истины.
        m_ectx.m_ctx.report(type_node->range(), diag::DiagId::ParseError,
                            "unable to generate C++ type '{}': the annotation is not resolved by semantic analysis", requiredText(*type_node));
        return "";
    }
    // Квалификаторы аннотации (const/reftype/policy/impl) материализованы семантикой
    // (resolveTypeRef → IdentType::resolvedState); повторно не применяются.
    if (it->resolvedState.isValid()) {
        applied = it->resolvedState;
    } else if (const AstNodeAttr* attrNode = type_node->as_attr(); attrNode != nullptr) {
        const AttrPool& pool = m_ectx.m_ctx.attrs();
        const auto reftypeId = pool.lookup(attr::Reftype);
        if (attrNode->has_attr(pool, attr::ReadOnly) || (reftypeId.has_value() && attrNode->has_attr(*reftypeId))) {
            m_ectx.m_ctx.report(type_node->range(), diag::DiagId::ParseError, "type qualifiers of '{}' were not materialized by semantic analysis",
                                requiredText(*type_node));
            return "";
        }
    }
    // Многомерное определение массива `:Bool[3,4]`: тип регистрируется (семантика), но генерация
    // C++ не реализована («не реализовано» - только на кодогенерации, как у вложенных литералов).
    if (isMultiDimArray(applied.getTypeId(), m_ectx.m_ctx.readTypes())) {
        m_ectx.m_ctx.report(type_node->range(), diag::DiagId::ParseError, "многомерные массивы пока не реализованы: транслируются как тензоры (LibTorch)");
        return "";
    }
    // Абстрактный `:Tensor`/`:SparseTensor` без формы и dtype: в позиции аннотации (параметр/
    // возврат/поле) конкретный элемент неизвестен - это ошибка аннотации, а не отсутствие провайдера.
    {
        const TypeReadModel reg = m_ectx.m_ctx.readTypes();
        const TypeId canon = reg.store().desugaredTypeId(applied.getTypeId());
        if (canon == reg.getType(type::Tensor) || canon == reg.getType(type::SparseTensor)) {
            m_ectx.m_ctx.report(type_node->range(), diag::DiagId::ParseError, "tensor element type is unknown; annotate the initializer or use ':Elem[dims]'");
            return "";
        }
    }
    // resolveCppTypeId записывает все инклуды типа (on-use) и строит C++-имя.
    if (auto resolved = resolveCppTypeId(applied)) {
        return std::move(resolved->first);
    }
    // resolveCppTypeId при отказе ВСЕГДА выдаёт диагностику - дублировать её здесь нельзя.
    return "";
}

// Рантайм-символ по типизированному идентификатору: заголовки из компайлтайм-таблицы
// (types/runtime_symbols.hpp). Enum вместо строки - опечатка в имени символа невозможна.
// ЕДИНСТВЕННЫЙ способ записи заголовков рантайм-символа.

std::optional<std::pair<std::string, std::string_view>> TypeNameResolver::resolveCppTypeId(TypeId type_id) const {
    return resolveCppTypeId(QualType::fromTypeId(type_id));
}

namespace {
template <typename>
inline constexpr bool kUnhandledTypeData = false;
} // namespace

std::string TypeNameResolver::wrapRefKind(RefType refKind, std::string base) {
    return refTypeCppName(refKind, base);
}

std::optional<std::pair<std::string, std::string_view>> TypeNameResolver::resolveCppTypeId(QualType state) const {
    const TypeId type_id = state.getTypeId();
    // ИНВАРИАНТ: UNKNOWN-тип не должен доходить до кодогенерации - анализатор обязан разрешить
    // каждый тип. Иначе - явная диагностика + FAULT (без тихого `auto`).
    if (type_id == INVALID_TYPE_ID) {
        m_ectx.m_ctx.report(MapperRange{}, diag::DiagId::ParseError,
                            "internal: UNKNOWN type reached code generation; the analyzer must resolve every type before transpilation");
        FAULT("UNKNOWN type (INVALID_TYPE_ID) reached code generation");
    }
    const TypeId canonical = m_ectx.m_ctx.readTypes().store().desugaredTypeId(type_id);
    const RefType refKind = getRefType(getTypeKind(canonical));
    // Спецправило: узкая строка как указатель на константные данные (`StrChar^` с reftype(ptr))
    // - это C-строка `const char*`, а не `const std::string*`.
    if (state.isConst() && refKind == RefType::kPtr) {
        const TypeKind baseKind = withRefType(getTypeKind(canonical), RefType::kValue);
        const TypeId baseId = QualType::fromTypeId(canonical).withKind(baseKind).getTypeId();
        if (m_ectx.m_ctx.readTypes().store().desugaredTypeId(baseId) == m_ectx.m_ctx.readTypes().getType(type::StrChar)) {
            return std::make_pair(std::string("const char*"), m_ectx.m_ctx.readTypes().builtins().preprocIncludeOf(canonical));
        }
    }
    const auto& data = m_ectx.m_ctx.readTypes().store().getTypeData(canonical);
    if (!data) {
        // Forward/incomplete: имя и диагностику даёт единый принтер (descriptor-путь).
        return resolveViaPrinter(state, type_id, canonical, refKind);
    }
    // ЕДИНЫЙ дискриминатор: альтернатива TypeData. Новый вариант не компилируется без handler'а.
    return std::visit(
        [&](const auto& d) -> CppTypeNameResult {
            using D = std::decay_t<decltype(d)>;
            if constexpr (std::is_same_v<D, SimpleTypeData>) {
                return renderSimple(state, type_id, canonical, refKind);
            } else if constexpr (std::is_same_v<D, FunctionTypeData>) {
                return resolveViaPrinter(state, type_id, canonical, refKind);
            } else if constexpr (std::is_same_v<D, TemplateTypeData>) {
                // Сегодня TemplateTypeData создаётся только для Range<Elem>; иные шаблоны - printer.
                if (m_ectx.m_ctx.readTypes().store().isRangeType(canonical)) {
                    return renderRange(canonical, refKind);
                }
                return resolveViaPrinter(state, type_id, canonical, refKind);
            } else if constexpr (std::is_same_v<D, ArrayTypeData>) {
                return renderArray(state, canonical, refKind);
            } else if constexpr (std::is_same_v<D, TensorTypeData>) {
                return renderTensor(canonical, refKind);
            } else if constexpr (std::is_same_v<D, MemberPointerTypeData>) {
                return resolveViaPrinter(state, type_id, canonical, refKind);
            } else if constexpr (std::is_same_v<D, RefTypeData>) {
                return resolveViaPrinter(state, type_id, canonical, refKind);
            } else if constexpr (std::is_same_v<D, PackExpansionTypeData>) {
                return resolveViaPrinter(state, type_id, canonical, refKind);
            } else if constexpr (std::is_same_v<D, TupleTypeData>) {
                return renderTuple(canonical, refKind);
            } else if constexpr (std::is_same_v<D, EnumTypeData> || std::is_same_v<D, VariantTypeData>) {
                return renderNominal(state, canonical);
            } else if constexpr (std::is_same_v<D, NativeTemplateTypeData>) {
                return renderNativeTemplate(canonical, refKind);
            } else if constexpr (std::is_same_v<D, NativeClassTypeData>) {
                return renderNativeClass(canonical, refKind);
            } else if constexpr (std::is_same_v<D, RecordTypeData>) {
                return renderRecord(state, canonical);
            } else if constexpr (std::is_same_v<D, TemplateParamTypeData>) {
                return resolveViaPrinter(state, type_id, canonical, refKind);
            } else {
                static_assert(kUnhandledTypeData<D>, "resolveCppTypeId: unhandled TypeData variant - add a C++ renderer");
            }
        },
        *data);
}

TypeNameResolver::CppTypeNameResult TypeNameResolver::resolveViaPrinter(QualType state, TypeId type_id, TypeId canonical, RefType refKind) const {
    auto cpp_name = m_ectx.m_ctx.readTypes().printer().getCppTypeName(canonical);
    if (!cpp_name) {
        m_ectx.m_ctx.report(MapperRange{}, diag::DiagId::ParseError, "{}", cpp_name.error());
        return std::nullopt;
    }
    const std::string_view include = m_ectx.m_ctx.readTypes().builtins().preprocIncludeOf(canonical);
    m_includes.recordUsedType(canonical);
    // Пользовательский алиас сохраняет trust-имя; встроенные алиасы маппятся на каноническое
    // C++-имя. Структурный ref-узел, функция и типовой параметр алиасом не считаются.
    const bool ref_node = m_ectx.m_ctx.readTypes().store().isTypeDataKind(canonical, TypeDataKind::kRefType);
    const bool function_node = m_ectx.m_ctx.readTypes().store().isTypeDataKind(canonical, TypeDataKind::kFunction);
    const bool template_param = m_ectx.m_ctx.readTypes().store().isTemplateParamType(type_id);
    if (!ref_node && !function_node && !template_param && refKind == RefType::kValue && m_ectx.m_ctx.readTypes().store().isUserDefinedType(type_id)) {
        auto name = m_ectx.m_ctx.readTypes().printer().getCppTypeName(state);
        if (!name) {
            m_ectx.m_ctx.report(MapperRange{}, diag::DiagId::ParseError, "{}", name.error());
            return std::nullopt;
        }
        return std::make_pair(std::move(*name), include);
    }
    if (state.isConst()) {
        return std::make_pair(std::string("const ") + *cpp_name, include);
    }
    return std::make_pair(std::move(*cpp_name), include);
}

TypeNameResolver::CppTypeNameResult TypeNameResolver::renderSimple(QualType state, TypeId type_id, TypeId canonical, RefType refKind) const {
    const TypeReadModel& types = m_ectx.m_ctx.readTypes();
    if (canonical == types.getType(type_category::Tuple)) {
        m_ectx.m_ctx.report(MapperRange{}, diag::DiagId::ParseError,
                            "abstract type ':Tuple' has no C++ representation; use a structural annotation ':Tuple(name:Type, ...)' or a typed initializer");
        return std::nullopt;
    }
    if (canonical == types.getType(type_category::Range)) {
        m_ectx.m_ctx.report(MapperRange{}, diag::DiagId::ParseError,
                            "abstract type ':Range' has no C++ representation; initialize it from a range literal or declare a concrete element type");
        return std::nullopt;
    }
    if (canonical == types.getType(type::Array)) {
        m_ectx.m_ctx.report(MapperRange{}, diag::DiagId::ParseError,
                            "abstract type ':Array' has no C++ representation; annotate the element type (e.g. ':Int32') or use an initializer");
        return std::nullopt;
    }
    if (canonical == types.getType(type::Tensor) || canonical == types.getType(type::SparseTensor)) {
        return renderTensor(canonical, refKind);
    }
    return resolveViaPrinter(state, type_id, canonical, refKind);
}

TypeNameResolver::CppTypeNameResult TypeNameResolver::renderNominal(QualType state, TypeId canonical) const {
    auto name = m_ectx.m_ctx.readTypes().printer().getCppTypeName(state);
    if (!name) {
        m_ectx.m_ctx.report(MapperRange{}, diag::DiagId::ParseError, "{}", name.error());
        return std::nullopt;
    }
    m_includes.recordUsedType(canonical);
    return std::make_pair(std::move(*name), std::string_view{});
}

TypeNameResolver::CppTypeNameResult TypeNameResolver::renderRecord(QualType state, TypeId canonical) const {
    auto name = m_ectx.m_ctx.readTypes().printer().getCppTypeName(state);
    if (!name) {
        m_ectx.m_ctx.report(MapperRange{}, diag::DiagId::ParseError, "{}", name.error());
        return std::nullopt;
    }
    // Заголовки типовых аргументов записываются рекурсивно: getCppTypeName только рендерит.
    if (const auto* rd = m_ectx.m_ctx.readTypes().store().recordData(canonical); rd != nullptr && rd->templateOf != INVALID_TYPE_ID) {
        for (const TypeId a : rd->templateArgs) {
            (void)resolveCppTypeId(a);
        }
    }
    m_includes.recordUsedType(canonical);
    return std::make_pair(std::move(*name), std::string_view{});
}

TypeNameResolver::CppTypeNameResult TypeNameResolver::renderTuple(TypeId canonical, RefType refKind) const {
    const auto* tt = m_ectx.m_ctx.readTypes().store().getTypeDataAs<TupleTypeData>(canonical);
    EXPECT(tt != nullptr && "renderTuple: canonical type must be a tuple");
    std::string result = "std::tuple<";
    bool first = true;
    for (const auto& el : tt->elements) {
        if (!first) {
            result += ", ";
        }
        first = false;
        if (!el.type.isValid()) {
            m_ectx.m_ctx.report(MapperRange{}, diag::DiagId::ParseError, "tuple element type is not resolved");
            return std::nullopt;
        }
        auto en = resolveCppTypeId(el.type.getTypeId());
        if (!en) {
            return std::nullopt;
        }
        if (en->first.empty()) {
            m_ectx.m_ctx.report(MapperRange{}, diag::DiagId::ParseError, "unable to generate C++ type for a tuple element");
            return std::nullopt;
        }
        result += en->first;
    }
    result += ">";
    m_includes.recordRequiredInclude("#include <tuple>");
    m_includes.recordUsedType(canonical);
    return std::make_pair(wrapRefKind(refKind, std::move(result)), m_ectx.m_ctx.readTypes().builtins().preprocIncludeOf(canonical));
}

TypeNameResolver::CppTypeNameResult TypeNameResolver::renderRange(TypeId canonical, RefType refKind) const {
    const TypeId elem = m_ectx.m_ctx.readTypes().store().rangeElementType(canonical);
    std::string elemCpp;
    if (elem != INVALID_TYPE_ID) {
        if (auto en = resolveCppTypeId(elem)) {
            elemCpp = std::move(en->first);
        }
    }
    if (elemCpp.empty()) {
        m_ectx.m_ctx.report(MapperRange{}, diag::DiagId::ParseError, "cannot resolve the C++ element type of the range");
        return std::nullopt;
    }
    m_includes.recordUsedType(canonical);
    return std::make_pair(wrapRefKind(refKind, "trust::Range<" + elemCpp + ">"), m_ectx.m_ctx.readTypes().builtins().preprocIncludeOf(canonical));
}

TypeNameResolver::CppTypeNameResult TypeNameResolver::renderNativeTemplate(TypeId canonical, RefType refKind) const {
    const std::string_view cppTpl = m_ectx.m_ctx.readTypes().store().nativeTemplateCppName(canonical);
    const auto& args = m_ectx.m_ctx.readTypes().store().nativeTemplateArgs(canonical);
    std::string result(cppTpl);
    result += "<";
    bool first = true;
    for (const TypeId a : args) {
        if (!first) {
            result += ", ";
        }
        first = false;
        std::string ac;
        if (a == INVALID_TYPE_ID) {
            m_ectx.m_ctx.report(MapperRange{}, diag::DiagId::ParseError, "native template argument type is not resolved");
            return std::nullopt;
        }
        if (m_ectx.m_ctx.readTypes().store().isPackExpansionType(a)) {
            const TypeId pattern = m_ectx.m_ctx.readTypes().store().packExpansionPattern(a);
            if (!m_ectx.m_ctx.readTypes().store().isTemplateParamType(pattern)) {
                m_ectx.m_ctx.report(MapperRange{}, diag::DiagId::ParseError, "type parameter pack expansion requires a type parameter");
                return std::nullopt;
            }
            const std::string_view pname = m_ectx.m_ctx.readTypes().store().templateParamName(pattern);
            bool active = false;
            for (const auto& p : m_ectx.m_activeTemplateParams) {
                if (p == pname) {
                    active = true;
                    break;
                }
            }
            if (!active) {
                m_ectx.m_ctx.report(MapperRange{}, diag::DiagId::ParseError, "pack expansion '{}...' is not allowed in this type position", pname);
                return std::nullopt;
            }
            ac = std::string(pname) + "...";
        } else if (auto en = resolveCppTypeId(a)) {
            ac = std::move(en->first);
        }
        if (ac.empty()) {
            m_ectx.m_ctx.report(MapperRange{}, diag::DiagId::ParseError, "unable to generate C++ type for a native template argument");
            return std::nullopt;
        }
        result += ac;
    }
    result += ">";
    m_includes.recordUsedType(canonical);
    return std::make_pair(wrapRefKind(refKind, std::move(result)), m_ectx.m_ctx.readTypes().builtins().preprocIncludeOf(canonical));
}

TypeNameResolver::CppTypeNameResult TypeNameResolver::renderNativeClass(TypeId canonical, RefType refKind) const {
    const std::string_view cppName = m_ectx.m_ctx.readTypes().store().nativeClassCppName(canonical);
    if (cppName.empty()) {
        m_ectx.m_ctx.report(MapperRange{}, diag::DiagId::ParseError, "native class type '{}' has no C++ name",
                            m_ectx.m_ctx.readTypes().printer().getTypeName(canonical));
        return std::nullopt;
    }
    m_includes.recordUsedType(canonical);
    return std::make_pair(wrapRefKind(refKind, std::string(cppName)), m_ectx.m_ctx.readTypes().builtins().preprocIncludeOf(canonical));
}

TypeNameResolver::CppTypeNameResult TypeNameResolver::renderTensor(TypeId canonical, RefType refKind) const {
    const TypeId canonicalTensor = m_ectx.m_ctx.readTypes().store().desugaredTypeId(canonical);
    const bool isConcreteTensor = m_ectx.m_ctx.readTypes().store().isTensorType(canonicalTensor);
    const bool isAbstractTensor =
        canonicalTensor == m_ectx.m_ctx.readTypes().getType(type::Tensor) || canonicalTensor == m_ectx.m_ctx.readTypes().getType(type::SparseTensor);
    EXPECT((isConcreteTensor || isAbstractTensor) && "renderTensor: canonical type must be a tensor");
    const bool sparse = isConcreteTensor && m_ectx.m_ctx.readTypes().store().isSparseTensorType(canonicalTensor);
    const TypeId baseTensor = sparse ? m_ectx.m_ctx.readTypes().getType(type::SparseTensor) : m_ectx.m_ctx.readTypes().getType(type::Tensor);
    if (m_ectx.m_ctx.readTypes().builtins().getBuiltinDefinition(baseTensor) == nullptr) {
        m_ectx.m_ctx.report(MapperRange{}, diag::DiagId::ParseError, "implementation of built-in type '{}' is not defined; load provider 'tensor'",
                            sparse ? "SparseTensor" : "Tensor");
        return std::nullopt;
    }
    if (sparse) {
        m_ectx.m_ctx.report(MapperRange{}, diag::DiagId::ParseError, "sparse tensors are not implemented");
        return std::nullopt;
    }
    if (!isConcreteTensor) {
        m_ectx.m_ctx.report(MapperRange{}, diag::DiagId::ParseError, "tensor type requires an element type and shape (use ':Tensor[dims](:Elem)')");
        return std::nullopt;
    }
    const TypeId elem = m_ectx.m_ctx.readTypes().store().tensorElementType(canonicalTensor);
    if (elem == INVALID_TYPE_ID || elem == m_ectx.m_ctx.readTypes().getType(type_generic::Any)) {
        m_ectx.m_ctx.report(MapperRange{}, diag::DiagId::ParseError, "tensor element type is unknown; annotate the initializer or use ':Elem[dims]'");
        return std::nullopt;
    }
    std::string elemCpp;
    if (auto en = resolveCppTypeId(elem)) {
        elemCpp = std::move(en->first);
    }
    if (elemCpp.empty()) {
        m_ectx.m_ctx.report(MapperRange{}, diag::DiagId::ParseError, "cannot resolve the C++ element type of the tensor");
        return std::nullopt;
    }
    m_includes.recordUsedType(canonicalTensor);
    return std::make_pair(wrapRefKind(refKind, "trust::Tensor<" + elemCpp + ">"), m_ectx.m_ctx.readTypes().builtins().preprocIncludeOf(canonicalTensor));
}

TypeNameResolver::CppTypeNameResult TypeNameResolver::renderArray(QualType state, TypeId canonical, RefType refKind) const {
    const TypeId elem = m_ectx.m_ctx.readTypes().store().arrayElementType(canonical);
    std::string elemCpp;
    if (elem != INVALID_TYPE_ID) {
        if (auto en = resolveCppTypeId(elem)) {
            elemCpp = std::move(en->first);
        }
    }
    if (elemCpp.empty()) {
        m_ectx.m_ctx.report(MapperRange{}, diag::DiagId::ParseError, "cannot resolve the C++ element type of the array");
        return std::nullopt;
    }
    if (state.isConst()) {
        uint64_t n = 0;
        const auto& dims = m_ectx.m_ctx.readTypes().store().arrayDimensions(canonical);
        if (!dims.empty()) {
            n = dims.front();
        }
        m_includes.recordRequiredInclude("#include <array>");
        return std::make_pair(wrapRefKind(refKind, "std::array<" + elemCpp + ", " + std::to_string(n) + ">"), std::string_view{});
    }
    m_includes.recordRequiredInclude("#include <vector>");
    return std::make_pair(wrapRefKind(refKind, "std::vector<" + elemCpp + ">"), std::string_view{});
}

QualType TypeNameResolver::toRuntimeType(QualType state) const {
    const TypeId id = state.getTypeId();
    // Кодоген не запускается при ошибках семантики, поэтому невалидный тип здесь — нарушение
    // внутреннего инварианта, а не пользовательская ошибка (нельзя молча печатать тип 0).
    EXPECT(id.isValid() && "toRuntimeType: value type must be valid");
    if (m_ectx.m_ctx.readTypes().store().isUserDefinedType(id)) {
        return state; // номинальная identity: registry_index — часть идентичности
    }
    // Встроенный тип/прозрачный алиас: рантайм строит id как TypeId::fromKind (index 0).
    // Слот реестра компилятора не должен протекать в ABI-identity, иначе компиляторный и
    // рантаймный теги одного типа не совпадут. Квалификаторы сохраняются.
    return state.withIdentity(TypeId::fromKind(getTypeKind(id)));
}

} // namespace trust
