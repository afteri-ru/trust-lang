// src/semantic/nativeref.cpp
// Реализация NativeRefHook - отслеживание инвалидации ссылок (условный атрибут @[reftrace@],
// см. include/semantic/nativeref.hpp) + диагностики нативных (сырых) C++-ссылок.
// Структурная детекция по инициализатору/типу.

#include "semantic/nativeref.hpp"

#include "ast/attr_pool.hpp"
#include "ast/ident_name.hpp"
#include "diag/options.hpp"
#include "utils/operators.hpp"
#include "types/registry.hpp"
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

bool NativeRefHook::reftraceEnabled() const {
    return m_actx.ctx().opts().get(semantic::DiagId::RefTrace) != Severity::Ignore;
}

std::string NativeRefHook::normalizeMethodName(std::string_view m) {
    std::string r(m);
    // Инстанс-член класса регистрируется с ведущей '.' (см. semantic/MEMORY.md);
    // константный вызов несёт хвостовой '^'.
    if (!r.empty() && r[0] == '.') {
        r.erase(0, 1);
    }
    if (!r.empty() && r.back() == '^') {
        r.pop_back();
    }
    return r;
}

/// Сводит имя переменной к «bare» (без сигила локальности '$'): локальные переменные
/// нормализуются в SymbolTable как `$x`, а в AST-узлах (VarDecl/инициализаторы) текст
/// на момент onNode ещё без '$'. Единая форма ключей зависимых/источников - bare.
static std::string bareName(std::string_view name) {
    if (!name.empty() && name.front() == '$') {
        return std::string(name.substr(1));
    }
    return std::string(name);
}

std::string NativeRefHook::sourceOf(const AstNodeBase* node) const {
    if (!node) {
        return {};
    }
    switch (node->kind()) {
    case ParserToken::Kind::Ident: {
        const auto& id = static_cast<const IdentName&>(*node);
        const std::string t(id.text());
        return (t.empty() || t == "_") ? std::string{} : t;
    }
    case ParserToken::Kind::ArrayAccess:
    case ParserToken::Kind::MemberAccess: {
        const auto& b = static_cast<const Binary&>(*node);
        return sourceOf(b.m_left.get());
    }
    case ParserToken::Kind::RefMakeExpr:
    case ParserToken::Kind::RefTakeExpr:
    case ParserToken::Kind::NativeRefMakeExpr:
    case ParserToken::Kind::NativeRefTakeExpr: {
        const auto& s = static_cast<const Sequence&>(*node);
        if (!s.m_body.empty()) {
            return sourceOf(s.m_body[0].get());
        }
        return {};
    }
    case ParserToken::Kind::CallExpr: {
        // Конструктор reftrace-класса Span(container) -> источник - первый аргумент.
        const auto& call = static_cast<const CallExpr&>(*node);
        if (call.m_callee && call.m_callee->kind() == ParserToken::Kind::Ident && call.m_args && !call.m_args->empty()) {
            const std::string cname(call.m_callee->text());
            if (m_markedClasses.count(cname) != 0) {
                return sourceOf((*call.m_args)[0].get());
            }
        }
        return {};
    }
    default:
        return {};
    }
}

bool NativeRefHook::varIsMarkedClassType(const VarDecl& var) const {
    if (!var.m_type) {
        return false;
    }
    const std::string t(var.m_type->text());
    // Тип может быть голым ("Span") или шаблонным ("Span<Int32>").
    for (const auto& c : m_markedClasses) {
        if (t == c) {
            return true;
        }
        if (t.size() > c.size() && t.compare(0, c.size(), c) == 0 && t[c.size()] == '<') {
            return true;
        }
    }
    return false;
}

