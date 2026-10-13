// test/unit/transpiler/runtime_type_tag_test.cpp
// Контракт ABI-границы «компилятор → trust::TypedValue»: toRuntimeType приводит identity к
// рантайм-форме. Встроенные типы (и их прозрачные алиасы) в рантайме идентифицируются только
// TypeKind (registry_index = 0); номинальные пользовательские типы сохраняют index.
// Квалификаторы (const/inferred) сохраняются всегда.

#include "session/context.hpp"
#include "transpiler/emit/emit_ctx.hpp"
#include "transpiler/type/type_include_collector.hpp"
#include "transpiler/type/type_name_resolver.hpp"
#include "types/group.hpp"
#include "types/type_system.hpp"
#include "types/type_data.hpp"
#include "types/type_id.hpp"
#include "types/names.hpp"
#include "utils/utils.hpp"

#include "gtest/gtest.h"

#include <memory>
#include <sstream>

namespace trust {
namespace {

class RuntimeTypeTagTest : public ::testing::Test {
  protected:
    void SetUp() override {
        m_prevErr = setErrs(&m_stream);
        m_types = std::make_unique<TypeSystem>(m_ctx.diag(), m_ctx.opts());
        m_ctx.setTypes(m_types.get());
    }
    void TearDown() override { setErrs(m_prevErr); }

    QualType tag(QualType state) {
        CppEmitContext ectx(m_ctx);
        TypeIncludeCollector includes(ectx);
        TypeNameResolver resolver(ectx, includes);
        return resolver.toRuntimeType(state);
    }

    std::ostream* m_prevErr = nullptr;
    std::ostringstream m_stream;
    Context m_ctx;
    std::unique_ptr<TypeSystem> m_types;
};

TEST_F(RuntimeTypeTagTest, BuiltinIdentityIsKindOnlyAndKeepsQualifiers) {
    const TypeId int32 = m_types->read().getType(type::Int32);
    ASSERT_NE(getRegistryIndex(int32), 0u) << "builtin has a registry slot in the compiler";
    const QualType state = QualType::fromTypeId(int32).withConst().withInferred();

    const QualType out = tag(state);

    EXPECT_EQ(getTypeKind(out.getTypeId()), getTypeKind(int32));
    EXPECT_EQ(getRegistryIndex(out.getTypeId()), 0u) << "compiler registry slot must not leak into runtime identity";
    EXPECT_TRUE(out.isConst());
    EXPECT_TRUE(out.isInferred());
}

TEST_F(RuntimeTypeTagTest, BuiltinAliasIsTransparent) {
    // `Integer` — representation-алиас Int64: TypeKind совпадает с целевым, поэтому рантайм-тег
    // совпадает с тегом Int64 (оба kind-only).
    const TypeId integer = m_types->read().getType("Integer");
    const TypeId int64 = m_types->read().getType(type::Int64);

    const QualType out = tag(QualType::fromTypeId(integer));

    EXPECT_EQ(getTypeKind(out.getTypeId()), getTypeKind(int64));
    EXPECT_EQ(getRegistryIndex(out.getTypeId()), 0u);
}

TEST_F(RuntimeTypeTagTest, UserAliasKeepsNominalIndex) {
    const TypeId int32 = m_types->read().getType(type::Int32);
    const TypeId myInt = m_types->registrar().registerType("MyInt", int32);
    ASSERT_NE(myInt, INVALID_TYPE_ID);
    const QualType state = QualType::fromTypeId(myInt).withConst();

    const QualType out = tag(state);

    EXPECT_EQ(out.getTypeId(), myInt) << "user alias is nominal: registry_index is part of identity";
    EXPECT_NE(getRegistryIndex(out.getTypeId()), 0u);
    EXPECT_TRUE(out.isConst());
}

TEST_F(RuntimeTypeTagTest, RecordTypeKeepsNominalIndex) {
    const TypeId record = m_types->registrar().defineRecord("TagPoint", Group::kStructs, {TupleElementData{"x", QualType::fromTypeId(m_types->read().getType(type::Int32))}});
    ASSERT_NE(record, INVALID_TYPE_ID);

    const QualType out = tag(QualType::fromTypeId(record));

    EXPECT_EQ(out.getTypeId(), record);
    EXPECT_NE(getRegistryIndex(out.getTypeId()), 0u);
}

TEST_F(RuntimeTypeTagTest, InvalidTypeIsRejected) {
    // Невалидный тип элемента — нарушение внутреннего инварианта кодогена, а не «Any»:
    // молчаливая эмиссия типа 0 запрещена.
    EXPECT_THROW(tag(QualType{}), std::runtime_error);
}

} // namespace
} // namespace trust
