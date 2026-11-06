// test/unit/types/fn_identity_test.cpp
// FunctionIdentity (types/fn_identity.hpp) - the SINGLE function-signature identity:
// interning key slot layout (variadic/pack/pack-position), C++ identity (deep aliases,
// positional template binders, pack binders) and the deterministic C++ suffix.

#include "support/cpp_facts_test_util.hpp"
#include "types/call.hpp"
#include "type_flag_test_utils.hpp"
#include "types/group.hpp"
#include "types/type_system.hpp"
#include "types/names.hpp"
#include "types/typekind.hpp"
#include "session/context.hpp"
#include "gtest/gtest.h"

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace trust {
namespace {

class FnIdentityFixture : public ::testing::Test {
  protected:
    void SetUp() override {
        m_types = std::make_unique<TypeSystem>(m_ctx.diag(), m_ctx.opts());
        m_ctx.setTypes(m_types.get());
        test::installTargetFacts(m_ctx, m_target);
    }
    Context m_ctx;
    std::unique_ptr<TypeSystem> m_types;
    TargetContract m_target;
};

TEST_F(FnIdentityFixture, SlotLayoutSeparatesVariadicFromFixedParameter) {
    TypeSystem& reg = *m_types;
    const TypeId i32 = reg.read().getType(type::Int32);
    const TypeId any = reg.read().getType(type_generic::Any);
    // f(a:Int32, b:Any) and f(a:Int32, ...) have the same flat children list.
    const TypeId fixed = reg.interner().getOrCreateFunctionType(i32, {i32, any});
    const TypeId variadic = reg.interner().getOrCreateFunctionType(i32, {i32}, any);
    EXPECT_NE(fixed, variadic);
    EXPECT_EQ(reg.interner().getOrCreateFunctionType(i32, {i32, any}), fixed); // idempotent
}

TEST_F(FnIdentityFixture, SlotLayoutSeparatesPackFromFixedParameter) {
    TypeSystem& reg = *m_types;
    const TypeId i32 = reg.read().getType(type::Int32);
    const TypeId t = reg.interner().getOrCreateTemplateParamType("T");
    const TypeId fixed = reg.interner().getOrCreateFunctionType(i32, {t});
    const TypeId pack = reg.interner().getOrCreateFunctionType(i32, {}, INVALID_TYPE_ID, false, t, 1);
    EXPECT_NE(fixed, pack);
    // Pack position is a part of the identity.
    EXPECT_NE(pack, reg.interner().getOrCreateFunctionType(i32, {}, INVALID_TYPE_ID, false, t, 2));
}

TEST_F(FnIdentityFixture, NativeTemplateIdentityUsesAbstractTemplate) {
    TypeSystem& reg = *m_types;
    const TypeId i32 = reg.read().getType(type::Int32);
    const TypeId pairTpl = test::registerNativeTemplateFacts(reg, m_target, "PairTpl", "std::pair", {});
    const TypeId tupleTpl = test::registerNativeTemplateFacts(reg, m_target, "TupleTpl", "std::tuple", {});
    const TypeId pair = reg.interner().getOrCreateNativeTemplateType(pairTpl, {i32, i32});
    const TypeId tuple = reg.interner().getOrCreateNativeTemplateType(tupleTpl, {i32, i32});
    ASSERT_NE(pair, INVALID_TYPE_ID);
    ASSERT_NE(tuple, INVALID_TYPE_ID);
    EXPECT_NE(pair, tuple);
    // C++-имя — целевой факт (читается через абстрактный шаблон), НЕ часть языковой identity (R37).
    EXPECT_EQ(test::cppFactsViewOf(m_ctx).nativeTemplateCppName(pair), "std::pair");
    EXPECT_EQ(test::cppFactsViewOf(m_ctx).nativeTemplateCppName(tuple), "std::tuple");
}

TEST_F(FnIdentityFixture, UserAliasIsNominalButBuiltinAliasIsTransparent) {
    TypeSystem& reg = *m_types;
    const TypeId i32 = reg.read().getType(type::Int32);
    const TypeId alias = reg.registrar().registerType("MyInt", i32);
    ASSERT_NE(alias, INVALID_TYPE_ID);
    const TypeId arrayAlias = reg.interner().getOrCreateArrayType(alias);
    const TypeId arrayCanon = reg.interner().getOrCreateArrayType(i32);
    EXPECT_NE(arrayAlias, arrayCanon);
    const FunctionIdentity& identity = reg.identity();
    // `MyInt ::= :Int32` — НОВЫЙ тип Trust (в C++ реализуется через using).
    EXPECT_FALSE(identity.sameSemanticType(alias, i32));
    EXPECT_FALSE(identity.sameSemanticType(arrayAlias, arrayCanon));
    const TypeId sigAlias = reg.interner().getOrCreateFunctionType(i32, {arrayAlias});
    const TypeId sigCanon = reg.interner().getOrCreateFunctionType(i32, {arrayCanon});
    EXPECT_FALSE(identity.sameCppOverload(sigAlias, {}, sigCanon, {}));
    EXPECT_NE(identity.cppSuffix(sigAlias, {}), identity.cppSuffix(sigCanon, {}));
    // Встроенные representation-алиасы прозрачны: Integer == Int64.
    const auto integer = reg.read().findType("Integer");
    ASSERT_TRUE(integer.has_value());
    EXPECT_TRUE(identity.sameSemanticType(*integer, reg.read().getType(type::Int64)));
    // FFI-идентичность раскрывает и пользовательские алиасы (внешний C++ не различает using).
    EXPECT_TRUE(identity.sameInteropOverload(sigAlias, {}, sigCanon, {}));
}

TEST_F(FnIdentityFixture, TemplateBindersAreCanonicalizedByPosition) {
    TypeSystem& reg = *m_types;
    const TypeId i32 = reg.read().getType(type::Int32);
    const TypeId t = reg.interner().getOrCreateTemplateParamType("T");
    const TypeId u = reg.interner().getOrCreateTemplateParamType("U");
    const TypeId sigT = reg.interner().getOrCreateFunctionType(i32, {t});
    const TypeId sigU = reg.interner().getOrCreateFunctionType(i32, {u});
    const FunctionIdentity& identity = reg.identity();
    const TemplateBinders bindersT{{"T", false}};
    const TemplateBinders bindersU{{"U", false}};
    // Declaration-less context cannot prove equality (name-based fallback)...
    EXPECT_FALSE(identity.sameCppOverload(sigT, {}, sigU, {}));
    // ...but with declaration binders both are position 0 - the same C++ template signature.
    EXPECT_TRUE(identity.sameCppOverload(sigT, bindersT, sigU, bindersU));
    // The suffix uses the SAME positional identity (one projection), so equivalent declarations
    // mangle alike and distinct ones differ.
    EXPECT_EQ(identity.cppSuffix(sigT, bindersT), identity.cppSuffix(sigU, bindersU));
    // A pack binder is a different kind of signature: <T> vs <T...>.
    const TypeId sigPack = reg.interner().getOrCreateFunctionType(i32, {}, INVALID_TYPE_ID, false, t, 1);
    const TemplateBinders packBinder{{"T", true}};
    EXPECT_FALSE(identity.sameCppOverload(sigT, bindersT, sigPack, packBinder));
    EXPECT_TRUE(identity.sameCppOverload(sigPack, packBinder, sigPack, packBinder));
    EXPECT_NE(identity.cppSuffix(sigT, bindersT), identity.cppSuffix(sigPack, packBinder));
    EXPECT_NE(identity.cppSuffix(sigT, bindersT).find("_P"), std::string::npos);
}

TEST_F(FnIdentityFixture, SameCppTypeUsesBinderPositions) {
    TypeSystem& reg = *m_types;
    const TypeId t = reg.interner().getOrCreateTemplateParamType("T");
    const TypeId u = reg.interner().getOrCreateTemplateParamType("U");
    const FunctionIdentity& identity = reg.identity();
    const TemplateBinders bindersT{{"T", false}};
    const TemplateBinders bindersU{{"U", false}};
    // Declaration-less: имена различаются.
    EXPECT_FALSE(identity.sameSemanticType(t, u));
    // С binder-контекстом оба параметра стоят на позиции 0 — один C++-тип.
    EXPECT_TRUE(identity.sameSemanticType(t, u, bindersT, bindersU));
}

TEST_F(FnIdentityFixture, SuffixFormatAndStability) {
    TypeSystem& reg = *m_types;
    const TypeId i32 = reg.read().getType(type::Int32);
    const TypeId noParams = reg.interner().getOrCreateFunctionType(i32, {});
    const TypeId oneParam = reg.interner().getOrCreateFunctionType(i32, {i32});
    const FunctionIdentity& identity = reg.identity();
    EXPECT_EQ(identity.cppSuffix(noParams, {}), "_P0_");
    EXPECT_NE(identity.cppSuffix(noParams, {}), identity.cppSuffix(oneParam, {}));
    EXPECT_EQ(identity.cppSuffix(oneParam, {}), identity.cppSuffix(oneParam, {})); // deterministic
}

TEST_F(FnIdentityFixture, CppIdentityIgnoresReturnTypeAndTrust) {
    TypeSystem& reg = *m_types;
    const TypeId i32 = reg.read().getType(type::Int32);
    const TypeId i64 = reg.read().getType(type::Int64);
    const FunctionIdentity& identity = reg.identity();
    const TypeId retI32 = reg.interner().getOrCreateFunctionType(i32, {i32});
    const TypeId retI64 = reg.interner().getOrCreateFunctionType(i64, {i32});
    const TypeId trusted = reg.interner().getOrCreateFunctionType(i32, {i32}, INVALID_TYPE_ID, /*hasTrust=*/true);
    EXPECT_NE(retI32, retI64);
    EXPECT_NE(retI32, trusted);
    EXPECT_TRUE(identity.sameCppOverload(retI32, {}, retI64, {}));  // C++ cannot overload on return type
    EXPECT_TRUE(identity.sameCppOverload(retI32, {}, trusted, {})); // trust is not part of C++ identity
    EXPECT_EQ(identity.cppSuffix(retI32, {}), identity.cppSuffix(retI64, {}));
    EXPECT_EQ(identity.cppSuffix(retI32, {}), identity.cppSuffix(trusted, {}));
}

TEST_F(FnIdentityFixture, RefOverTemplateParamIsNotThePlainParameter) {
    // Trap: applyRefType(T, shared) is a fast-path bit that keeps T's registry index, so the
    // TypeData of the reference is T's data. Identity must read the RefType kind FIRST.
    TypeSystem& reg = *m_types;
    const TypeId i32 = reg.read().getType(type::Int32);
    const TypeId t = reg.interner().getOrCreateTemplateParamType("T");
    const TypeId sharedT = reg.interner().applyRefType(t, RefType::kShared);
    ASSERT_NE(sharedT, t);
    const FunctionIdentity& identity = reg.identity();
    const TemplateBinders bindersT{{"T", false}};
    EXPECT_FALSE(identity.sameSemanticType(sharedT, t));
    const TypeId sigT = reg.interner().getOrCreateFunctionType(i32, {t});
    const TypeId sigSharedT = reg.interner().getOrCreateFunctionType(i32, {sharedT});
    ASSERT_NE(sigT, sigSharedT);
    EXPECT_FALSE(identity.sameCppOverload(sigT, bindersT, sigSharedT, bindersT));
    EXPECT_NE(identity.cppSuffix(sigT, bindersT), identity.cppSuffix(sigSharedT, bindersT));
}

TEST_F(FnIdentityFixture, OverloadIdentityIsPositionalForPermutedBinders) {
    // `<T,U> f(T,U)` and `<U,T> f(T,U)` share the same declared signature TypeId but are
    // DIFFERENT C++ templates (the used positions are (0,1) vs (1,0)).
    TypeSystem& reg = *m_types;
    const TypeId i32 = reg.read().getType(type::Int32);
    const TypeId t = reg.interner().getOrCreateTemplateParamType("T");
    const TypeId u = reg.interner().getOrCreateTemplateParamType("U");
    const TypeId sig = reg.interner().getOrCreateFunctionType(i32, {t, u});
    const FunctionIdentity& identity = reg.identity();
    const TemplateBinders tu{{"T", false}, {"U", false}};
    const TemplateBinders ut{{"U", false}, {"T", false}};
    EXPECT_FALSE(identity.sameCppOverload(sig, tu, sig, ut)); // no TypeId early-out for different contexts
    EXPECT_NE(identity.cppSuffix(sig, tu), identity.cppSuffix(sig, ut));
    // Declaration contract = overload identity + tail; here both parts match.
    EXPECT_TRUE(identity.sameCppOverload(sig, tu, sig, tu));
    EXPECT_TRUE(identity.compare(sig, tu, sig, tu, IdentityScope::kDeclaration) == SignatureRelation::kEquivalent);
}

TEST_F(FnIdentityFixture, ParameterizedTypeIdentityIncludesTemplateMarker) {
    // TemplateTypeData identity = (template marker, ALL args). Interning knows the marker
    // (structuralKeyTag); the C++ code must know it too.
    TypeSystem& reg = *m_types;
    const TypeId i32 = reg.read().getType(type::Int32);
    const TypeId range = reg.interner().getOrCreateRangeType(i32);
    const TypeId other =
        reg.interner().getOrCreateStructuralType("", makeTypeKind(Group::kRanges, 1), {TypeKeyChild{i32}}, TypeData{TemplateTypeData{reg.read().getType(type_category::TemplateParam), {i32}}});
    ASSERT_NE(other, INVALID_TYPE_ID);
    const FunctionIdentity& identity = reg.identity();
    EXPECT_FALSE(identity.sameSemanticType(range, other));
}

TEST_F(FnIdentityFixture, SuffixMatchesOverloadIdentity) {
    // The C++ suffix is a PROJECTION of the same segment list as the overload identity:
    // equal names <=> equal overload identity (one source, no drift).
    TypeSystem& reg = *m_types;
    const TypeId i32 = reg.read().getType(type::Int32);
    const TypeId i64 = reg.read().getType(type::Int64);
    const TypeId str = reg.read().getType(type::StrChar);
    const TypeId alias = reg.registrar().registerType("MyInt", i32);
    const TypeId t = reg.interner().getOrCreateTemplateParamType("T");
    const TypeId u = reg.interner().getOrCreateTemplateParamType("U");
    const FunctionIdentity& identity = reg.identity();
    const TemplateBinders none{};
    const TemplateBinders bt{{"T", false}};
    const TemplateBinders btu{{"T", false}, {"U", false}};
    const TemplateBinders but{{"U", false}, {"T", false}};

    struct Case {
        TypeId sig;
        const TemplateBinders* binders;
    };
    const std::vector<Case> cases{
        {reg.interner().getOrCreateFunctionType(i32, {}), &none},         {reg.interner().getOrCreateFunctionType(i32, {i32}), &none},
        {reg.interner().getOrCreateFunctionType(i32, {i64}), &none},      {reg.interner().getOrCreateFunctionType(i32, {alias}), &none},
        {reg.interner().getOrCreateFunctionType(i32, {i32}, str), &none}, {reg.interner().getOrCreateFunctionType(i32, {}, INVALID_TYPE_ID, false, t, 1), &bt},
        {reg.interner().getOrCreateFunctionType(i32, {t}), &bt},          {reg.interner().getOrCreateFunctionType(i32, {t, u}), &btu},
    };
    for (std::size_t i = 0; i < cases.size(); ++i) {
        for (std::size_t j = i; j < cases.size(); ++j) {
            const bool same = identity.sameCppOverload(cases[i].sig, *cases[i].binders, cases[j].sig, *cases[j].binders);
            const bool sameName = identity.cppSuffix(cases[i].sig, *cases[i].binders) == identity.cppSuffix(cases[j].sig, *cases[j].binders);
            EXPECT_EQ(same, sameName) << i << " vs " << j;
        }
    }
    // The same declared TypeId in different binder contexts is a distinct overload and name.
    const TypeId perm = reg.interner().getOrCreateFunctionType(i32, {t, u});
    EXPECT_FALSE(identity.sameCppOverload(perm, btu, perm, but));
    EXPECT_NE(identity.cppSuffix(perm, btu), identity.cppSuffix(perm, but));
}

TEST_F(FnIdentityFixture, PackPositionRequiresPackSlot) {
    // The pack position is meaningful only together with a pack type; a garbage position is
    // invalid input (EXPECT), never a silent part of the identity.
    TypeSystem& reg = *m_types;
    const TypeId i32 = reg.read().getType(type::Int32);
    const TypeId t = reg.interner().getOrCreateTemplateParamType("T");
    EXPECT_THROW(reg.interner().getOrCreateFunctionType(i32, {}, INVALID_TYPE_ID, false, INVALID_TYPE_ID, 1), std::runtime_error);
    EXPECT_THROW(reg.interner().getOrCreateFunctionType(i32, {}, INVALID_TYPE_ID, false, t, 0), std::runtime_error);
    EXPECT_NE(reg.interner().getOrCreateFunctionType(i32, {}, INVALID_TYPE_ID, false, t, 1), INVALID_TYPE_ID);
}

TEST_F(FnIdentityFixture, SameTypeListDifferentSlotsHasDistinctNames) {
    // f(Int32, Any), f(Int32, ...) and f(Int32, Any...) share the flat type list {Int32, Any}
    // but are different signatures - the slot tags must keep both identity and names apart.
    TypeSystem& reg = *m_types;
    const TypeId i32 = reg.read().getType(type::Int32);
    const TypeId any = reg.read().getType(type_generic::Any);
    const FunctionIdentity& identity = reg.identity();
    const TypeId fixed = reg.interner().getOrCreateFunctionType(i32, {i32, any});
    const TypeId variadic = reg.interner().getOrCreateFunctionType(i32, {i32}, any);
    const TypeId pack = reg.interner().getOrCreateFunctionType(i32, {i32}, INVALID_TYPE_ID, false, any, 2);
    const std::vector<TypeId> sigs{fixed, variadic, pack};
    for (std::size_t i = 0; i < sigs.size(); ++i) {
        for (std::size_t j = i + 1; j < sigs.size(); ++j) {
            EXPECT_FALSE(identity.sameCppOverload(sigs[i], {}, sigs[j], {})) << i << " vs " << j;
            EXPECT_NE(identity.cppSuffix(sigs[i], {}), identity.cppSuffix(sigs[j], {})) << i << " vs " << j;
        }
    }
}

TEST_F(FnIdentityFixture, PolicyMatrixDistinguishesTrustAndInterop) {
    TypeSystem& reg = *m_types;
    const TypeId i32 = reg.read().getType(type::Int32);
    const TypeId i64 = reg.read().getType(type::Int64);
    const TypeId alias = reg.registrar().registerType("MyInt", i32);
    const FunctionIdentity& identity = reg.identity();

    // A USER alias is nominal for Trust and transparent for external C++ (using).
    EXPECT_FALSE(identity.sameSemanticType(alias, i32));
    const TypeId sigAlias = reg.interner().getOrCreateFunctionType(i32, {alias});
    const TypeId sigBase = reg.interner().getOrCreateFunctionType(i32, {i32});
    EXPECT_FALSE(identity.sameCppOverload(sigAlias, {}, sigBase, {}));
    EXPECT_TRUE(identity.sameInteropOverload(sigAlias, {}, sigBase, {}));
    EXPECT_NE(identity.cppSuffix(sigAlias, {}), identity.cppSuffix(sigBase, {}));

    // A BUILTIN representation alias (Integer -> Int64) is transparent in both policies.
    const auto integer = reg.read().findType("Integer");
    ASSERT_TRUE(integer.has_value());
    EXPECT_TRUE(identity.sameSemanticType(*integer, i64));
    const TypeId sigInteger = reg.interner().getOrCreateFunctionType(i32, {*integer});
    const TypeId sigI64 = reg.interner().getOrCreateFunctionType(i32, {i64});
    EXPECT_TRUE(identity.sameCppOverload(sigInteger, {}, sigI64, {}));
    EXPECT_EQ(identity.cppSuffix(sigInteger, {}), identity.cppSuffix(sigI64, {}));
}

TEST_F(FnIdentityFixture, NestedFunctionTrustIsNotPartOfCppIdentity) {
    // C++ has no trust: a parameter whose type is a function WITH conditions is the same C++
    // type as one without. Interning still keeps them apart (the nested TypeId is a child).
    TypeSystem& reg = *m_types;
    const TypeId i32 = reg.read().getType(type::Int32);
    const TypeId plainCb = reg.interner().getOrCreateFunctionType(i32, {i32});
    const TypeId trustedCb = reg.interner().getOrCreateFunctionType(i32, {i32}, INVALID_TYPE_ID, /*hasTrust=*/true);
    ASSERT_NE(plainCb, trustedCb);
    const TypeId sigPlain = reg.interner().getOrCreateFunctionType(i32, {plainCb});
    const TypeId sigTrusted = reg.interner().getOrCreateFunctionType(i32, {trustedCb});
    ASSERT_NE(sigPlain, sigTrusted);
    const FunctionIdentity& identity = reg.identity();
    EXPECT_TRUE(identity.sameCppOverload(sigPlain, {}, sigTrusted, {}));
    EXPECT_EQ(identity.cppSuffix(sigPlain, {}), identity.cppSuffix(sigTrusted, {}));
}

TEST_F(FnIdentityFixture, ParameterFlagsAreNotStoredInFunctionType) {
    // Behavioral flags (const/inferred/uninit) are occurrence qualifiers (QualType), NOT type
    // identity and NOT part of FunctionTypeData: `f(x^: Int32)` and `f(x: Int32)` share one
    // function TypeId whose parameter types are stored structurally.
    TypeSystem& reg = *m_types;
    const TypeId i32 = reg.read().getType(type::Int32);
    const FunctionIdentity& identity = reg.identity();
    const TypeId withConstFirst = reg.interner().getOrCreateFunctionType(i32, {setFlag(i32, TypeFlag::Const)});
    const TypeId plainAfter = reg.interner().getOrCreateFunctionType(i32, {i32});
    EXPECT_EQ(withConstFirst, plainAfter);
    EXPECT_TRUE(identity.sameCppOverload(withConstFirst, {}, plainAfter, {}));
    EXPECT_EQ(identity.cppSuffix(withConstFirst, {}), identity.cppSuffix(plainAfter, {}));
    const auto* fd = reg.read().store().getTypeDataAs<FunctionTypeData>(withConstFirst);
    ASSERT_NE(fd, nullptr);
    ASSERT_EQ(fd->paramTypes.size(), 1u);
    EXPECT_FALSE(testFlag(fd->paramTypes[0], TypeFlag::Const));
}

TEST_F(FnIdentityFixture, TypeCodeCacheIsTransparentAcrossReset) {
    // The canonical-code cache is not observable state: recomputing after reset() yields the
    // same suffix. Builtin-only types are used because reset() drops user aliases.
    TypeSystem& reg = *m_types;
    const TypeId i32 = reg.read().getType(type::Int32);
    const TypeId any = reg.read().getType(type_generic::Any);
    const FunctionIdentity& identity = reg.identity();
    const TypeId sig = reg.interner().getOrCreateFunctionType(i32, {i32}, any);
    const std::string before = identity.cppSuffix(sig, {});
    EXPECT_EQ(identity.cppSuffix(sig, {}), before); // second call hits the cache
    reg.reset();
    const TypeId sigAfter = reg.interner().getOrCreateFunctionType(reg.read().getType(type::Int32), {reg.read().getType(type::Int32)}, reg.read().getType(type_generic::Any));
    EXPECT_EQ(identity.cppSuffix(sigAfter, {}), before);
}

TEST_F(FnIdentityFixture, ReturnAndTrustComparesAliasedReturn) {
    // Хвост контракта объявления: return type сравнивается по C++-identity (встроенный
    // representation-алиас прозрачен), trust-бит — отдельная часть контракта.
    TypeSystem& reg = *m_types;
    const TypeId i32 = reg.read().getType(type::Int32);
    const TypeId i64 = reg.read().getType(type::Int64);
    const auto integer = reg.read().findType("Integer");
    ASSERT_TRUE(integer.has_value());
    const TypeId sigIntegerRet = reg.interner().getOrCreateFunctionType(*integer, {i32});
    const TypeId sigI64Ret = reg.interner().getOrCreateFunctionType(i64, {i32});
    const TypeId sigTrusted = reg.interner().getOrCreateFunctionType(*integer, {i32}, INVALID_TYPE_ID, /*hasTrust=*/true);
    const FunctionIdentity& identity = reg.identity();
    EXPECT_TRUE(identity.compare(sigIntegerRet, {}, sigI64Ret, {}, IdentityScope::kDeclaration) == SignatureRelation::kEquivalent);
    EXPECT_TRUE(identity.compare(sigIntegerRet, {}, sigTrusted, {}, IdentityScope::kDeclaration) == SignatureRelation::kTailMismatch);
}

TEST_F(FnIdentityFixture, InternKeyMatchesStructuralTagDerivation) {
    // ОДИН источник layout слотов: путь getOrCreateStructuralType (tag выводится из TypeData)
    // и путь getOrCreateFunctionType (tag из аргументов) обязаны давать один и тот же TypeId.
    TypeSystem& reg = *m_types;
    const TypeId i32 = reg.read().getType(type::Int32);
    const TypeId any = reg.read().getType(type_generic::Any);
    const TypeId t = reg.interner().getOrCreateTemplateParamType("T");

    const TypeKind callable = makeTypeKind(Group::kCallable, 1);
    const TypeId directVariadic = reg.interner().getOrCreateStructuralType("", callable, {TypeKeyChild{i32}, TypeKeyChild{i32}, TypeKeyChild{any}}, FunctionTypeData{i32, {i32}, any, INVALID_TYPE_ID, 0});
    EXPECT_EQ(directVariadic, reg.interner().getOrCreateFunctionType(i32, {i32}, any));

    const TypeId directPack = reg.interner().getOrCreateStructuralType("", callable, {TypeKeyChild{i32}, TypeKeyChild{t}}, FunctionTypeData{i32, {}, INVALID_TYPE_ID, t, 1});
    EXPECT_EQ(directPack, reg.interner().getOrCreateFunctionType(i32, {}, INVALID_TYPE_ID, false, t, 1));

    const TypeKind trustedCallable = setTrustFlag(makeTypeKind(Group::kCallable, 1));
    const TypeId directTrusted =
        reg.interner().getOrCreateStructuralType("", trustedCallable, {TypeKeyChild{i32}, TypeKeyChild{i32}}, FunctionTypeData{i32, {i32}, INVALID_TYPE_ID, INVALID_TYPE_ID, 0});
    EXPECT_EQ(directTrusted, reg.interner().getOrCreateFunctionType(i32, {i32}, INVALID_TYPE_ID, /*hasTrust=*/true));
}

TEST_F(FnIdentityFixture, VoidReturnSpellingsAreOneCppType) {
    // Позиция ВОЗВРАТА: нет аннотации (INVALID), `Void` и `None` кодогенер эмитит как `void`,
    // поэтому для C++-идентичности это ОДИН тип (иначе forward/definition даёт ложный
    // SignatureMismatch). В позиции ПАРАМЕТРА правило другое: INVALID = нетипизированный Any.
    TypeSystem& reg = *m_types;
    const TypeId i32 = reg.read().getType(type::Int32);
    const TypeId voidType = reg.read().getType(type::Void);
    const TypeId noneType = reg.read().getType(type::None);
    ASSERT_NE(voidType, INVALID_TYPE_ID);
    ASSERT_NE(noneType, INVALID_TYPE_ID);
    const TypeId sigNoRet = reg.interner().getOrCreateFunctionType(INVALID_TYPE_ID, {i32});
    const TypeId sigVoid = reg.interner().getOrCreateFunctionType(voidType, {i32});
    const TypeId sigNone = reg.interner().getOrCreateFunctionType(noneType, {i32});
    const TypeId sigI32 = reg.interner().getOrCreateFunctionType(i32, {i32});
    const FunctionIdentity& identity = reg.identity();
    EXPECT_EQ(identity.compare(sigNoRet, {}, sigVoid, {}, IdentityScope::kDeclaration), SignatureRelation::kEquivalent);
    EXPECT_EQ(identity.compare(sigVoid, {}, sigNone, {}, IdentityScope::kDeclaration), SignatureRelation::kEquivalent);
    EXPECT_EQ(identity.compare(sigVoid, {}, sigI32, {}, IdentityScope::kDeclaration), SignatureRelation::kTailMismatch);

    // Вложенный функциональный тип: return-код входит в C++-идентичность типа параметра.
    const TypeId cbNoRet = reg.interner().getOrCreateFunctionType(INVALID_TYPE_ID, {});
    const TypeId cbVoid = reg.interner().getOrCreateFunctionType(voidType, {});
    const TypeId sigCbNoRet = reg.interner().getOrCreateFunctionType(i32, {cbNoRet});
    const TypeId sigCbVoid = reg.interner().getOrCreateFunctionType(i32, {cbVoid});
    EXPECT_TRUE(identity.sameCppOverload(sigCbNoRet, {}, sigCbVoid, {}));

    // Параметр: нетипизированный (Any) и `:Void` — разные типы (эмитятся как std::any и void).
    const TypeId sigUntypedParam = reg.interner().getOrCreateFunctionType(i32, {INVALID_TYPE_ID});
    const TypeId sigVoidParam = reg.interner().getOrCreateFunctionType(i32, {voidType});
    EXPECT_FALSE(identity.sameCppOverload(sigUntypedParam, {}, sigVoidParam, {}));
}

TEST_F(FnIdentityFixture, CompareScopesDistinguishOverloadAndDeclaration) {
    TypeSystem& reg = *m_types;
    const TypeId i32 = reg.read().getType(type::Int32);
    const TypeId i64 = reg.read().getType(type::Int64);
    const TypeId sigRet32 = reg.interner().getOrCreateFunctionType(i32, {i32});
    const TypeId sigRet64 = reg.interner().getOrCreateFunctionType(i64, {i32});
    const TypeId sigParam64 = reg.interner().getOrCreateFunctionType(i32, {i64});
    const FunctionIdentity& identity = reg.identity();

    EXPECT_EQ(identity.compare(sigRet32, {}, sigRet64, {}, IdentityScope::kOverload), SignatureRelation::kEquivalent);
    EXPECT_EQ(identity.compare(sigRet32, {}, sigRet64, {}, IdentityScope::kDeclaration), SignatureRelation::kTailMismatch);
    EXPECT_EQ(identity.compare(sigRet32, {}, sigParam64, {}, IdentityScope::kOverload), SignatureRelation::kDifferent);
    EXPECT_EQ(identity.compare(sigRet32, {}, sigParam64, {}, IdentityScope::kDeclaration), SignatureRelation::kDifferent);
    // kInterop: пользовательский алиас раскрыт (внешний C++ не различает `using`).
    const TypeId alias = reg.registrar().registerType("MyInt", i32);
    ASSERT_NE(alias, INVALID_TYPE_ID);
    const TypeId sigAlias = reg.interner().getOrCreateFunctionType(i32, {alias});
    EXPECT_EQ(identity.compare(sigAlias, {}, sigRet32, {}, IdentityScope::kOverload), SignatureRelation::kDifferent);
    EXPECT_EQ(identity.compare(sigAlias, {}, sigRet32, {}, IdentityScope::kInterop), SignatureRelation::kEquivalent);
}

// R2/E6: одно вычисление на факт. `identityOf` идемпотентен (повтор по уже
// канонизированному id даёт тот же id — единственный путь канонизации).
TEST_F(FnIdentityFixture, IdentityOfIsIdempotentSinglePath) {
    TypeSystem& reg = *m_types;
    const TypeId i32 = reg.read().getType(type::Int32);
    const TypeId arr = reg.interner().getOrCreateArrayType(i32);
    const TypeStore& store = reg.read().store();
    const TypeId once = store.identityOf(arr, TypeIdentityScope::kCanonical);
    const TypeId twice = store.identityOf(once, TypeIdentityScope::kCanonical);
    EXPECT_EQ(once, twice);
}

// R2/E6/R5: отношения идентичности детерминированы и идут единственным путём
// (внутренний канонический код недоступен извне — проверяется через публичные `compare`/
// `sameCppOverload`).
TEST_F(FnIdentityFixture, IdentityRelationsAreDeterministicSinglePath) {
    TypeSystem& reg = *m_types;
    const TypeId i32 = reg.read().getType(type::Int32);
    const TypeId sig1 = reg.interner().getOrCreateFunctionType(i32, {i32});
    const TypeId sig2 = reg.interner().getOrCreateFunctionType(i32, {i32});
    const FunctionIdentity& id = reg.identity();
    const SignatureRelation r1 = id.compare(sig1, {}, sig2, {}, IdentityScope::kOverload);
    const SignatureRelation r2 = id.compare(sig1, {}, sig2, {}, IdentityScope::kOverload);
    EXPECT_EQ(r1, r2);
    EXPECT_EQ(r1, SignatureRelation::kEquivalent);
    EXPECT_TRUE(id.sameCppOverload(sig1, {}, sig2, {}));
}

} // namespace
} // namespace trust
