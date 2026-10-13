// test/unit/types/type_invariant_test.cpp
// Инвариант identity/state: TypeId не несёт type-state-флагов в identity-операциях,
// хеш структурный, точки входа реестра отвергают флаговые TypeId.

#include "types/type_system.hpp"
#include "type_flag_test_utils.hpp"
#include "types/type_id.hpp"
#include "session/context.hpp"
#include "gtest/gtest.h"

#include <map>
#include <memory>
#include <stdexcept>
#include <unordered_set>
#include <vector>

namespace trust {
namespace {

class TypeInvariantFixture : public ::testing::Test {
  protected:
    Context m_ctx;
    std::unique_ptr<TypeSystem> m_types;
    void SetUp() override {
        m_types = std::make_unique<TypeSystem>(m_ctx.diag(), m_ctx.opts());
        m_ctx.setTypes(m_types.get());
    }
};

TEST_F(TypeInvariantFixture, StructuralTypeStripsQualTypeFlags) {
    const TypeId int32 = m_types->read().getType("Int32");
    const TypeId flagged = setFlag(setFlag(int32, TypeFlag::Inferred), TypeFlag::Const);

    EXPECT_TRUE(isUnqualifiedType(int32));
    EXPECT_FALSE(isUnqualifiedType(flagged));
    EXPECT_EQ(getUnqualifiedType(flagged), int32);
    EXPECT_EQ(getRegistryIndex(flagged), getRegistryIndex(int32));
}

TEST_F(TypeInvariantFixture, HashAndEqualityAreIdentity) {
    const TypeId int32 = m_types->read().getType("Int32");
    const TypeId flagged = setFlag(setFlag(int32, TypeFlag::Inferred), TypeFlag::Const);
    const std::hash<TypeId> hash;
    // operator== and std::hash are identity-based: a state-bearing TypeId is the same KEY as its
    // identity projection; only raw() exposes the qualifier bits.
    EXPECT_EQ(int32, flagged);
    EXPECT_EQ(hash(int32), hash(flagged));
    EXPECT_NE(int32.raw(), flagged.raw());
    EXPECT_TRUE(flagged.isValid());
}

TEST_F(TypeInvariantFixture, InvalidIsIdentityNotRaw) {
    // State bits never make an invalid identity valid: fromRaw(kConstFlag) == INVALID_TYPE_ID.
    const TypeId constOnly = TypeId::fromRaw(kConstFlag);
    EXPECT_FALSE(constOnly.isValid());
    EXPECT_EQ(constOnly, INVALID_TYPE_ID);
    EXPECT_EQ(std::hash<TypeId>{}(constOnly), std::hash<TypeId>{}(INVALID_TYPE_ID));
    EXPECT_NE(constOnly.raw(), INVALID_TYPE_ID.raw());
}

TEST_F(TypeInvariantFixture, RecordFieldKeepsDeclaredConst) {
    const TypeId int32 = m_types->read().getType("Int32");
    const QualType cnst = QualType::fromTypeId(int32).withConst();
    const TypeId rec = m_types->registrar().defineRecord("ConstHolder", Group::kClassDefs, {{"x", cnst}});
    ASSERT_NE(rec, INVALID_TYPE_ID);

    const auto* rd = m_types->read().store().recordData(rec);
    ASSERT_NE(rd, nullptr);
    ASSERT_EQ(rd->fields.size(), 1u);
    EXPECT_TRUE(rd->fields[0].type.isConst());

    const QualType found = m_types->read().store().findField(rec, "x");
    EXPECT_TRUE(found.isValid());
    EXPECT_TRUE(found.isConst());
    EXPECT_EQ(found.getTypeId(), int32);
}

TEST_F(TypeInvariantFixture, IdentityKeysCollapseStateInContainers) {
    const TypeId int32 = m_types->read().getType("Int32");
    const TypeId flagged = setFlag(setFlag(int32, TypeFlag::Inferred), TypeFlag::Const);

    std::unordered_set<TypeId> unique{int32, flagged};
    EXPECT_EQ(unique.size(), 1u);

    std::map<TypeId, int> ordered;
    ordered[int32] = 1;
    ordered[flagged] = 2;
    EXPECT_EQ(ordered.size(), 1u);
    EXPECT_EQ(ordered[int32], 2);
}

TEST_F(TypeInvariantFixture, QualTypeApiValidity) {
    const TypeId int32 = m_types->read().getType("Int32");
    const QualType st = QualType::fromTypeId(int32).withConst().withInferred();
    EXPECT_TRUE(st.isValid());
    EXPECT_TRUE(st.isConst());
    EXPECT_TRUE(st.isInferred());
    EXPECT_FALSE(QualType{}.isValid());
}

TEST_F(TypeInvariantFixture, RegistryRejectsFlaggedBaseType) {
    const TypeId int32 = m_types->read().getType("Int32");
    const TypeId flagged = setFlag(int32, TypeFlag::Const);
    EXPECT_THROW(m_types->registrar().registerType("BadAlias", flagged), std::runtime_error);
}

TEST_F(TypeInvariantFixture, RegistryRejectsFlaggedEnumValueType) {
    const TypeId int32 = m_types->read().getType("Int32");
    const TypeId flagged = setFlag(int32, TypeFlag::Inferred);
    EXPECT_THROW(m_types->registrar().registerEnumType("BadEnum", flagged, std::vector<EnumMemberData>{}), std::runtime_error);
}

TEST_F(TypeInvariantFixture, RegistryRejectsFlaggedVariantMember) {
    const TypeId int32 = m_types->read().getType("Int32");
    const TypeId flagged = setFlag(int32, TypeFlag::Const);
    std::vector<VariantMemberData> members{{"x", flagged}};
    EXPECT_THROW(m_types->registrar().registerVariantType("BadVariant", std::move(members)), std::runtime_error);
}

TEST_F(TypeInvariantFixture, TupleElementStateIsNormalizedToIdentity) {
    const TypeId int32 = m_types->read().getType("Int32");
    const TypeId flagged = setFlag(int32, TypeFlag::Const);
    const TypeId tuple = m_types->interner().getOrCreateTupleType({{"x", QualType::fromTypeId(flagged)}});
    ASSERT_NE(tuple, INVALID_TYPE_ID);
    // Tuple-элементы - occurrence-позиции: квалификатор не входит в identity и нормализуется.
    const auto* td = m_types->read().store().getTypeDataAs<TupleTypeData>(tuple);
    ASSERT_NE(td, nullptr);
    ASSERT_EQ(td->elements.size(), 1u);
    EXPECT_EQ(td->elements[0].type.getTypeId(), int32);
    EXPECT_FALSE(td->elements[0].type.isConst());
}

TEST_F(TypeInvariantFixture, RegistryRejectsFlaggedMethodSignature) {
    const TypeId int32 = m_types->read().getType("Int32");
    const TypeId signature = m_types->interner().getOrCreateFunctionType(int32, {int32});
    const TypeId flagged = setFlag(signature, TypeFlag::Const);
    EXPECT_THROW(m_types->methods().addMethod(int32, "%bad", flagged), std::runtime_error);
}

TEST_F(TypeInvariantFixture, QualTypeFromIdentityStripsFlags) {
    const TypeId int32 = m_types->read().getType("Int32");
    const TypeId flagged = setFlag(setFlag(int32, TypeFlag::Inferred), TypeFlag::Const);

    // fromTypeId - чистая identity: состояние задаётся только withConst()/withInferred().
    const QualType identity = QualType::fromTypeId(flagged);
    EXPECT_FALSE(identity.isConst());
    EXPECT_FALSE(identity.isInferred());
    EXPECT_EQ(identity.getTypeId(), int32);
    // fromRaw сохраняет флаги, уже лежащие в raw (ABI/диагностика).
    const QualType raw = QualType::fromRaw(flagged.raw());
    EXPECT_TRUE(raw.isConst());
    EXPECT_TRUE(raw.isInferred());
}

TEST_F(TypeInvariantFixture, RegistryRejectsFlaggedArrayElement) {
    const TypeId int32 = m_types->read().getType("Int32");
    const TypeId flagged = setFlag(int32, TypeFlag::Const);
    EXPECT_THROW(m_types->interner().getOrCreateArrayType(flagged, {2}), std::runtime_error);
}

TEST_F(TypeInvariantFixture, RegistryRejectsFlaggedTensorElement) {
    const TypeId int32 = m_types->read().getType("Int32");
    const TypeId flagged = setFlag(int32, TypeFlag::Inferred);
    EXPECT_THROW(m_types->interner().getOrCreateTensorType(flagged, {2, 2}, false), std::runtime_error);
}

TEST_F(TypeInvariantFixture, RegistryRejectsFlaggedRangeElement) {
    const TypeId int32 = m_types->read().getType("Int32");
    const TypeId flagged = setFlag(int32, TypeFlag::Const);
    EXPECT_THROW(m_types->interner().getOrCreateRangeType(flagged), std::runtime_error);
}

TEST_F(TypeInvariantFixture, RegistryRejectsFlaggedNativeTemplateArg) {
    const TypeId int32 = m_types->read().getType("Int32");
    const TypeId flagged = setFlag(int32, TypeFlag::Inferred);
    EXPECT_THROW(m_types->interner().getOrCreateNativeTemplateType("%std::vector", {flagged}), std::runtime_error);
}

TEST_F(TypeInvariantFixture, ByteAlignedLayout) {
    EXPECT_EQ(kTypeIndexBits, 24u);
    EXPECT_EQ(kTypeIndexMask, 0x00FFFFFFu);
    EXPECT_EQ(kMaxTypeIndex, 0x00FFFFFFu);
    EXPECT_EQ(kQualifierMask, 0xFF000000ULL);
    EXPECT_EQ(kConstFlag & kQualifierMask, kConstFlag);
    EXPECT_EQ(kInferredFlag & kQualifierMask, kInferredFlag);
}

TEST_F(TypeInvariantFixture, IndexIgnoresWholeStateByte) {
    const TypeId int32 = m_types->read().getType("Int32");
    // Бит 24 зарезервирован, но принадлежит state-байту: identity его игнорирует.
    const TypeId reserved = TypeId::fromRaw(int32.raw() | 0x01000000ULL);
    EXPECT_EQ(getRegistryIndex(reserved), getRegistryIndex(int32));
    EXPECT_EQ(getUnqualifiedType(reserved), int32);
    EXPECT_FALSE(isUnqualifiedType(reserved));
}

TEST_F(TypeInvariantFixture, MaxIndexDoesNotOverlapStateByte) {
    const TypeId maxIndex = makeTypeId(makeTypeKind(Group::kIntegers, 32), kMaxTypeIndex);
    EXPECT_TRUE(isUnqualifiedType(maxIndex));
    EXPECT_EQ(getRegistryIndex(maxIndex), kMaxTypeIndex);
    EXPECT_EQ(getRegistryIndex(setFlag(maxIndex, TypeFlag::Const)), kMaxTypeIndex);
}

TEST_F(TypeInvariantFixture, MakeTypeIdRejectsOversizedIndex) {
    // Индекс вне 24 бит занял бы state-байт и схлопнулся бы с другим ключом (operator== снимает
    // state-биты) — вместо молчаливой маски ожидается явная диагностика.
    EXPECT_THROW(makeTypeId(makeTypeKind(Group::kIntegers, 32), kMaxTypeIndex + 1), std::runtime_error);
}

TEST_F(TypeInvariantFixture, IdentityOfRejectsUnknownScope) {
    const TypeId int32 = m_types->read().getType("Int32");
    EXPECT_THROW(m_types->read().store().identityOf(int32, static_cast<TypeIdentityScope>(99)), std::runtime_error);
}

TEST_F(TypeInvariantFixture, QualTypeStripsReservedStateBits) {
    const TypeId int32 = m_types->read().getType("Int32");
    const QualType st = QualType::fromRaw(int32.raw() | 0x01000000ULL | kConstFlag);
    EXPECT_EQ(st.getTypeId(), int32);
    EXPECT_TRUE(st.isConst());
}

TEST_F(TypeInvariantFixture, IsTypeDataKindUsesVariantIndex) {
    const TypeId int32 = m_types->read().getType("Int32");
    const TypeId ref = m_types->interner().getOrCreateRefType(RefType::kPtr, int32);
    EXPECT_TRUE(m_types->read().store().isTypeDataKind(ref, TypeDataKind::kRefType));
    EXPECT_FALSE(m_types->read().store().isTypeDataKind(ref, TypeDataKind::kFunction));
    // Без данных (forward declaration) - не «угаданный» вид.
    const TypeId forward = m_types->registrar().declareRecord("ForwardOnly", Group::kStructs);
    ASSERT_NE(forward, INVALID_TYPE_ID);
    EXPECT_FALSE(m_types->read().store().isTypeDataKind(forward, TypeDataKind::kRecord));
}

} // namespace
} // namespace trust
