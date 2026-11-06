// FunctionIdentity: the single implementation of function-signature identity.
#include "types/func/fn_identity.hpp"
#include "types/registry/type_code_cache.hpp"
#include "types/registry/type_printer.hpp"
#include "types/registry/type_store.hpp"
#include "types/group.hpp"
#include "types/type/ref_type.hpp"
#include "types/typekind.hpp"
#include "utils/error.hpp"

#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

namespace trust {

namespace {

/// Identifier-safe, INJECTIVE escaping: [A-Za-z0-9] is preserved, every other byte (including
/// '_' and ':') becomes `_HH`. Because '_' itself is escaped, `_HH` can never be confused with
/// a literal underscore sequence - distinct canonical codes always produce distinct suffixes.
std::string escapeIdentifier(std::string_view text) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(text.size());
    for (const unsigned char c : text) {
        const bool safe = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
        if (safe) {
            out += static_cast<char>(c);
        } else {
            out += '_';
            out += kHex[(c >> 4) & 0x0F];
            out += kHex[c & 0x0F];
        }
    }
    return out;
}

} // namespace

FunctionIdentity::FunctionIdentity(const TypeStore& store, const TypePrinter& printer, TypeCodeCache& codes)
: m_store(store)
, m_printer(printer)
, m_codes(codes) {
}

bool FunctionIdentity::sameSemanticType(TypeId a, TypeId b) const {
    return sameSemanticType(a, b, {}, {});
}

bool FunctionIdentity::sameSemanticType(TypeId a, TypeId b, const TemplateBinders& aBinders, const TemplateBinders& bBinders) const {
    if (getUnqualifiedType(a) == getUnqualifiedType(b) && aBinders == bBinders) {
        return true;
    }
    // Binder-free fast path: the semantic identity relation (builtin aliases resolved, user aliases
    // nominal) is already the exact relation `typeCode(kSemantic)` encodes; equal identity ids mean
    // equal canonical codes. Avoids building strings for the common non-generic case.
    if (aBinders.empty() && bBinders.empty() && m_store.identityOf(a, TypeIdentityScope::kSemantic) == m_store.identityOf(b, TypeIdentityScope::kSemantic)) {
        return true;
    }
    return typeCode(a, aBinders, 0, TypeIdentityScope::kSemantic) == typeCode(b, bBinders, 0, TypeIdentityScope::kSemantic);
}

bool FunctionIdentity::sameCppOverload(TypeId a, const TemplateBinders& aBinders, TypeId b, const TemplateBinders& bBinders) const {
    if (getUnqualifiedType(a) == getUnqualifiedType(b) && aBinders == bBinders) {
        return true;
    }
    return signatureSegments(a, aBinders, TypeIdentityScope::kSemantic) == signatureSegments(b, bBinders, TypeIdentityScope::kSemantic);
}

bool FunctionIdentity::sameInteropOverload(TypeId a, const TemplateBinders& aBinders, TypeId b, const TemplateBinders& bBinders) const {
    if (getUnqualifiedType(a) == getUnqualifiedType(b) && aBinders == bBinders) {
        return true;
    }
    return signatureSegments(a, aBinders, TypeIdentityScope::kCanonical) == signatureSegments(b, bBinders, TypeIdentityScope::kCanonical);
}

SignatureRelation FunctionIdentity::compare(TypeId a, const TemplateBinders& aBinders, TypeId b, const TemplateBinders& bBinders, IdentityScope scope) const {
    const bool paramsEqual = [&] {
        switch (scope) {
        case IdentityScope::kInterop:
            return sameInteropOverload(a, aBinders, b, bBinders);
        case IdentityScope::kOverload:
        case IdentityScope::kDeclaration:
            return sameCppOverload(a, aBinders, b, bBinders);
        }
        EXPECT(false && "FunctionIdentity::compare: unknown identity scope");
        return false;
    }();
    if (!paramsEqual) {
        return SignatureRelation::kDifferent;
    }
    if (scope != IdentityScope::kDeclaration) {
        return SignatureRelation::kEquivalent;
    }
    // kDeclaration: параметры уже сопоставлены; хвост (return + trust) решает контракт.
    return sameReturnAndTrust(a, aBinders, b, bBinders) ? SignatureRelation::kEquivalent : SignatureRelation::kTailMismatch;
}

bool FunctionIdentity::sameReturnAndTrust(TypeId a, const TemplateBinders& aBinders, TypeId b, const TemplateBinders& bBinders) const {
    if (getUnqualifiedType(a) == getUnqualifiedType(b) && aBinders == bBinders) {
        return true;
    }
    const auto tail = [this](TypeId signature, const TemplateBinders& binders) {
        const TypeId id = m_store.identityOf(signature, TypeIdentityScope::kCanonical);
        const auto* fd = m_store.getTypeDataAs<FunctionTypeData>(id);
        EXPECT(fd != nullptr && "FunctionIdentity: signature must be a function type");
        return std::pair<std::string, bool>{returnTypeCode(fd->returnType, binders, 0, TypeIdentityScope::kSemantic), isTrustedType(id)};
    };
    return tail(a, aBinders) == tail(b, bBinders);
}

