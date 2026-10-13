// src/semantic/typer/type_set.cpp
// Проверка комбинаций наборов допустимых типов: см. include/semantic/typer/type_set.hpp.
#include "types/type_read.hpp"
#include "semantic/typer/type_set.hpp"

#include "ast/ast_nodes.hpp"
#include "ast/token_type.hpp"
#include "diag/diag.hpp"
#include "semantic/core/pass.hpp"
#include "types/group.hpp"
#include "types/type_system.hpp"
#include "types/typekind.hpp"

namespace trust::semantic {

bool isTypeSetSubtype(const TypeReadModel& reg, TypeId sub, TypeId root) {
    if (sub == INVALID_TYPE_ID || root == INVALID_TYPE_ID) {
        return false;
    }
    const TypeId sc = reg.store().desugaredTypeId(sub);
    const TypeId rc = reg.store().desugaredTypeId(root);
    if (sc == rc) {
        return true;
    }
    // Record-классы: root в транзитивных базовых классах sub.
    if (reg.store().isRecordType(rc) && reg.store().isRecordType(sc)) {
        std::vector<TypeId> pending = reg.store().baseClasses(sc);
        while (!pending.empty()) {
            const TypeId b = reg.store().desugaredTypeId(pending.back());
            pending.pop_back();
            if (b == rc) {
                return true;
            }
            for (const TypeId bb : reg.store().baseClasses(b)) {
                pending.push_back(bb);
            }
        }
        return false;
    }
    const TypeKind sk = getTypeKind(sc);
    const TypeKind rk = getTypeKind(rc);
    if (getGroup(sk) != getGroup(rk)) {
        return false;
    }
    // Одна арифметическая группа: порядок задаёт ШИРИНА (Data, биты) - `:Int8` уже `:Int32`.
    if (isArithmeticGroup(getGroup(sk))) {
        const uint8_t sd = getData(sk);
        const uint8_t rd = getData(rk);
        if (rd == 0) {
            return true; // абстрактный корень группы (Data=0) - широчайший
        }
        if (sd == 0) {
            return false;
        }
        return sd <= rd;
    }
    // Прочие группы: подтип только при равенстве (уже проверено выше) - иначе разные типы.
    return false;
}

std::optional<TypeSetModel> validateTypeSet(const Sequence& node, AnalysisContext& actx) {
    TypeSystem& reg = actx.ctx().types();
    DiagnosticEngine& diag = actx.ctx().diag();
    TypeSetModel model;
    bool ok = true;
    bool first = true;
    for (const auto& mem : node.m_body) {
        if (!mem || mem->kind() != ParserToken::Kind::ArgNode) {
            continue;
        }
        const auto& arg = static_cast<const ArgNode&>(*mem);
        const std::string_view sign = arg.text();
        const bool isMinus = (sign == "-");
        if (first && isMinus) {
            diag.report(Severity::Error, arg.range(), "type set cannot start with '-'");
            ok = false;
            break;
        }
        first = false;
        // Составной тип-член (шаблон/кортеж/ссылка/...) - «составное внутри составного» не поддержано.
        const AstNodeBase* tn = arg.m_type.get();
        if (tn == nullptr || tn->kind() != ParserToken::Kind::TypeName || static_cast<const IdentType*>(tn)->isTemplate()) {
            diag.report(Severity::Error, arg.range(), "composite types are not supported inside a type set");
            ok = false;
            continue;
        }
        const std::optional<TypeId> tid = actx.types().resolveTypeRef(*tn);
        if (!tid) {
            diag.report(Severity::Error, tn->range(), "type '{}' not found", requiredText(*tn));
            ok = false;
            continue;
        }
        const TypeId id = reg.read().store().desugaredTypeId(*tid);
        if (!isMinus) {
            bool memberOk = true;
            for (const auto& b : model.branches) {
                if (b.root == id) {
                    diag.report(Severity::Error, tn->range(), "duplicate type set branch start '{}'", requiredText(*tn));
                    memberOk = false;
                    break;
                }
                // Новый начальный тип ветки НЕ должен быть УЖЕ уже использованного ранее
                // (иначе он уже покрыт более широкой ветвью).
                if (isTypeSetSubtype(reg, id, b.root)) {
                    diag.report(Severity::Error, tn->range(), "type set branch start '{}' is narrower than already-used branch start", requiredText(*tn));
                    memberOk = false;
                    break;
                }
            }
            if (!memberOk) {
                ok = false;
                continue;
            }
            model.branches.push_back(TypeSetBranch{id, {}});
        } else {
            TypeSetBranch& last = model.branches.back();
            if (!isTypeSetSubtype(reg, id, last.root)) {
                diag.report(Severity::Error, tn->range(), "type '{}' is not a subtype of the type set branch start", requiredText(*tn));
                ok = false;
                continue;
            }
            last.excluded.push_back(id);
        }
    }
    if (!ok || model.branches.empty()) {
        return std::nullopt;
    }
    return model;
}

} // namespace trust::semantic
