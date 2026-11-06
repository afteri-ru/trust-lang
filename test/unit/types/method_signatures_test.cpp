// test/unit/types/method_signatures_test.cpp
// ЕДИНЫЙ примитив эффективных сигнатур набора (`types/method_signatures.hpp`): порядок набора
// сохраняется (индекс выровнен с MethodRef), контейнерные типы подставляют Elem.
#include "type_registry_fixture.hpp"
#include "types/call.hpp"
#include "types/names.hpp"
#include "types/type_system.hpp"

#include "gtest/gtest.h"

#include <vector>

namespace trust {
namespace {

class MethodSignaturesTest : public test::TypeSystemFixture {};

TEST_F(MethodSignaturesTest, PreservesOrderAndSubstitutesElement) {
    TypeSystem& reg = types();
    const TypeId i32 = reg.read().getType(type::Int32);
    const TypeId boolean = reg.read().getType(type::Bool);

    // Порядок набора сохраняется: по индексу выбираются C++-суффикс/параметры.
    const TypeId first = reg.interner().getOrCreateFunctionType(boolean, {i32});
    const TypeId second = reg.interner().getOrCreateFunctionType(boolean, {i32, i32});
    const std::vector<TypeId> signatures{second, first};
    EXPECT_EQ(effectiveMethodSignatures(reg, i32, signatures), signatures);

    // Контейнерный тип подставляет элемент на месте типового параметра.
    const TypeId param = reg.read().getType(type_category::TemplateParam);
    const TypeId arrayI32 = reg.interner().getOrCreateArrayType(i32);
    const TypeId signature = reg.interner().getOrCreateFunctionType(boolean, {param});
    const std::vector<TypeId> out = effectiveMethodSignatures(reg, arrayI32, std::vector<TypeId>{signature});
    ASSERT_EQ(out.size(), 1U);
    const auto* fd = reg.read().store().getTypeDataAs<FunctionTypeData>(out.front());
    ASSERT_NE(fd, nullptr);
    ASSERT_EQ(fd->paramTypes.size(), 1U);
    EXPECT_EQ(fd->paramTypes.front(), i32); // T -> Elem
}

TEST_F(MethodSignaturesTest, MethodRefProjectionIsIndexAlignedWithEntries) {
    // R14: единственный владелец набора — `MethodRef::entries`. `effectiveMethodSignatures(..., *method)`
    // возвращает ПРОЕКЦИЮ, выровненную по индексу с владельцем (на этом держится выбор C++-суффикса
    // и метаданных параметров). Дублирующего хранилища сигнатур нет — только производный вектор.
    TypeSystem& reg = types();
    const TypeId i32 = reg.read().getType(type::Int32);
    const TypeId boolean = reg.read().getType(type::Bool);

    const TypeId rec = reg.registrar().defineRecord("OrderRec", Group::kStructs, {});
    ASSERT_NE(rec, INVALID_TYPE_ID);

    const TypeId sig0 = reg.interner().getOrCreateFunctionType(boolean, {i32});
    const TypeId sig1 = reg.interner().getOrCreateFunctionType(boolean, {i32, i32});
    reg.methods().addMethod(rec, "f", sig0, {}, CallableParamInfo{});
    reg.methods().addMethod(rec, "f", sig1, {}, CallableParamInfo{});

    const auto method = reg.read().methods().findMethodInfo(rec, "f");
    ASSERT_TRUE(method.has_value());
    ASSERT_EQ(method->entries.size(), 2u);

    const std::vector<TypeId> sigs = effectiveMethodSignatures(reg, rec, *method);
    ASSERT_EQ(sigs.size(), method->entries.size());
    for (std::size_t i = 0; i < sigs.size(); ++i) {
        EXPECT_EQ(sigs[i], method->entries[i].signature) << "index=" << i;
    }
}

} // namespace
} // namespace trust
