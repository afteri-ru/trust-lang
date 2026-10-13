#pragma once

// include/types/func/fn_identity.hpp
// SINGLE source of FUNCTION SIGNATURE IDENTITY (C++ model).
//
// Responsibilities:
//   - canonical code of a type under an explicit `TypeIdentityScope` (kSemantic | kCanonical):
//     `kCanonical` resolves aliases at EVERY nesting level; `kSemantic` keeps a user alias nominal
//     and expands only builtin representation aliases. Template parameters are canonicalized by
//     BINDER POSITION (declaration order); parameter packs are a separate binder kind
//     (C++-like <T> vs <T...>);
//   - C++ overload identity (parameter types + variadic + pack + pack position; return type
//     and trust flags are NOT part of the C++ identity);
//   - declaration identity (overload identity + return type + trust) used for duplicates and
//     forward/definition completion - exposed as a TRI-STATE relation (`compare`), so a caller
//     cannot check only half of the contract;
//   - deterministic, injective C++ name suffix derived from the SAME overload identity that
//     the duplicate/exact checks use (one projection: name == identity);
//   - interning key of FunctionTypeData (the trust flag is carried by TypeKind, not by the tag).
//
// Relation to TypeSystem (see the comment at TypeSystem::getOrCreateFunctionType):
//   FunctionIdentity BUILDS the identity/key only; StructuralInterner OWNS the interning table
//   and is the only place that mutates it. FunctionIdentity reads the
//   registry (const) for canonicalization and never mutates it (the type-code cache is an
//   internal memo, see TypeSystem).

#include "types/call/binders.hpp"
#include "types/registry/type_identity_scope.hpp"
#include "types/type_data.hpp"
#include "types/type_id.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace trust {

class TypeCodeCache;
class TypePrinter;
class TypeStore;

/// Область сравнения сигнатур. Отношения различаются политикой раскрытия алиасов и тем,
/// входит ли в контракт хвост объявления (return type + trust).
enum class IdentityScope : uint8_t {
    kOverload,    ///< параметры + variadic + pack (C++-идентичность перегрузки, kTrust)
    kDeclaration, ///< overload + return type + trust (полный контракт объявления)
    kInterop,     ///< параметры для ВНЕШНЕГО C++-символа (TypeIdentityScope::kCanonical: `using` раскрыт)
};

/// Результат сравнения сигнатур в заданной области.
enum class SignatureRelation : uint8_t {
    kDifferent,    ///< разные перегрузки (параметры C++-несовместимы)
    kEquivalent,   ///< одна и та же сигнатура в выбранной области
    kTailMismatch, ///< параметры совпали, но return type/trust не совпали (только kDeclaration)
};

class FunctionIdentity {
  public:
    FunctionIdentity(const TypeStore& store, const TypePrinter& printer, TypeCodeCache& codes);

    /// True if the types are the SAME SEMANTIC Trust type: structural shape is compared deeply,
    /// builtin representation aliases resolve, but USER aliases stay nominal (not C++-interop
    /// identity). For external C++ symbols use `sameInteropOverload` (`kCanonical`).
    [[nodiscard]] bool sameSemanticType(TypeId a, TypeId b) const;
    /// То же, но template-параметры канонизируются по ПОЗИЦИИ в переданных binder-списках
    /// (declaration-less вызов выше сравнивает их по имени).
    [[nodiscard]] bool sameSemanticType(TypeId a, TypeId b, const TemplateBinders& aBinders, const TemplateBinders& bBinders) const;

    /// C++ identity of an OVERLOAD: parameter types + variadic + pack + pack position.
    /// Template parameters are canonicalized by POSITION in the given binder lists. Return type
    /// and trust flags are excluded (C++ cannot overload on them). A USER alias is a distinct
    /// nominal Trust type; only builtin representation aliases resolve to their implementation.
    [[nodiscard]] bool sameCppOverload(TypeId a, const TemplateBinders& aBinders, TypeId b, const TemplateBinders& bBinders) const;

    /// FFI-идентичность для НАТИВНОГО C++-символа: внешний C++ не различает `using`-алиасы,
    /// поэтому здесь пользовательские алиасы раскрываются. Для Trust-функций используйте
    /// `sameCppOverload` (номинальная identity).
    [[nodiscard]] bool sameInteropOverload(TypeId a, const TemplateBinders& aBinders, TypeId b, const TemplateBinders& bBinders) const;