FunctionIdentity::Segments FunctionIdentity::signatureSegments(TypeId signature, const TemplateBinders& binders, TypeIdentityScope scope) const {
    const TypeId id = m_store.identityOf(signature, TypeIdentityScope::kCanonical);
    const auto* fd = m_store.getTypeDataAs<FunctionTypeData>(id);
    EXPECT(fd != nullptr && "FunctionIdentity: signature must be a function type");
    Segments segments;
    segments.reserve(fd->paramTypes.size() + 4);
    for (const TypeId param : fd->paramTypes) {
        segments.push_back(Segment{'P', 0, typeCode(param, binders, 0, scope)});
    }
    if (fd->variadicType != INVALID_TYPE_ID) {
        segments.push_back(Segment{'V', 0, typeCode(fd->variadicType, binders, 0, scope)});
    }
    if (fd->paramPackType != INVALID_TYPE_ID) {
        EXPECT(fd->paramPackPosition != 0 && "FunctionIdentity: a pack slot requires a non-zero position");
        segments.push_back(Segment{'K', fd->paramPackPosition, typeCode(fd->paramPackType, binders, 0, scope)});
    } else {
        EXPECT(fd->paramPackPosition == 0 && "FunctionIdentity: paramPackPosition must be 0 without a pack slot");
    }
    return segments;
}

std::string FunctionIdentity::cppSuffix(TypeId signature, const TemplateBinders& binders) const {
    const Segments segments = signatureSegments(signature, binders, TypeIdentityScope::kSemantic);
    std::size_t fixedCount = 0;
    for (const Segment& segment : segments) {
        if (segment.tag == 'P') {
            ++fixedCount;
        }
    }
    std::string suffix;
    if (fixedCount == 0) {
        suffix = "_P0";
    }
    for (const Segment& segment : segments) {
        const std::string code = escapeIdentifier(segment.code);
        switch (segment.tag) {
        case 'P':
            suffix += "_P" + std::to_string(code.size()) + code;
            break;
        case 'V':
            suffix += "_V" + std::to_string(code.size()) + code;
            break;
        case 'K':
            suffix += "_K" + std::to_string(static_cast<unsigned>(segment.position)) + "_P" + std::to_string(code.size()) + code;
            break;
        default:
            EXPECT(false && "FunctionIdentity: unexpected signature segment tag");
            break;
        }
    }
    return suffix + "_";
}

SlotLayoutTag FunctionIdentity::slotLayout(std::size_t paramCount, bool hasVariadic, uint8_t paramPackPosition) {
    EXPECT(static_cast<std::size_t>(static_cast<uint32_t>(paramCount)) == paramCount && "FunctionIdentity: parameter count does not fit the slot layout tag");
    return SlotLayoutTag{static_cast<uint32_t>(paramCount), hasVariadic, paramPackPosition};
}

TypeKey FunctionIdentity::internKey(TypeId returnType, std::span<const TypeId> paramTypes, TypeId variadicType, TypeId paramPackType, uint8_t paramPackPosition,
                                    bool hasTrust) const {
    std::vector<TypeKeyChild> children;
    children.reserve(1 + paramTypes.size() + (variadicType != INVALID_TYPE_ID ? 1 : 0) + (paramPackType != INVALID_TYPE_ID ? 1 : 0));
    children.push_back(TypeKeyChild{getUnqualifiedType(returnType)});
    for (const TypeId param : paramTypes) {
        children.push_back(TypeKeyChild{getUnqualifiedType(param)});
    }
    if (variadicType != INVALID_TYPE_ID) {
        children.push_back(TypeKeyChild{getUnqualifiedType(variadicType)});
    }
    if (paramPackType != INVALID_TYPE_ID) {
        children.push_back(TypeKeyChild{getUnqualifiedType(paramPackType)});
    }

    // The flat children list cannot distinguish "last fixed parameter" from "variadic slot"
    // or "pack slot"; the slot layout is an identity discriminator (TypeKey::tag).
    // The trust bit is already part of TypeKey::kind and is not duplicated here.
    const SlotLayoutTag layout = slotLayout(paramTypes.size(), variadicType != INVALID_TYPE_ID, paramPackPosition);

    TypeKind kind = makeTypeKind(Group::kCallable, 1);
    if (hasTrust) {
        kind = setTrustFlag(kind);
    }
    return TypeKey{kind, std::move(children), StructuralTag{layout}};
}

} // namespace trust