bool NativeRefHook::varIsTracked(const VarDecl& var, const std::string& source) const {
    if (source.empty()) {
        return false;
    }
    // Транзитивность: если источник сам зависимая - новая переменная тоже отслеживается.
    if (findFrameWithDependent(source) != nullptr) {
        return true;
    }
    const AstNodeBase* init = var.m_initializer.get();
    if (!init) {
        return varIsMarkedClassType(var);
    }
    const AttrPool& attrs = m_actx.ctx().attrs();
    // 1. Индексный доступ obj[i] -> ссылка на данные -> всегда зависимая.
    if (init->kind() == ParserToken::Kind::ArrayAccess) {
        return true;
    }
    // 2. Маркер ссылки на переменной (& x / * x) - чистая C++-ссылка.
    if (var.has_attr(attrs, attr::Reftype)) {
        return true;
    }
    // 3. & obj / * obj (RefMake/RefTake) в выражении.
    if (init->kind() == ParserToken::Kind::RefMakeExpr || init->kind() == ParserToken::Kind::RefTakeExpr ||
        init->kind() == ParserToken::Kind::NativeRefMakeExpr || init->kind() == ParserToken::Kind::NativeRefTakeExpr) {
        return true;
    }
    // 4. Вызов метода obj.m(): отслеживается, если метод помечен @[reftrace@].
    if (init->kind() == ParserToken::Kind::MemberAccess) {
        const auto& b = static_cast<const Binary&>(*init);
        if (b.m_right && b.m_right->kind() == ParserToken::Kind::CallExpr) {
            const auto& call = static_cast<const CallExpr&>(*b.m_right);
            if (call.m_callee && call.m_callee->kind() == ParserToken::Kind::Ident) {
                const std::string mname = normalizeMethodName(call.m_callee->text());
                if (m_markedMethods.count(mname) != 0) {
                    return true;
                }
            }
        }
        return false;
    }
    // 5. Конструктор reftrace-класса Span(container).
    if (init->kind() == ParserToken::Kind::CallExpr) {
        const auto& call = static_cast<const CallExpr&>(*init);
        if (call.m_callee && call.m_callee->kind() == ParserToken::Kind::Ident) {
            if (m_markedClasses.count(std::string(call.m_callee->text())) != 0) {
                return true;
            }
        }
        return false;
    }
    // 6. Тип переменной - reftrace-класс (s := vect, где s : Span).
    return varIsMarkedClassType(var);
}

std::string NativeRefHook::resolveRootSource(const std::string& name) const {
    std::set<std::string> seen;
    std::string cur = name;
    while (true) {
        std::pair<std::string, int64_t> entry;
        if (findFrameWithDependent(cur, &entry) == nullptr) {
            return cur; // не зависимая -> корневой источник
        }
        const std::string& src = entry.first;
        if (src.empty() || src == cur || seen.count(src) != 0) {
            return cur; // защита от циклов
        }
        seen.insert(src);
        cur = src;
    }
}

void NativeRefHook::recordBirth(const VarDecl& var) {
    const std::string dep = bareName(var.text());
    if (dep.empty() || dep == "_") {
        return;
    }
    const AstNodeBase* init = var.m_initializer.get();
    std::string source = bareName(sourceOf(init));
    if (source.empty()) {
        return;
    }
    if (!varIsTracked(var, source)) {
        return;
    }
    // Сводим к корневому источнику (транзитивность) и фиксируем эпоху на момент рождения.
    source = resolveRootSource(source);
    if (source.empty() || source == dep) {
        return;
    }
    const int64_t born = m_epoch[source];
    m_frames.back().dependent[dep] = {source, born};
}

void NativeRefHook::recordMutation(const std::string& name, const MapperRange& range) {
    if (name.empty()) {
        return;
    }
    m_epoch[name]++;
    m_changeSite[name] = range;
}

bool NativeRefHook::hasDependents(const std::string& name) const {
    for (auto it = m_frames.rbegin(); it != m_frames.rend(); ++it) {
        for (const auto& kv : it->dependent) {
            if (kv.second.first == name) {
                return true;
            }
        }
    }
    return false;
}

const NativeRefHook::Frame* NativeRefHook::findFrameWithDependent(const std::string& name, std::pair<std::string, int64_t>* out) const {
    for (auto it = m_frames.rbegin(); it != m_frames.rend(); ++it) {
        auto found = it->dependent.find(name);
        if (found != it->dependent.end()) {
            if (out) {
                *out = found->second;
            }
            return &*it;
        }
    }
    return nullptr;
}

// -- Диагностики нативных (сырых) C++-ссылок (D3..D7) --------------------------

RefType NativeRefHook::nativeKindOfTypeNode(const AstNodeBase* typeNode) {
    if (!typeNode) {
        return RefType::kValue;
    }
    const auto k = refTypeFromTypeSigil(typeNode->text());
    if (k.has_value() && (*k == RefType::kRef || *k == RefType::kPtr || *k == RefType::kRref || *k == RefType::kPtrPtr)) {
        return *k;
    }
    return RefType::kValue;
}

static RefType nativeKindOfTypeId(TypeId tid) {
    if (tid == INVALID_TYPE_ID) {
        return RefType::kValue;
    }
    const RefType rt = getRefType(getKindFromId(tid));
    return (rt == RefType::kRef || rt == RefType::kPtr || rt == RefType::kRref || rt == RefType::kPtrPtr) ? rt : RefType::kValue;
}