    /// ЕДИНАЯ точка сравнения сигнатур. `kOverload`/`kInterop` отвечают на вопрос «та же
    /// перегрузка?»; `kDeclaration` дополнительно различает kTailMismatch (параметры совпали,
    /// return type/trust - нет), что нужно для forward/definition completion.
    [[nodiscard]] SignatureRelation compare(TypeId a, const TemplateBinders& aBinders, TypeId b, const TemplateBinders& bBinders, IdentityScope scope) const;

    /// Deterministic C++ suffix for a function name. Format: `_P<len><code>_..._`,
    /// no parameters -> `_P0_`, variadic `_V<len><code>`, pack `_K<pos>_P<len><code>`.
    /// Derived from the SAME positional overload identity as `sameCppOverload`, so two
    /// declarations have the same C++ name iff they are the same C++ overload. The declaration
    /// and every call site use the declaration's own binder list (the suffix is stored on the
    /// declaration and copied to call sites, never recomputed without context).
    [[nodiscard]] std::string cppSuffix(TypeId signature, const TemplateBinders& binders) const;

    /// Typed identity tag of a functional signature slot layout (parameter count, variadic slot,
    /// pack position). The return type and the trust bit are encoded by the interning key
    /// separately, so they are not part of the layout. Shared with `TypeSystem` so every path
    /// that interns a FunctionTypeData derives the same tag from the data.
    [[nodiscard]] static SlotLayoutTag slotLayout(std::size_t paramCount, bool hasVariadic, uint8_t paramPackPosition);

    /// Interning key of a function type. The slot structure (parameter count, variadic slot,
    /// pack slot + position) is encoded in `TypeKey::tag`, so `f(a,b:Any)` != `f(a,...)`
    /// and `f(x:T)` != `f(xs:T...)` (the flat children list alone is ambiguous).
    [[nodiscard]] TypeKey internKey(TypeId returnType, std::span<const TypeId> paramTypes, TypeId variadicType, TypeId paramPackType, uint8_t paramPackPosition,
                                    bool hasTrust) const;

  private:
    /// One self-delimiting identity segment of a signature. `tag` distinguishes the slot kind
    /// ('P' fixed parameter, 'V' C-variadic slot, 'K' type-pack slot), `position` is the pack
    /// position (only meaningful for 'K'), `code` is the canonical type code of the slot.
    /// The SAME segment list is the single source of the overload identity (equality) and of
    /// the C++ name suffix (one projection: name == identity).
    struct Segment {
        char tag;
        uint8_t position; ///< pack position; 0 for non-pack slots
        std::string code; ///< canonical type code

        friend bool operator==(const Segment&, const Segment&) = default;
    };
    using Segments = std::vector<Segment>;

    /// Canonical segments of a signature in declaration order. Slots: 'P' fixed parameter,
    /// 'V' C-variadic, 'K' type-pack (+ position). Return type and trust are NOT part of the
    /// C++ overload identity (the declaration tail is `sameReturnAndTrust`).
    [[nodiscard]] Segments signatureSegments(TypeId signature, const TemplateBinders& binders, TypeIdentityScope scope) const;

    /// Declaration TAIL identity: return type + trust bit. Private: единственный публичный вход -
    /// `compare(..., kDeclaration)`, поэтому «половину» контракта снаружи проверить нельзя.
    [[nodiscard]] bool sameReturnAndTrust(TypeId a, const TemplateBinders& aBinders, TypeId b, const TemplateBinders& bBinders) const;

    [[nodiscard]] std::string typeCode(TypeId type, const TemplateBinders& binders, int depth, TypeIdentityScope scope) const;

    /// Canonical code of a RETURN position: нет аннотации (`INVALID`), `Void` и `None` — один
    /// C++-тип `void` (кодогенер эмитит `void` для всех трёх). В ПОЗИЦИИ ПАРАМЕТРА так делать
    /// нельзя: `INVALID` там означает нетипизированный `:Any` (`std::any`), а не `void`.
    [[nodiscard]] std::string returnTypeCode(TypeId type, const TemplateBinders& binders, int depth, TypeIdentityScope scope) const;

    /// Uncached canonical code of an ALREADY resolved structural id. Recursive calls go through
    /// the caching `typeCode` entry point, so every concrete subexpression is cached too.
    [[nodiscard]] std::string computeTypeCode(TypeId id, const TemplateBinders& binders, int depth, TypeIdentityScope scope) const;

    const TypeStore& m_store;
    /// Печать имён в кодах типов (`kDiagnostic`/номинальные имена структурных узлов).
    const TypePrinter& m_printer;
    /// Кеш кодов (per-instance, владелец — композиционный корень).
    TypeCodeCache& m_codes;
};

} // namespace trust
