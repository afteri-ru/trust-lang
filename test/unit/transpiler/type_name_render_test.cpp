// test/unit/transpiler/type_name_render_test.cpp
// Контракт «кто рендерит C++-имя типа» для каждого TypeDataKind.
//
// Два слоя:
//   * types/registry/type_printer.cpp::printType(kCpp) - номинальное C++-имя из данных реестра;
//   * transpiler/type/type_name_resolver.cpp::resolveCppTypeId - кодоген-политика (include on-use,
//     used-type, диагностика, явный отказ для абстрактных контейнеров).
//
// Таблица индексируется TypeDataKind и покрывает КАЖДЫЙ вариант TypeData: размер массива
// берётся из variant, а новый TypeDataKind без обработки в switch ловится компилятором (-Wswitch).

#include "transpiler/emit/emit_ctx.hpp"
#include "transpiler/type/type_include_collector.hpp"
#include "transpiler/type/type_name_resolver.hpp"
#include "session/context.hpp"
#include "types/registry/type_printer.hpp"
#include "types/group.hpp"
#include "types/type_system.hpp"
#include "types/type_data.hpp"
#include "types/type_id.hpp"
#include "types/names.hpp"
#include "types/typekind.hpp"
#include "utils/utils.hpp"

#include "gtest/gtest.h"