void NativeRefHook::registerNativeVar(const Symbol& sym, RefType kind) {
    if (!sym.decl || sym.decl->kind() != ParserToken::Kind::VarDecl) {
        return;
    }
    const std::string bare = bareName(sym.decl->text());
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
    RefType nativeKind = nativeKindOfTypeId(sym.type);
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
    if (e->kind() == ParserToken::Kind::NativeRefMakeExpr) {
        return true;
    }
    if (e->kind() == ParserToken::Kind::Ident) {
        const std::string n = bareName(e->text());
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
        const Symbol* sym = m_actx.symbols().resolve(call.m_callee->text());
        if (sym && sym->type != INVALID_TYPE_ID) {
            const auto* fd = m_actx.ctx().types().getTypeDataAs<FunctionTypeData>(sym->type);
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
    const std::string lhs = bareName(b.m_left->text());
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
    const std::string l = bareName(b.m_left->text());
    const std::string r = bareName(b.m_right->text());
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
    const AttrPool& attrs = m_actx.ctx().attrs();

    // Помеченные @[reftrace@] классы и методы.
    if (kind == ParserToken::Kind::ClassDecl) {
        const auto& cls = static_cast<const ClassDecl&>(*node);
        if (cls.has_attr(attrs, attr::RefTrace)) {
            m_markedClasses.insert(std::string(cls.text()));
        }
        return false;
    }
    if (kind == ParserToken::Kind::FuncDecl) {
        const auto& fn = static_cast<const FuncDecl&>(*node);
        if (fn.has_attr(attrs, attr::RefTrace)) {
            m_markedMethods.insert(normalizeMethodName(fn.text()));
        }
        // D3+D7: нативные маркеры в сигнатуре функции - ошибка.
        checkFuncSignature(fn);
        return false;
    }

    // Порождение зависимой переменной.
    if (kind == ParserToken::Kind::VarDecl) {
        recordBirth(static_cast<const VarDecl&>(*node));
        return false;
    }

    // Мутация источника присваиванием нового значения.
    if (kind == ParserToken::Kind::AssignOp) {
        const auto& b = static_cast<const Binary&>(*node);
        if (b.m_left && b.m_left->kind() == ParserToken::Kind::Ident) {
            const std::string lhs = bareName(b.m_left->text());
            if (hasDependents(lhs)) {
                recordMutation(lhs, b.range());
            }
        }
        // Диагностики нативных ссылок на присваивании/swap.
        if (utils::isSwapOp(b.text())) {
            checkSwapNativeAcrossScopes(b);
        } else if (b.text() == "=") {
            checkAssignIntoOuterNative(b);
        }
        return false;
    }

    // Мутация источника вызовом не-const метода.
    if (kind == ParserToken::Kind::MemberAccess) {
        const auto& b = static_cast<const Binary&>(*node);
        if (b.m_right && b.m_right->kind() == ParserToken::Kind::CallExpr) {
            const std::string obj = sourceOf(b.m_left.get());
            if (hasDependents(obj)) {
                const auto& call = static_cast<const CallExpr&>(*b.m_right);
                // const-вызов (obj.method^() или @[readonly@]) данные не меняет.
                const bool isConst = call.has_attr(attrs, attr::ReadOnly) || (call.m_callee && call.m_callee->text().find('^') != std::string_view::npos);
                if (!isConst) {
                    recordMutation(obj, b.range());
                }
            }
        }
        return false;
    }

    return false;
}

void NativeRefHook::onResolve(const AstNodeBase& node, const Symbol* sym) {
    if (!sym || !sym->decl || sym->decl->kind() != ParserToken::Kind::VarDecl) {
        return;
    }
    const std::string dep = bareName(sym->name);
    std::pair<std::string, int64_t> entry;
    if (findFrameWithDependent(dep, &entry) == nullptr) {
        return;
    }
    const std::string& source = entry.first;
    const int64_t born = entry.second;
    auto epochIt = m_epoch.find(source);
    const int64_t cur = (epochIt == m_epoch.end()) ? 0 : epochIt->second;
    if (cur <= born) {
        return; // источник не мутировал после рождения зависимой
    }
    if (!reftraceEnabled()) {
        return;
    }
    m_actx.ctx().report(node.range(), semantic::DiagId::RefTrace, "using the dependent variable '{}' after changing the main variable '{}'!", dep, source);
    auto cs = m_changeSite.find(source);
    if (cs != m_changeSite.end()) {
        m_actx.ctx().diag().report(Severity::Note, cs->second, "using main variable '{}'", source);
    }
}

} // namespace trust
