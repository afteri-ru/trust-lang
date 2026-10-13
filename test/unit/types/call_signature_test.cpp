// test/unit/types/call_signature_test.cpp
// Интернирование СИГНАТУРЫ ВЫЗОВА: instantiateCallSignature (types/subst/template_subst.hpp:
// подстановка bindings + разворот типового пака) и сохранение метаданных вызова у инстанциата
// шаблон-класса.

#include "types/type_system.hpp"
#include "types/subst/template_subst.hpp"
#include "types/names.hpp"
#include "types/call.hpp"
#include "session/context.hpp"
#include "gtest/gtest.h"

#include <memory>
#include <string>
#include <vector>

namespace trust {
namespace {

class CallSignatureFixture : public ::testing::Test {
  protected:
    void SetUp() override {
        m_types = std::make_unique<TypeSystem>(m_ctx.diag(), m_ctx.opts());
        m_ctx.setTypes(m_types.get());
    }
    Context m_ctx;
    std::unique_ptr<TypeSystem> m_types;
};

TEST_F(CallSignatureFixture, NonGenericIsIdentity) {
    TypeSystem& reg = m_ctx.types();
    const TypeId i32 = reg.read().getType(type::Int32);
    const TypeId sig = reg.interner().getOrCreateFunctionType(i32, {i32});
    const TemplateBindings none;
    const std::vector<TypeId> args{i32};
    // Подставлять нечего - эффективная сигнатура та же (единственный интернированный экземпляр).
    EXPECT_EQ(instantiateCallSignature({reg.read(), reg.interner()}, sig, none, args), sig);
}

TEST_F(CallSignatureFixture, GenericSubstitutesParamsAndReturn) {
    TypeSystem& reg = m_ctx.types();
    const TypeId i32 = reg.read().getType(type::Int32);
    const TypeId t = reg.interner().getOrCreateTemplateParamType("T");
    const TypeId sig = reg.interner().getOrCreateFunctionType(t, {t});
    const TemplateBindings bindings{{t, false, {i32}}};
    const std::vector<TypeId> args{i32};
    const TypeId eff = instantiateCallSignature({reg.read(), reg.interner()}, sig, bindings, args);
    ASSERT_NE(eff, INVALID_TYPE_ID);
    EXPECT_NE(eff, sig); // объявленная (с T) и эффективная (с Int32) - разные типы
    const auto* fd = reg.read().store().getTypeDataAs<FunctionTypeData>(eff);
    ASSERT_NE(fd, nullptr);
    EXPECT_EQ(fd->returnType, i32);
    EXPECT_EQ(fd->paramTypes, std::vector<TypeId>{i32});
    // Интернирование: тот же (declaredSig, argTypes) даёт тот же TypeId.
    EXPECT_EQ(instantiateCallSignature({reg.read(), reg.interner()}, sig, bindings, args), eff);
}

TEST_F(CallSignatureFixture, PackIsExpandedToFlatParams) {
    TypeSystem& reg = m_ctx.types();
    const TypeId i32 = reg.read().getType(type::Int32);
    const TypeId str = reg.read().getType(type::StrChar);
    const TypeId t = reg.interner().getOrCreateTemplateParamType("T");
    // `f(a:Int64, xs:T...) -> Int32`: pack-слот отдельный (paramPackPosition = 1).
    const TypeId sig = reg.interner().getOrCreateFunctionType(i32, {i32}, INVALID_TYPE_ID, false, t, 1);
    const TemplateBindings bindings{{t, true, {i32, str}}};
    const std::vector<TypeId> args{i32, i32, str};
    const TypeId eff = instantiateCallSignature({reg.read(), reg.interner()}, sig, bindings, args);
    ASSERT_NE(eff, INVALID_TYPE_ID);
    const auto* fd = reg.read().store().getTypeDataAs<FunctionTypeData>(eff);
    ASSERT_NE(fd, nullptr);
    EXPECT_EQ(fd->paramTypes, (std::vector<TypeId>{i32, i32, str})); // пак развёрнут в плоский список
    EXPECT_EQ(fd->paramPackType, INVALID_TYPE_ID);                   // pack-слота в вызове нет
    EXPECT_EQ(fd->paramPackPosition, 0);
    // Порядок аргументов пака значим: другая последовательность - другая сигнатура вызова.
    // (Порядок в bindings не влияет: идентичность вызова определяется ФАКТИЧЕСКИМИ типами аргументов.)
    const std::vector<TypeId> swappedArgs{i32, str, i32};
    EXPECT_NE(instantiateCallSignature({reg.read(), reg.interner()}, sig, bindings, swappedArgs), eff);
    const TemplateBindings sameTypesOtherOrder{{t, true, {str, i32}}};
    EXPECT_EQ(instantiateCallSignature({reg.read(), reg.interner()}, sig, sameTypesOtherOrder, args), eff);
}

TEST_F(CallSignatureFixture, DefaultsDoNotChangeArity) {
    TypeSystem& reg = m_ctx.types();
    const TypeId i32 = reg.read().getType(type::Int32);
    // Сигнатура объявляет два параметра; вызов передал один (второй - default в объявлении).
    const TypeId sig = reg.interner().getOrCreateFunctionType(i32, {i32, i32});
    const TemplateBindings none;
    const std::vector<TypeId> args{i32};
    const TypeId eff = instantiateCallSignature({reg.read(), reg.interner()}, sig, none, args);
    const auto* fd = reg.read().store().getTypeDataAs<FunctionTypeData>(eff);
    ASSERT_NE(fd, nullptr);
    EXPECT_EQ(fd->paramTypes.size(), 2u); // арность меняет ТОЛЬКО пак
}

TEST_F(CallSignatureFixture, TrustFlagPreserved) {
    TypeSystem& reg = m_ctx.types();
    const TypeId i32 = reg.read().getType(type::Int32);
    const TypeId t = reg.interner().getOrCreateTemplateParamType("T");
    const TypeId trusted = reg.interner().getOrCreateFunctionType(t, {t}, INVALID_TYPE_ID, /*hasTrust=*/true);
    const TemplateBindings bindings{{t, false, {i32}}};
    const std::vector<TypeId> args{i32};
    const TypeId eff = instantiateCallSignature({reg.read(), reg.interner()}, trusted, bindings, args);
    // trust-флаги - часть идентичности: подстановка обязана их сохранить.
    EXPECT_TRUE(isTrustedType(eff));
    EXPECT_NE(eff, reg.interner().getOrCreateFunctionType(i32, {i32}));
}

TEST_F(CallSignatureFixture, ElementParamSubstitutesNestedTypes) {
    TypeSystem& reg = m_ctx.types();
    const TypeId i32 = reg.read().getType(type::Int32);
    const TypeId tp = reg.read().getType(type_category::TemplateParam); // встроенный параметр контейнера
    const TypeId arrT = reg.interner().getOrCreateArrayType(tp);
    const TypeId sig = reg.interner().getOrCreateFunctionType(tp, {arrT});
    const TypeId arrI32 = reg.interner().getOrCreateArrayType(i32);
    // instantiateArrayMethod подставляет T→Elem РЕКУРСИВНО (nested Array<T>), а не только точный T.
    const TypeId eff = reg.interner().instantiateArrayMethod(arrI32, sig);
    const auto* fd = reg.read().store().getTypeDataAs<FunctionTypeData>(eff);
    ASSERT_NE(fd, nullptr);
    EXPECT_EQ(fd->returnType, i32);
    ASSERT_EQ(fd->paramTypes.size(), 1u);
    EXPECT_EQ(fd->paramTypes[0], arrI32);
}

TEST_F(CallSignatureFixture, TemplateInstanceKeepsMethodParams) {
    TypeSystem& reg = m_ctx.types();
    const TypeId i32 = reg.read().getType(type::Int32);
    const TypeId t = reg.interner().getOrCreateTemplateParamType("T");
    const TypeId tmpl = reg.registrar().declareRecord("Box", Group::kClassDefs, /*isTemplate=*/true);
    reg.registrar().setRecordTemplateParams(tmpl, {t});
    ASSERT_NE(tmpl, INVALID_TYPE_ID);
    ASSERT_NE(reg.registrar().defineRecord("Box", Group::kClassDefs, {{"v", QualType::fromTypeId(t)}}), INVALID_TYPE_ID);

    CallableParamInfo info;
    info.names = {"a", "b"};
    info.hasDefault = {false, true};
    reg.methods().addMethod(tmpl, "f", reg.interner().getOrCreateFunctionType(i32, {i32, i32}), {}, info);

    const TypeId inst = reg.interner().getOrCreateRecordTemplateInstance(tmpl, {i32});
    ASSERT_NE(inst, INVALID_TYPE_ID);
    const auto method = reg.read().methods().findMethodInfo(inst, "f");
    ASSERT_TRUE(method.has_value());
    // Инвариант CallableParamInfo: metadata в том же элементе набора и у инстанса шаблона.
    ASSERT_EQ(method->entries.size(), 1u);
    EXPECT_EQ(method->entries[0].params.names, (std::vector<std::string>{"a", "b"}));
    EXPECT_EQ(method->entries[0].params.hasDefault, (std::vector<bool>{false, true}));
}
TEST_F(CallSignatureFixture, TemplateParamOwnerScopingDistinguishesSiblingTemplates) {
    // Owner-scoping (C++-модель TemplateTypeParmDecl): одинаковое имя параметра в РАЗНЫХ шаблонах
    // даёт разные TypeId (принадлежность параметра своему шаблону), а не общий «глобальный T».
    TypeSystem& reg = m_ctx.types();
    const TypeId a = reg.registrar().declareRecord("A", Group::kClassDefs, /*isTemplate=*/true);
    const TypeId b = reg.registrar().declareRecord("B", Group::kClassDefs, /*isTemplate=*/true);
    const TypeId ta = reg.interner().getOrCreateTemplateParamType("T", a, /*depth=*/0, /*index=*/0);
    const TypeId tb = reg.interner().getOrCreateTemplateParamType("T", b, /*depth=*/0, /*index=*/0);
    const TypeId tNameOnly = reg.interner().getOrCreateTemplateParamType("T"); // не-scoped (generic-функция)
    EXPECT_NE(ta, tb) << "same name, different owners -> distinct param identity";
    EXPECT_NE(ta, tNameOnly);
    EXPECT_NE(tb, tNameOnly);
    // Позиция в списке параметров одного шаблона тоже различает (`T` vs `U` по index).
    const TypeId t0 = reg.interner().getOrCreateTemplateParamType("T", a, 0, 0);
    const TypeId t1 = reg.interner().getOrCreateTemplateParamType("T", a, 0, 1);
    EXPECT_NE(t0, t1) << "same owner+name, different index -> distinct param identity";
    reg.registrar().setRecordTemplateParams(a, {ta});
    reg.registrar().setRecordTemplateParams(b, {tb});
}

TEST_F(CallSignatureFixture, RecordTemplateInstanceMemoizesMethods) {
    // Специализация (templateId, args) — канонический TypeId через структурное интернирование;
    // повторный запрос того же инстанса ВОЗВРАЩАЕТ тот же TypeId и НЕ дублирует методы.
    TypeSystem& reg = m_ctx.types();
    const TypeId i32 = reg.read().getType(type::Int32);
    const TypeId t = reg.interner().getOrCreateTemplateParamType("T");
    const TypeId tmpl = reg.registrar().declareRecord("MemoBox", Group::kClassDefs, /*isTemplate=*/true);
    reg.registrar().setRecordTemplateParams(tmpl, {t});
    ASSERT_NE(reg.registrar().defineRecord("MemoBox", Group::kClassDefs, {{"v", QualType::fromTypeId(t)}}), INVALID_TYPE_ID);
    CallableParamInfo info;
    info.names = {"x"};
    reg.methods().addMethod(tmpl, "get", reg.interner().getOrCreateFunctionType(i32, {i32}), {}, info);
    const TypeId inst1 = reg.interner().getOrCreateRecordTemplateInstance(tmpl, {i32});
    ASSERT_NE(inst1, INVALID_TYPE_ID);
    const TypeId inst2 = reg.interner().getOrCreateRecordTemplateInstance(tmpl, {i32});
    EXPECT_EQ(inst2, inst1) << "специализация (templateId,args) мемоизируется каноническим TypeId";
    const auto m1 = reg.read().methods().findMethodInfo(inst1, "get");
    ASSERT_TRUE(m1.has_value());
    EXPECT_EQ(m1->entries.size(), 1u);
    // Методы не продублированы повторным запросом того же инстанса.
    EXPECT_EQ(reg.read().methods().findMethodInfo(inst2, "get")->entries.size(), 1u);
}

TEST_F(CallSignatureFixture, RecordTemplateInstanceClassifiedAsImplicitSpecialization) {
    // S1: record-инстанциация (templateId, args) — имплицитная специализация; primary-шаблон,
    // обычный record — kNone (не специализация).
    TypeSystem& reg = m_ctx.types();
    const TypeId i32 = reg.read().getType(type::Int32);
    const TypeId t = reg.interner().getOrCreateTemplateParamType("T");
    const TypeId tmpl = reg.registrar().declareRecord("KindBox", Group::kClassDefs, /*isTemplate=*/true);
    reg.registrar().setRecordTemplateParams(tmpl, {t});
    ASSERT_NE(reg.registrar().defineRecord("KindBox", Group::kClassDefs, {{"v", QualType::fromTypeId(t)}}), INVALID_TYPE_ID);

    // Primary-шаблон — «не специализация» (аналог Clang Undeclared).
    EXPECT_EQ(reg.read().store().recordSpecializationKind(tmpl), TemplateSpecializationKind::kNone);
    // Инстанциация — имплицитная специализация (TemplateSpecializationKind).
    const TypeId inst = reg.interner().getOrCreateRecordTemplateInstance(tmpl, {i32});
    ASSERT_NE(inst, INVALID_TYPE_ID);
    EXPECT_EQ(reg.read().store().recordSpecializationKind(inst), TemplateSpecializationKind::kImplicitInstantiation);
    // Один canonical узел на (templateId, args): повторный запрос — тот же TypeId и тот же kind.
    const TypeId inst2 = reg.interner().getOrCreateRecordTemplateInstance(tmpl, {i32});
    EXPECT_EQ(inst2, inst);
    EXPECT_EQ(reg.read().store().recordSpecializationKind(inst2), TemplateSpecializationKind::kImplicitInstantiation);
    // Обычный (не-шаблонный) record — kNone.
    const TypeId plain = reg.registrar().defineRecord("PlainKind", Group::kStructs, {{"x", QualType::fromTypeId(i32)}});
    EXPECT_EQ(reg.read().store().recordSpecializationKind(plain), TemplateSpecializationKind::kNone);
}

} // namespace
} // namespace trust