#include <array>
#include <cstddef>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace trust {
namespace {

TypeId makeSimple(TypeSystem& reg) {
    return reg.read().getType(type::Int32);
}

TypeId makeFunction(TypeSystem& reg) {
    return reg.interner().getOrCreateFunctionType(reg.read().getType(type::Int32), {reg.read().getType(type::Float64)});
}

TypeId makeRange(TypeSystem& reg) {
    return reg.interner().getOrCreateRangeType(reg.read().getType(type::Int64));
}

TypeId makeArray(TypeSystem& reg) {
    return reg.interner().getOrCreateArrayType(reg.read().getType(type::Int32), {3});
}

TypeId makeTensor(TypeSystem& reg) {
    return reg.interner().getOrCreateTensorType(reg.read().getType(type::Int32), {2, 2});
}

TypeId makeMemberPointer(TypeSystem& reg) {
    const TypeId cls =
        reg.registrar().defineRecord("RenderPoint", Group::kStructs, {TupleElementData{"x", QualType::fromTypeId(reg.read().getType(type::Int32))}});
    EXPECT_NE(cls, INVALID_TYPE_ID) << "test fixture: member-pointer class must be created";
    const TypeId member = reg.read().getType(type::Int32);
    const TypeKind kind = makeTypeKind(Group::kReftype, 1, TypeClass::kTrivial, RefType::kMptr);
    return reg.interner().getOrCreateStructuralType("", kind, {cls, member}, TypeData{MemberPointerTypeData{cls, member}});
}

TypeId makeRefType(TypeSystem& reg) {
    return reg.interner().getOrCreateRefType(RefType::kPtr, reg.read().getType(type::Int32));
}

TypeId makePackExpansion(TypeSystem& reg) {
    return reg.interner().getOrCreatePackExpansionType(reg.interner().getOrCreateTemplateParamType("T"));
}

TypeId makeTuple(TypeSystem& reg) {
    return reg.interner().getOrCreateTupleType(
        {{"a", QualType::fromTypeId(reg.read().getType(type::Int32))}, {"b", QualType::fromTypeId(reg.read().getType(type::Float64))}});
}

TypeId makeEnum(TypeSystem& reg) {
    return reg.registrar().registerEnumType("RenderColor", reg.read().getType(type::Int32), {EnumMemberData{"RED", "0"}});
}

TypeId makeVariant(TypeSystem& reg) {
    return reg.registrar().registerVariantType("RenderValue", {VariantMemberData{"num", reg.read().getType(type::Int32)}});
}

TypeId makeNativeTemplate(TypeSystem& reg) {
    return reg.interner().getOrCreateNativeTemplateType("std::pair", {reg.read().getType(type::Int32), reg.read().getType(type::Float64)},
                                                        "#include <utility>");
}

TypeId makeNativeClass(TypeSystem& reg) {
    return reg.registrar().registerNativeClass("RenderString", "std::string", MapperRange{}, "#include <string>");
}

TypeId makeRecord(TypeSystem& reg) {
    return reg.registrar().defineRecord("RenderStruct", Group::kStructs, {TupleElementData{"x", QualType::fromTypeId(reg.read().getType(type::Int32))}});
}

TypeId makeTemplateParam(TypeSystem& reg) {
    return reg.interner().getOrCreateTemplateParamType("T");
}

/// Ожидания для одного TypeDataKind. nullopt в renderer/resolver - ЯВНАЯ
/// неподдерживаемость слоя (ошибка рендера / отказ резолвера), а не «нет данных».
struct Expected {
    std::optional<std::string> renderer;
    std::optional<std::string> resolver;
    /// Основной include типа (второй элемент пары resolveCppTypeId).
    std::string resolverInclude;
    /// Обязательные директивы, записанные резолвером через recordRequiredInclude (on-use).
    std::vector<std::string> requiredIncludes;
    /// Отмечен ли тип как использованный (m_usedTypes).
    bool resolverUsed = false;
};

struct Row {
    TypeDataKind kind;
    const char* label;
    TypeId (*make)(TypeSystem&);
    Expected expected;
};

// Порядок строк ОБЯЗАН совпадать с порядком TypeDataKind (проверяется в тесте).
const std::array<Row, std::variant_size_v<TypeData>> kRows{{
    {TypeDataKind::kSimple, "kSimple", makeSimple, {"int32_t", "int32_t", "#include <cstdint>", {}, true}},
    {TypeDataKind::kFunction,
     "kFunction",
     makeFunction,
     {"std::function<int32_t(double)>", "std::function<int32_t(double)>", "#include <functional>", {}, true}},
    // Range<Elem>: конкретное C++-имя есть только у кодоген-слоя (номинального имени у типа нет).
    {TypeDataKind::kTemplate, "kTemplate (Range<Elem>)", makeRange, {std::nullopt, "trust::Range<int64_t>", "@trust/range.hpp", {}, true}},
    {TypeDataKind::kArray, "kArray", makeArray, {std::nullopt, "std::vector<int32_t>", "", {"#include <vector>"}, false}},
    {TypeDataKind::kTensor, "kTensor", makeTensor, {std::nullopt, "trust::Tensor<int32_t>", "@trust/tensor.hpp", {}, true}},
    // Указатель на член: структурный узел не имеет номинального C++-имени ни в одном слое
    // (в текущей модели не создаётся семантикой; фиксируем явную неподдерживаемость).
    {TypeDataKind::kMemberPointer, "kMemberPointer", makeMemberPointer, {std::nullopt, std::nullopt, "", {}, false}},
    {TypeDataKind::kRefType, "kRefType", makeRefType, {"int32_t*", "int32_t*", "", {}, true}},
    {TypeDataKind::kPackExpansion, "kPackExpansion", makePackExpansion, {std::nullopt, std::nullopt, "", {}, false}},
    {TypeDataKind::kTuple, "kTuple", makeTuple, {std::nullopt, "std::tuple<int32_t, double>", "", {"#include <tuple>"}, true}},
    {TypeDataKind::kEnum, "kEnum", makeEnum, {"c_RenderColor", "c_RenderColor", "", {}, true}},
    {TypeDataKind::kVariant, "kVariant", makeVariant, {"c_RenderValue", "c_RenderValue", "", {}, true}},
    {TypeDataKind::kNativeTemplate, "kNativeTemplate", makeNativeTemplate, {std::nullopt, "std::pair<int32_t, double>", "#include <utility>", {}, true}},
    {TypeDataKind::kNativeClass, "kNativeClass", makeNativeClass, {"std::string", "std::string", "#include <string>", {}, true}},
    {TypeDataKind::kRecord, "kRecord", makeRecord, {"c_RenderStruct", "c_RenderStruct", "", {}, true}},
    {TypeDataKind::kTemplateParam, "kTemplateParam", makeTemplateParam, {"T", "T", "", {}, true}},
}};

struct ResolveOutcome {
    std::optional<std::string> cpp;
    std::string include;
    std::set<std::string> requiredIncludes;
    bool used = false;
};

ResolveOutcome resolve(TypeSystem& reg, Context& ctx, TypeId id) {
    CppEmitContext ectx(ctx);
    TypeIncludeCollector includes(ectx);
    TypeNameResolver resolver(ectx, includes);
    ResolveOutcome out;
    if (auto res = resolver.resolveCppTypeId(id)) {
        out.cpp = std::move(res->first);
        out.include = std::string(res->second);
    }
    out.requiredIncludes = ectx.m_accumulators.requiredIncludes;
    out.used = ectx.m_accumulators.usedTypes.contains(reg.read().store().desugaredTypeId(id));
    return out;
}

class TypeNameRenderTest : public ::testing::Test {
  protected:
    void SetUp() override {
        m_prevErr = setErrs(&m_stream);
        m_types = std::make_unique<TypeSystem>(m_ctx.diag(), m_ctx.opts());
        m_ctx.setTypes(m_types.get());
        // Провайдер тензора: конкретный Tensor требует определения встроенного типа.
        (void)m_types->builtins().define(m_types->read(), type::Tensor, "trust::Tensor", {"@trust/tensor.hpp"});
    }

