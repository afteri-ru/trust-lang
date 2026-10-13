// src/semantic/typer/operator_resolve.cpp
// Разрешение объявленных (пользовательских) операторов (см. operator_resolve.hpp).
#include "types/type_read.hpp"
#include "semantic/typer/operator_resolve.hpp"
#include "symbols/symbol_table.hpp"
#include "diag/diag.hpp"
#include "semantic/diag.hpp"
#include "types/call.hpp"
#include "types/type_system.hpp"
#include "types/type_id.hpp"
#include "types/names.hpp"
#include "types/typekind.hpp"
#include <algorithm>
#include <string>
#include <vector>

namespace trust::semantic {

namespace {

// Тексты диагностики строятся по ОПЕРАНДАМ (без dummy-параметра postfix) и различают
// один и два операнда - объявленные семейства операторов унарные и бинарные.
void reportNoMatch(AnalysisContext& actx, TypeSystem& reg, std::string_view symbol, std::span<const TypeId> operands, MapperRange range) {
    EXPECT(operands.size() <= 2 && "resolveUserOperator: no-match diagnostic supports one or two operands");
    if (operands.size() == 1) {
        actx.ctx().diag().report(Severity::Error, range, "no matching operator '{}' for type '{}'", symbol, reg.read().printer().getTypeName(operands.front()));
        return;
    }
    actx.ctx().diag().report(Severity::Error, range, "no matching operator '{}' for types '{}' and '{}'", symbol, reg.read().printer().getTypeName(operands[0]),
                             reg.read().printer().getTypeName(operands[1]));
}

// Несравнимые лучшие кандидаты: конфликт ФОРМ (member и free одновременно) - особая
// формулировка; конфликт внутри одной формы - общая (амбигуа перегрузок объявления).
void reportAmbiguous(AnalysisContext& actx, TypeSystem& reg, std::string_view symbol, TypeId receiver, const std::vector<OverloadCandidate>& candidates,
                     const OverloadResolution& resolution, std::span<const TypeId> operands, MapperRange range) {
    const auto ambiguousSig = [&resolution](TypeId sig) {
        return std::find(resolution.ambiguous.begin(), resolution.ambiguous.end(), getUnqualifiedType(sig)) != resolution.ambiguous.end();
    };
    bool memberAmbiguous = false;
    bool freeAmbiguous = false;
    for (const OverloadCandidate& cand : candidates) {
        if (!ambiguousSig(cand.declaredSig)) {
            continue;
        }
        (cand.objectOwner != INVALID_TYPE_ID ? memberAmbiguous : freeAmbiguous) = true;
    }
    if (memberAmbiguous && freeAmbiguous) {
        actx.ctx().diag().report(Severity::Error, range, "operator '{}' is ambiguous: declared both as a member of '{}' and as a free function", symbol,
                                 reg.read().printer().getTypeName(receiver));
        return;
    }
    EXPECT(operands.size() <= 2 && "resolveUserOperator: ambiguous diagnostic supports one or two operands");
    if (operands.size() == 1) {
        actx.ctx().diag().report(Severity::Error, range, "operator '{}' is ambiguous for type '{}'", symbol,
                                 reg.read().printer().getTypeName(operands.front()));
        return;
    }
    actx.ctx().diag().report(Severity::Error, range, "operator '{}' is ambiguous for types '{}' and '{}'", symbol,
                             reg.read().printer().getTypeName(operands[0]), reg.read().printer().getTypeName(operands[1]));
}

} // namespace

bool isUserOperatorType(const TypeReadModel& reg, TypeId type) {
    if (type == INVALID_TYPE_ID) {
        return false;
    }
    const TypeId canonical = reg.store().desugaredTypeId(type);
    return canonical != INVALID_TYPE_ID &&
           (reg.store().isRecordType(canonical) || reg.store().isNativeClassType(canonical) || reg.store().isNativeTemplateType(canonical));
}

std::optional<ResolvedUserOperator> resolveUserOperator(AnalysisContext& actx, std::string_view symbol, std::span<const TypeId> operands, bool postfix,
                                                        MapperRange range) {
    EXPECT(!operands.empty() && "resolveUserOperator: at least one operand is required");
    TypeSystem& reg = actx.ctx().types();
    // Пользовательским может быть ЛЮБОЙ операнд: свободный оператор ищется по всем типам
    // аргументов (`5 == p` законен, если объявлен `__eq__(i:Int32, p:P)`).
    bool anyUserType = false;
    for (const TypeId t : operands) {
        if (isUserOperatorType(reg, t)) {
            anyUserType = true;
            break;
        }
    }
    if (!anyUserType) {
        return std::nullopt; // built-in/numeric path решает вызывающий
    }
    const std::string_view fname = op::functionNameOfSymbol(symbol);
    if (fname.empty()) {
        actx.ctx().diag().report(Severity::Error, range, "operator '{}' is not implemented", symbol);
        return std::nullopt;
    }
    const TypeId receiver = reg.read().store().desugaredTypeId(operands.front());
    std::optional<::trust::MethodRef> member;
    if (receiver != INVALID_TYPE_ID) {
        member = reg.read().methods().findMethodInfo(receiver, fname);
    }
    // Свободный оператор - из таблицы символов: member-операторы в скоуп не попадают.
    const Symbol* freeSym = actx.symbols().resolve(fname);

    // ОДИН набор кандидатов: member-форма получает неявный object-параметр (получатель),
    // free-форма - все операнды. Аргументы в позициях объявления: операнды (получатель первый)
    // + dummy-параметр postfix `Int32`. Амбигуа и отсутствие кандидатов - результат ранжирования.
    std::vector<TypeId> args(operands.begin(), operands.end());
    if (postfix) {
        args.push_back(reg.read().getType(type::Int32));
    }
    std::vector<OverloadCandidate> candidates;
    std::size_t memberCount = 0;
    std::size_t freeCount = 0;
    if (member.has_value()) {
        // Fallback-объявления (Range/Array/Tensor/native-template) не имеют собственного
        // владельца: `this` - сам тип получателя (exact).
        const TypeId owner = (member->owner != INVALID_TYPE_ID) ? member->owner : receiver;
        const std::vector<TypeId> memberSigs = effectiveMethodSignatures(reg, receiver, *member);
        if (std::find(memberSigs.begin(), memberSigs.end(), INVALID_TYPE_ID) != memberSigs.end()) {
            actx.ctx().diag().report(Severity::Error, range,
                                     "operator '{}' of native template '{}' cannot be instantiated: multiple type parameters are not supported", symbol,
                                     reg.read().printer().getTypeName(receiver));
            return std::nullopt;
        }
        for (const TypeId sig : memberSigs) {
            candidates.push_back(OverloadCandidate{sig, args, 0, false, false, owner});
            ++memberCount;
        }
    }
    if (freeSym != nullptr) {
        const std::vector<TypeId> sigs = overloadSignatures(*freeSym);
        for (const TypeId sig : sigs) {
            candidates.push_back(OverloadCandidate{sig, args, 0, false, false});
            ++freeCount;
        }
    }

    const OverloadResolution resolution = resolveCall({reg.read(), reg.interner()}, candidates);
    if (resolution.chosen == INVALID_TYPE_ID) {
        if (!resolution.ambiguous.empty()) {
            reportAmbiguous(actx, reg, symbol, receiver, candidates, resolution, operands, range);
        } else {
            reportNoMatch(actx, reg, symbol, operands, range);
        }
        return std::nullopt;
    }
    const bool isMember = (candidates[resolution.chosenIndex].objectOwner != INVALID_TYPE_ID);
    const auto* fd = reg.read().store().getTypeDataAs<FunctionTypeData>(resolution.chosen);
    EXPECT(fd != nullptr && "resolveUserOperator: declared operator signature must be a function type");
    ResolvedUserOperator out;
    out.returnType = fd->returnType;
    out.signature = getUnqualifiedType(resolution.chosen);
    out.member = isMember;
    // Кодогену нужен explicit-dispatch, только если набор ВЫБРАННОЙ формы перегружен.
    out.overloaded = (isMember ? memberCount : freeCount) > 1;
    if (!isMember && freeSym != nullptr) {
        // Свободный оператор изолируется namespace'ом модуля объявления: у operator-функции
        // нет идентификатора для префикса. Модуль — из узла объявления (единственный факт).
        out.namespacePath = stableSymbolName(utils::namespacePathOf(freeSym->name));
        out.module = freeSym->decl != nullptr ? freeSym->decl->module : ModuleIndex::invalid();
    }
    return out;
}

} // namespace trust::semantic
