#pragma once

// include/semantic/ref/ref_qualifiers.hpp
// Единственная реализация применения ортогональных квалификаторов объявления/аннотации:
// const, вид ссылки (reftype), deleter, lifetime, pin. Используется анализаторами объявлений
// и резолвом аннотаций типов.

#include "semantic/ref/ref_kind.hpp"
#include "semantic/core/type_resolver.hpp"
#include "ast/ref_syntax.hpp"

#include <string>
#include <vector>

namespace trust {

[[nodiscard]] inline QualType applyDeclarationQualifiers(Context& ctx, const TypeResolution& types, QualType base, const AstNodeAttr& node, MapperRange range) {
    if (!base.isValid()) {
        const AttrPool& attrs0 = ctx.attrs();
        if (const auto did0 = attrs0.lookup(attr::Deleter); did0.has_value() && node.has_attr(*did0)) {
            ctx.diag().report(Severity::Error, range,
                              "attribute 'deleter' requires an explicit type annotation (the resource value type cannot be inferred); "
                              "use '<name> : @[reftype(...)@] @[deleter(D)@] <Type> := <handle>'");
        }
        return base;
    }
    const AttrPool& attrs = ctx.attrs();
    if (node.has_attr(attrs, attr::ReadOnly)) {
        base = base.withConst();
    }
    auto reftype_id = attrs.lookup(attr::Reftype);
    if (reftype_id.has_value() && node.has_attr(*reftype_id)) {
        const std::vector<std::string>* rargs = node.attr_args(*reftype_id);
        if (!rargs || rargs->empty()) {
            ctx.diag().report(Severity::Error, range, "attribute 'reftype' requires a reference-kind parameter, e.g. @[reftype(\"ptr\")]");
        } else {
            auto refkind = refKindFromAttrArgs(rargs);
            if (!refkind) {
                ctx.diag().report(Severity::Error, range, "unknown reference kind '{}'", rargs->front());
            } else {
                if (rargs->size() > 3) {
                    ctx.diag().report(Severity::Error, range, "attribute 'reftype' accepts at most 3 arguments: kind[, access_policy[, impl]]");
                }
                const TypeId accessPolicy = resolveAccessPolicy(ctx, rargs, *refkind, range);
                const TypeId impl = resolveImpl(ctx, rargs, *refkind, accessPolicy, range);
                base = applyDeclaredRefKind(ctx, base, *refkind, range, /*subjectName=*/{}, /*actualLabel=*/"type", RefNameStyle::Mnemonic, accessPolicy, impl);
            }
        }
    }
    auto deleter_attr = attrs.lookup(attr::Deleter);
    if (deleter_attr.has_value() && node.has_attr(*deleter_attr)) {
        const std::vector<std::string>* dargs = node.attr_args(*deleter_attr);
        const RefType curKind = getRefType(getTypeKind(base.getTypeId()));
        if (!dargs || dargs->empty()) {
            ctx.diag().report(Severity::Error, range, "attribute 'deleter' requires a deleter type parameter, e.g. @[deleter(FreeDeleter)]");
        } else if (dargs->size() != 1) {
            ctx.diag().report(Severity::Error, range, "attribute 'deleter' accepts exactly one argument: the deleter type name");
        } else if (checkQualifierApplicable(ctx, attr::Deleter, curKind, range)) {
            TypeSystem& reg = ctx.types();
            const auto did = types.lookupTypeName(dargs->front());
            if (!did.has_value() || (!reg.read().store().isDeleterPolicyType(*did) && !reg.read().store().isNativeClassType(*did))) {
                ctx.diag().report(Severity::Error, range, "unknown deleter type '{}' (expected a registered deleter type, e.g. FreeDeleter, or a native class)",
                                  dargs->front());
            } else if (curKind == RefType::kUnique) {
                base = reg.interner().applyRefType(reg.read().store().getPointeeType(base), RefType::kUnique, *did);
            }
        }
    }
    if (const auto lt = attrs.lookup(attr::Lifetime); lt.has_value() && node.has_attr(*lt)) {
        validateLifetimeArgs(ctx, node.attr_args(*lt), getRefType(getTypeKind(base.getTypeId())), range);
    }
    if (const auto pinid = attrs.lookup(attr::Pin); pinid.has_value() && node.has_attr(*pinid)) {
        checkQualifierApplicable(ctx, attr::Pin, getRefType(getTypeKind(base.getTypeId())), range);
    }
    return base;
}

} // namespace trust
