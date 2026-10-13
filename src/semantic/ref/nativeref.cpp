// src/semantic/ref/nativeref.cpp
// Реализация NativeRefHook — диагностики нативных (сырых) C++-ссылок D3–D7
// (см. include/semantic/nativeref.hpp).

#include "semantic/ref/nativeref.hpp"
#include "semantic/core/local_name.hpp"

#include "ast/binary_op.hpp"
#include "ast/ref_syntax.hpp"
#include "types/type_system.hpp"
#include "types/type_id.hpp"

#include <utility>

namespace trust {

NativeRefHook::NativeRefHook(AnalysisContext& actx)
: m_actx(actx) {
    m_frames.emplace_back(); // глобальный скоуп (depth >= 1)
}

void NativeRefHook::enterScope() {
    m_frames.emplace_back();
}

void NativeRefHook::exitScope() {
    if (m_frames.size() > 1) {
        m_frames.pop_back();
    }
}

RefType NativeRefHook::nativeKindOfTypeNode(const AstNodeBase* typeNode) {
    if (!typeNode) {
        return RefType::kValue;
    }
    const auto k = refKindOfTypeNode(typeNode);
    if (k.has_value() && isNativeRefKind(*k)) {
        return *k;
    }
    return RefType::kValue;
}

static RefType nativeKindOfTypeId(TypeId tid) {
    if (tid == INVALID_TYPE_ID) {
        return RefType::kValue;
    }
    const RefType rt = getRefType(getTypeKind(tid));
    return isNativeRefKind(rt) ? rt : RefType::kValue;
}

void NativeRefHook::registerNativeVar(const Symbol& sym, RefType kind) {
    if (!sym.decl || sym.decl->kind() != ParserToken::Kind::VarDecl) {
        return;
    }
    const std::string bare = bareLocalName(requiredText(*sym.decl));
    if (bare.empty() || bare == "_") {
        return;
    }
    // D4: нативная ссылка в статической/глобальной/TLS-переменной - утечка времени жизни.
    if (sym.storage == Storage::Static || sym.storage == Storage::Global || sym.storage == Storage::ThreadLocal) {
        m_actx.ctx().diag().report(Severity::Error, sym.decl->range(), "native reference cannot be stored in a static/global variable '{}' (lifetime escape)",
                                   bare);
        return;
    }
    const size_t depth = m_frames.size();
    m_frames.back().nativeVars[bare] = {depth, kind};
}

void NativeRefHook::onDeclare(const Symbol& sym) {
    if (!sym.decl || sym.decl->kind() != ParserToken::Kind::VarDecl) {
        return;
    }
    RefType nativeKind = nativeKindOfTypeId(sym.type.getTypeId());
    if (nativeKind == RefType::kValue) {
        const auto& vd = static_cast<const VarDecl&>(*sym.decl);
        nativeKind = nativeKindOfTypeNode(vd.m_type.get());
    }
    if (nativeKind != RefType::kValue) {
        registerNativeVar(sym, nativeKind);
    }
}

void NativeRefHook::checkFuncSignature(const FuncDecl& fn) {
    // D3+D7 применяются ТОЛЬКО к ОПРЕДЕЛЕНИЯМ функций (с телом). Forward-объявления ВНЕШНИХ
    // нативных C++-функций (`%foo() : %& Int32;`) могут иметь нативные возвраты/параметры - их
    // анализируемый код вправе вызывать (D5 контролирует сохранение результата).
    if (!fn.m_body.has_value()) {
        return;
    }
    // D7a: нативные маркеры в параметрах запрещены (разрешены только атрибут-указатели).
    if (fn.m_params) {
        for (const auto& p : *fn.m_params) {
            if (p && p->kind() == ParserToken::Kind::ArgNode) {
                const auto& pd = static_cast<const ArgNode&>(*p);
                if (pd.m_type && nativeKindOfTypeNode(pd.m_type.get()) != RefType::kValue) {
                    m_actx.ctx().diag().report(
                        Severity::Error, pd.m_type->range(),
                        "native reference is not allowed in a function parameter; use a reference type attribute (@[reftype(...)]) instead");
                }
            }
        }
    }
    // D3 (+D7b): возврат нативной ссылки из функции - запрещено.
    if (fn.m_type && nativeKindOfTypeNode(fn.m_type.get()) != RefType::kValue) {
        m_actx.ctx().diag().report(Severity::Error, fn.m_type->range(),
                                   "native reference cannot be returned from a function; use a value/smart type or a reference type attribute");
    }
}

const std::pair<size_t, RefType>* NativeRefHook::findNativeVar(const std::string& bare) const {
    for (auto it = m_frames.rbegin(); it != m_frames.rend(); ++it) {
        auto found = it->nativeVars.find(bare);
        if (found != it->nativeVars.end()) {
            return &found->second;
        }
    }
    return nullptr;
}

bool NativeRefHook::exprProducesNativeRef(const AstNodeBase* e) const {
    if (!e) {
        return false;
    }
    if (e->kind() == ParserToken::Kind::Ident) {
        const std::string n = bareLocalName(requiredText(*e));
        return !n.empty() && n != "_" && findNativeVar(n) != nullptr;
    }
    if (e->kind() == ParserToken::Kind::CallExpr) {
        // Вызов функции, возвращающей нативную ссылку (нативная C++-функция или forward-объявление
        // `%foo() : %& Type;`): результат - нативная ссылка. Возврат нативных из ОПРЕДЕЛЕНИЙ
        // запрещён (D3), но вызов уже существующих нативных функций с нативным возвратом допустим.
        const auto& call = static_cast<const CallExpr&>(*e);
        if (!call.m_callee) {
            return false;
        }
        const Symbol* sym = m_actx.symbols().resolve(requiredText(*call.m_callee));
        if (sym && sym->type.getTypeId() != INVALID_TYPE_ID) {
            const auto* fd = m_actx.ctx().readTypes().store().getTypeDataAs<FunctionTypeData>(sym->type.getTypeId());
            if (fd && nativeKindOfTypeId(fd->returnType) != RefType::kValue) {
                return true;
            }
        }
        return false;
    }
    return false;
}

void NativeRefHook::checkAssignIntoOuterNative(const Binary& b) {
    // D5: сохранение нативной ссылки в переменную, объявленную во ВНЕШНЕМ (более высоком) скоупе.
    if (!b.m_left || b.m_left->kind() != ParserToken::Kind::Ident) {
        return;
    }
    const std::string lhs = bareLocalName(requiredText(*b.m_left));
    if (lhs.empty() || lhs == "_") {
        return;
    }
    const auto* info = findNativeVar(lhs);
    if (!info) {
        return; // LHS - не нативная переменная
    }
    if (!exprProducesNativeRef(b.m_right.get())) {
        return; // присваивается не нативная ссылка
    }
    if (info->first < m_frames.size()) {
        m_actx.ctx().diag().report(Severity::Error, b.range(),
                                   "cannot store a native reference into variable '{}' defined in an outer scope; native references may only be stored into a "
                                   "new local variable of the current scope",
                                   lhs);
    }
}

void NativeRefHook::checkSwapNativeAcrossScopes(const Binary& b) {
    // D6: swap нативных ссылок, определённых на разных уровнях скоупов - запрещено.
    if (!b.m_left || b.m_left->kind() != ParserToken::Kind::Ident || !b.m_right || b.m_right->kind() != ParserToken::Kind::Ident) {
        return;
    }
    const std::string l = bareLocalName(requiredText(*b.m_left));
    const std::string r = bareLocalName(requiredText(*b.m_right));
    if (l.empty() || r.empty() || l == "_" || r == "_") {
        return;
    }
    const auto* li = findNativeVar(l);
    const auto* ri = findNativeVar(r);
    if (!li || !ri) {
        return; // оба операнда должны быть нативными
    }
    if (li->first != ri->first) {
        m_actx.ctx().diag().report(Severity::Error, b.range(), "cannot swap native references defined at different scope levels ('{}' and '{}')", l, r);
    }
}

bool NativeRefHook::onNode(AstNodePtr& node) {
    if (!node) {
        return false;
    }
    const auto kind = node->kind();

    // D3+D7: нативные маркеры в сигнатуре функции - ошибка.
    if (kind == ParserToken::Kind::FuncDecl) {
        checkFuncSignature(static_cast<const FuncDecl&>(*node));
        return false;
    }

    // D5/D6: сохранение нативной ссылки во внешний скоуп и swap на разных уровнях.
    if (kind == ParserToken::Kind::AssignOp) {
        const auto& b = static_cast<const Binary&>(*node);
        if (isSwapOp(b.m_op)) {
            checkSwapNativeAcrossScopes(b);
        } else if (isPlainAssignOp(b.m_op)) {
            checkAssignIntoOuterNative(b);
        }
        return false;
    }

    return false;
}

} // namespace trust
