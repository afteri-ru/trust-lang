#include "types/type_read.hpp"
#include "semantic/typer/arg_casts.hpp"

#include "ast/binary_op.hpp"
#include "session/context.hpp"
#include "types/type_system.hpp"
#include "types/type_data.hpp"
#include "types/names.hpp"

#include <cstddef>
#include <utility>
#include <vector>

namespace trust {
namespace {

/// Цель приведения аргумента вызова: Any/INVALID-параметр не приводится (контракт
/// `emitCastCallArgs`). Auto (внутренний sentinel «не выведен») - тоже без приведения.
TypeId callCastTarget(const TypeReadModel& reg, TypeId param) {
    if (param == INVALID_TYPE_ID) {
        return INVALID_TYPE_ID;
    }
    const TypeId c = reg.store().desugaredTypeId(param);
    const TypeId anyId = reg.store().desugaredTypeId(reg.getType(type_generic::Any));
    const TypeId autoId = reg.store().desugaredTypeId(reg.getType(type_generic::Auto));
    return (c == anyId || c == autoId) ? INVALID_TYPE_ID : param;
}

void fillCall(CallExpr& call, const TypeReadModel& reg) {
    // Обычный вызов - resolvedSignature; перегруженный `operator()` может иметь только
    // resolvedOperatorSignature (форма выбрана семантикой, effective-сигнатура не строилась).
    const TypeId signature = call.resolvedSignature != INVALID_TYPE_ID ? call.resolvedSignature : call.resolvedOperatorSignature;
    if (signature == INVALID_TYPE_ID) {
        return; // вызов не разрешён (диагностика уже выдана) - материализовать нечего
    }
    const auto* fd = reg.store().getTypeDataAs<FunctionTypeData>(signature);
    EXPECT(fd != nullptr && "materializeArgCasts: resolved signature must be a function type");
    const std::size_t count = call.boundArgs.has_value() ? call.boundArgs->size() : (call.m_args.has_value() ? call.m_args->size() : 0);
    std::vector<TypeId> casts;
    casts.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const TypeId param = i < fd->paramTypes.size() ? fd->paramTypes[i] : fd->variadicType;
        casts.push_back(callCastTarget(reg, param));
    }
    call.argCasts = std::move(casts);
}

void fillBinary(Binary& binary, const TypeReadModel& reg) {
    if (binary.resolvedOperatorSignature == INVALID_TYPE_ID) {
        return; // оператор не перегружен - приводить нечего
    }
    // Приведения нужны только пути `emitBinaryOpRaw → emitOperatorDispatch`. `ArrayAccess`
    // эмитится отдельно (свой эмиттер читает сигнатуру сам), а inc/dec/type-check/swap
    // обрабатываются до dispatch: их форма параметров иная (member-унарные имеют 0 параметров).
    if (binary.kind() == ParserToken::Kind::ArrayAccess) {
        return;
    }
    if (isIncDecOp(binary.m_op) || isTypeCheckOp(binary.m_op) || isSwapOp(binary.m_op)) {
        return;
    }
    const auto* fd = reg.store().getTypeDataAs<FunctionTypeData>(binary.resolvedOperatorSignature);
    EXPECT(fd != nullptr && "materializeArgCasts: operator signature must be a function type");
    std::vector<TypeId> casts;
    if (binary.resolvedOperatorIsMember) {
        EXPECT(fd->paramTypes.size() == 1 && "materializeArgCasts: member operator must have one parameter");
        casts.push_back(fd->paramTypes[0]); // правый операнд; получатель - левый
    } else {
        EXPECT(fd->paramTypes.size() == 2 && "materializeArgCasts: free operator must have two parameters");
        casts.push_back(fd->paramTypes[0]);
        casts.push_back(fd->paramTypes[1]);
    }
    binary.argCasts = std::move(casts);
}

void walk(AstNodePtr& node, const TypeReadModel& reg) {
    if (!node) {
        return;
    }
    if (node->is<CallExpr>()) {
        fillCall(*node->as<CallExpr>(), reg);
    } else if (node->is<Binary>()) {
        fillBinary(*node->as<Binary>(), reg);
    }
    std::vector<AstNodePtr*> children;
    node->collectChildren(children);
    for (AstNodePtr* child : children) {
        walk(*child, reg);
    }
}

} // namespace

void materializeArgCasts(std::vector<AstNodePtr>& roots, Context& ctx) {
    for (AstNodePtr& root : roots) {
        walk(root, ctx.types());
    }
}

} // namespace trust