    void TearDown() override { setErrs(m_prevErr); }

    std::ostream* m_prevErr = nullptr;
    std::ostringstream m_stream;
    Context m_ctx;
    std::unique_ptr<TypeSystem> m_types;
};

TEST_F(TypeNameRenderTest, TableOrderMatchesKinds) {
    for (size_t i = 0; i < kRows.size(); ++i) {
        EXPECT_EQ(static_cast<size_t>(kRows[i].kind), i) << kRows[i].label;
    }
}

TEST_F(TypeNameRenderTest, RendererAndResolverContractPerKind) {
    for (const Row& row : kRows) {
        SCOPED_TRACE(row.label);
        const TypeId id = row.make(*m_types);
        ASSERT_NE(id, INVALID_TYPE_ID);
        EXPECT_TRUE(m_types->read().store().isTypeDataKind(m_types->read().store().desugaredTypeId(id), row.kind)) << "sample type has unexpected TypeDataKind";

        // Номинальный рендер (types).
        const auto rendered = printType(m_types->printer(), id, TypePrintPolicy{TypePrintTarget::kCpp});
        if (row.expected.renderer.has_value()) {
            ASSERT_TRUE(rendered.has_value()) << rendered.error();
            EXPECT_EQ(*rendered, *row.expected.renderer);
        } else {
            EXPECT_FALSE(rendered.has_value()) << "renderer unexpectedly supports this kind: " << (rendered ? *rendered : "");
        }

        // Кодоген-политика (transpiler).
        const ResolveOutcome out = resolve(*m_types, m_ctx, id);
        if (row.expected.resolver.has_value()) {
            ASSERT_TRUE(out.cpp.has_value()) << "resolver unexpectedly refuses this kind";
            EXPECT_EQ(*out.cpp, *row.expected.resolver);
        } else {
            EXPECT_FALSE(out.cpp.has_value()) << "resolver unexpectedly supports this kind: " << (out.cpp ? *out.cpp : "");
        }
        EXPECT_EQ(out.include, row.expected.resolverInclude);
        for (const std::string& inc : row.expected.requiredIncludes) {
            EXPECT_TRUE(out.requiredIncludes.contains(inc)) << "missing on-use include: " << inc;
        }
        EXPECT_EQ(out.used, row.expected.resolverUsed);

        // Согласованность: если оба слоя дают имя, кодоген-слой делегирует/совпадает.
        if (row.expected.renderer.has_value() && row.expected.resolver.has_value()) {
            EXPECT_EQ(*row.expected.renderer, *row.expected.resolver);
        }
    }
}

// Абстрактные контейнеры не имеют C++-имени: номинальный рендер и кодоген-слой дают явный
// отказ (диагностика), а не `auto`. Конкретика задаётся структурной аннотацией/инициализатором.
TEST_F(TypeNameRenderTest, AbstractContainersHaveNoCppName) {
    const TypeId tuple = m_types->read().getType(type_category::Tuple);
    const TypeId range = m_types->read().getType(type_category::Range);
    const TypeId array = m_types->read().getType(type::Array);
    const TypeId tensor = m_types->read().getType(type::Tensor);

    for (const TypeId id : {tuple, range, array}) {
        EXPECT_FALSE(printType(m_types->printer(), id, TypePrintPolicy{TypePrintTarget::kCpp}).has_value());
    }
    // Абстрактный :Tensor - предобъявленный встроенный тип с cppName: номинальное имя есть
    // (конкретное имя Tensor<Elem> - только у кодоген-слоя).
    EXPECT_EQ(printType(m_types->printer(), tensor, TypePrintPolicy{TypePrintTarget::kCpp}).value_or(""), "trust::Tensor");

    for (const TypeId id : {tuple, range, array, tensor}) {
        EXPECT_FALSE(resolve(*m_types, m_ctx, id).cpp.has_value());
    }
}

// Единый принтер: политика kTrust даёт номинальную форму языка, kDiagnostic - её же с опциональным
// раскрытием алиаса (aka). Отсутствие дескриптора - ошибка (никакого fallback-имени).
TEST_F(TypeNameRenderTest, TrustPolicyNames) {
    const TypeId int32 = m_types->read().getType(type::Int32);
    const auto trust = [&](TypeId id) { return printType(m_types->printer(), id, TypePrintPolicy{TypePrintTarget::kTrust}); };
    ASSERT_TRUE(trust(int32).has_value());
    EXPECT_EQ(*trust(int32), "Int32");
    ASSERT_TRUE(trust(m_types->interner().applyRefType(int32, RefType::kShared)).has_value());
    EXPECT_EQ(*trust(m_types->interner().applyRefType(int32, RefType::kShared)), "shared<Int32>");
    ASSERT_TRUE(trust(m_types->interner().getOrCreateArrayType(int32, {3})).has_value());
    EXPECT_EQ(*trust(m_types->interner().getOrCreateArrayType(int32, {3})), "Array<Int32>");
    ASSERT_TRUE(trust(m_types->interner().getOrCreateRangeType(int32)).has_value());
    EXPECT_EQ(*trust(m_types->interner().getOrCreateRangeType(int32)), "Range<Int32>");
}

TEST_F(TypeNameRenderTest, PrinterHasNoFallbackForUnknownId) {
    // Отсутствие типа - легитимное значение: печатается явной меткой, C++-представления нет.
    EXPECT_EQ(printType(m_types->printer(), INVALID_TYPE_ID, TypePrintPolicy{TypePrintTarget::kTrust}).value_or(""), "<no type>");
    EXPECT_EQ(printType(m_types->printer(), INVALID_TYPE_ID, TypePrintPolicy{TypePrintTarget::kDiagnostic}).value_or(""), "<no type>");
    EXPECT_FALSE(printType(m_types->printer(), INVALID_TYPE_ID, TypePrintPolicy{TypePrintTarget::kCpp}).has_value());
    EXPECT_EQ(m_types->read().printer().getTypeName(INVALID_TYPE_ID), "<no type>");

    // Настоящая неизвестность (kind есть, дескриптора нет) - явная ошибка, не строка-заглушка.
    // Data=2 у kCallable не используется ни одним зарегистрированным типом.
    const TypeId unknown = TypeId::fromRaw(static_cast<uint64_t>(makeTypeKind(Group::kCallable, 2)) << 32);
    EXPECT_FALSE(printType(m_types->printer(), unknown, TypePrintPolicy{TypePrintTarget::kTrust}).has_value());
    EXPECT_THROW(m_types->read().printer().getTypeName(unknown), std::runtime_error);
}

TEST_F(TypeNameRenderTest, DiagnosticShowsDesugaredAlias) {
    const TypeId int32 = m_types->read().getType(type::Int32);
    const TypeId alias = m_types->registrar().registerType("PrintAliasInt", int32);
    ASSERT_NE(alias, INVALID_TYPE_ID);

    TypePrintPolicy plain{TypePrintTarget::kDiagnostic};
    TypePrintPolicy desugared{TypePrintTarget::kDiagnostic, true};
    ASSERT_TRUE(printType(m_types->printer(), alias, plain).has_value());
    EXPECT_EQ(*printType(m_types->printer(), alias, plain), "PrintAliasInt");
    ASSERT_TRUE(printType(m_types->printer(), alias, desugared).has_value());
    EXPECT_EQ(*printType(m_types->printer(), alias, desugared), "PrintAliasInt (aka Int32)");
    // Не-алиас не получает aka-хвост.
    ASSERT_TRUE(printType(m_types->printer(), int32, desugared).has_value());
    EXPECT_EQ(*printType(m_types->printer(), int32, desugared), "Int32");
}

TEST_F(TypeNameRenderTest, UnnamedStructuralTypeHasExplicitLabel) {
    const TypeId tuple = m_types->interner().getOrCreateTupleType({{"a", QualType::fromTypeId(m_types->read().getType(type::Int32))}});
    ASSERT_NE(tuple, INVALID_TYPE_ID);
    // Кортеж/сигнатура не имеют номинального имени: печатается явная метка, не пустая строка.
    const auto name = printType(m_types->printer(), tuple, TypePrintPolicy{TypePrintTarget::kTrust});
    ASSERT_TRUE(name.has_value());
    EXPECT_FALSE(name->empty());
    EXPECT_EQ(name->rfind("<unnamed type #", 0), 0u);
    EXPECT_EQ(m_types->read().printer().getTypeName(tuple).rfind("<unnamed type #", 0), 0u);
}

} // namespace
} // namespace trust
