#include "semantic/pass.hpp"
#include "ast/token_type.hpp"

#include "semantic/type_inference.hpp"
#include "diag/diag.hpp"
#include "types/registry.hpp"
#include "utils/strings.hpp"

namespace trust {

AnalysisContext::AnalysisContext(Context& ctx)
: m_ctx(ctx) {
}

bool AnalysisContext::hasErrors() const {
    return m_ctx.diag().errorCount() > 0;
}

// ── Контекст области имён и текущей функции (из скоуп-стека SymbolTable) ──

std::string AnalysisContext::namespacePath() const {
    // Сегменты каждой области имён собираются по скоупам (внутри наружу), затем
    // последовательность скоупов разворачивается: путь — от внешней к внутренней.
    std::vector<std::vector<std::string>> scopeSegs;
    m_symbols.forEachScope([&](const SymbolTable::Scope& s) {
        if (!s.creator || s.creator->kind() != ParserToken::Kind::ScopeBlock) {
            return;
        }
        const auto& sb = static_cast<const ScopeBlock&>(*s.creator);
        const std::string_view text = sb.text();
        const bool isCodeBlock = text.empty() || text == "{";
        const bool isGlobalNs = text == "::";
        const bool isLabel = !isCodeBlock && !sb.is_hidden() && !isGlobalNs && text.find("::") == std::string_view::npos;
        if (isCodeBlock || isLabel || sb.is_hidden() || isGlobalNs) {
            return;
        }
        // Область имён: "ns::name::" / "::ns::name". Сегменты — непустые части по "::".
        std::vector<std::string> segs;
        std::size_t pos = 0;
        while (pos <= text.size()) {
            const std::size_t end = text.find("::", pos);
            const std::string_view seg = (end == std::string_view::npos) ? text.substr(pos) : text.substr(pos, end - pos);
            if (!seg.empty()) {
                segs.emplace_back(seg);
            }
            if (end == std::string_view::npos) {
                break;
            }
            pos = end + 2;
        }
        scopeSegs.push_back(std::move(segs));
    });

    std::string result;
    for (auto it = scopeSegs.rbegin(); it != scopeSegs.rend(); ++it) { // от внешней к внутренней
        for (const auto& seg : *it) {
            if (!result.empty()) {
                result += "::";
            }
            result += seg;
        }
    }
    return result;
}

std::string AnalysisContext::namespaceFull() const {
    return "::" + namespacePath() + "::";
}

const FuncDecl* AnalysisContext::currentFunc() const {
    const FuncDecl* result = nullptr;
    // forEachScope идёт от внутреннего скоупа к глобальному — первый FuncDecl и есть текущий.
    m_symbols.forEachScope([&](const SymbolTable::Scope& s) {
        if (!result && s.creator && s.creator->kind() == ParserToken::Kind::FuncDecl) {
            result = static_cast<const FuncDecl*>(s.creator);
        }
    });
    return result;
}

std::string AnalysisContext::funcShortName() const {
    const FuncDecl* f = currentFunc();
    if (!f) {
        return {};
    }
    // Нативный %-префикс срезается (единый источник — utils::strip_native_prefix).
    return std::string(utils::strip_native_prefix(f->text()));
}

std::string AnalysisContext::qualifiedFuncName() const {
    std::string ns = namespacePath();
    std::string name = funcShortName();
    if (name.empty()) {
        return ns;
    }
    if (ns.empty()) {
        return name;
    }
    return ns + "::" + name;
}

bool AnalysisContext::requireFunction(const AstNodeBase& node, const char* macro) const {
    if (currentFunc()) {
        return true;
    }
    m_ctx.diag().report(Severity::Error, node.range(), "macro {} can only be used inside a function", macro);
    return false;
}

// ── Резолв типов и runtime-символов ──

std::optional<TypeId> AnalysisContext::resolveType(const AstNodeBase& type_node) const {
    if (type_node.kind() != ParserToken::Kind::TypeName) {
        return std::nullopt;
    }
    const auto& it = static_cast<const IdentType&>(type_node);
    std::string_view name = it.text();
    if (!name.empty() && name[0] == ':') {
        name.remove_prefix(1);
    }
    // Параметризованный кортеж `Tuple(:Rational, :Rational)` / `Tuple(sum:Rational, ...)` —
    // аннотация структурного Tuple-типа: строим тип через getOrCreateTupleType (элементы
    // позиционные имя="" или именованные). Такой тип становится возвращаемым типом функции →
    // getOrCreateFunctionType интернирует функции по нему. Распознавание — по типу из реестра
    // (TypeId), а НЕ по строковому сравнению имени.
    if (it.params() && !it.params()->empty()) {
        auto& reg = m_ctx.types();
        if (auto tid = reg.findType(name); tid.has_value() && reg.getCanonicalTypeId(*tid) == reg.getType(type_category::Tuple)) {
            std::vector<std::pair<std::string, TypeId>> elems;
            elems.reserve(it.params()->size());
            for (const auto& p : *it.params()) {
                if (!p) {
                    continue;
                }
                TypeId et = INVALID_TYPE_ID;
                std::string ename;
                if (p->kind() == ParserToken::Kind::TypeName) {
                    et = resolveType(*p).value_or(INVALID_TYPE_ID); // позиционный тип-параметр :Rational
                } else if (p->kind() == ParserToken::Kind::ArgNode) {
                    // Именованный параметр `sum:Rational` → ArgNode(name, type).
                    const auto& pd = static_cast<const ArgNode&>(*p);
                    ename = std::string(pd.text());
                    if (pd.m_type) {
                        et = resolveType(*pd.m_type).value_or(INVALID_TYPE_ID);
                    }
                }
                elems.emplace_back(ename, clearInferred(et));
            }
            return reg.getOrCreateTupleType(std::move(elems));
        }
    }
    // Пользовательские алиасы, связанные в скоуп-стеке (с учётом shadowing).
    if (const Symbol* s = m_symbols.resolve(name)) {
        if (s->decl->kind() == ParserToken::Kind::TypeDecl && s->type != INVALID_TYPE_ID) {
            return s->type;
        }
    }
    // Builtin-типы и глобальные алиасы — в реестре типов.
    auto base = m_ctx.types().findType(name);
    if (!base.has_value()) {
        return std::nullopt;
    }
    // Тип-определение массива `:Elem[3]` / `:Elem[3,4]`: размерности из `[...]` (без финальной
    // запятой) → структурный Array<Elem, dims> (ArrayTypeData). Определения N-D (`[3,4]`) работают
    // без кодогенерации (диагностика «не реализовано» при генерации); 1D (`[3]`) — фиксированный
    // (std::array). Размерности кладутся в данные типа.
    if (it.dims() && !it.dims()->empty()) {
        std::vector<uint64_t> dims;
        for (const auto& d : *it.dims()) {
            if (!d || d->kind() != ParserToken::Kind::IntLiteral) {
                continue;
            }
            unsigned long long v = 0;
            try {
                v = std::stoull(std::string(d->text()), nullptr, 0);
            } catch (...) {
                v = 0;
            }
            dims.push_back(v);
        }
        if (!dims.empty()) {
            // Определение типа массива `:Elem[3]`: изменяемый массив (std::vector) с известной
            // размерностью (для проверки границ при доступе). N-D определения работают без
            // кодогенерации (при генерации — диагностика «не реализовано»).
            return m_ctx.types().getOrCreateArrayType(*base, std::move(dims));
        }
    }
    return base;
}

TypeId AnalysisContext::resolvedType(const AstNodeBase& node) const {
    // Составное выражение уже типизировано пост-порядково → из кеша.
    auto it = m_exprTypes.find(&node);
    if (it != m_exprTypes.end()) {
        return it->second;
    }
    // Типизируемое бинарное выражение → результат типа из поля узла.
    if (is_binary_expr_kind(node.kind())) {
        return static_cast<const Binary&>(node).resultType;
    }
    // Литерал → выведенный тип (единый предикат литералов is_literal_kind).
    if (is_literal_kind(node.kind())) {
        return literalType(static_cast<const Literal&>(node), m_ctx.types());
    }
    switch (node.kind()) {
    case ParserToken::Kind::VarDecl:
        return static_cast<const VarDecl&>(node).inferredType;
    case ParserToken::Kind::DictLiteral:
    case ParserToken::Kind::Tuple: {
        // Кортеж (`kind==Tuple`): структурный тип уже создан и закеширован в analyzeDictLiteral
        // (мутирующем); здесь — const-фолбэк при пустом кеше → плоский Tuple-тип. В C++ → auto/std::tuple.
        if (node.kind() == ParserToken::Kind::Tuple) {
            return m_ctx.types().getType(type_category::Tuple);
        }
        // Литерал словаря: тип по аннотации m_type (типизированная конструкция/каст) или Dict.
        const auto& dl = static_cast<const DictLiteralNode&>(node);
        if (dl.m_type) {
            return resolveType(*dl.m_type).value_or(m_ctx.types().getType(type::Dict));
        }
        return m_ctx.types().getType(type::Dict);
    }
    case ParserToken::Kind::ArrayInit:
        // Литерал массива: интернированный структурный Array<Elem> (analyzeArrayInit).
        return static_cast<const DictLiteralNode&>(node).arrayType;
    case ParserToken::Kind::MemberAccess:
    case ParserToken::Kind::ArrayAccess:
        // Результат доступа к элементу словаря — std::any (Any).
        return m_ctx.types().getType(type_generic::Any);
    case ParserToken::Kind::Ident: {
        // Тип переменной — живой тип символа в скоуп-стеке (во время анализа).
        const Symbol* s = m_symbols.resolve(node.text());
        return s ? s->type : INVALID_TYPE_ID;
    }
    default:
        return INVALID_TYPE_ID;
    }
}

void AnalysisContext::setExprType(const AstNodeBase* node, TypeId id) {
    if (node) {
        m_exprTypes[node] = id;
    }
}

TypeId AnalysisContext::buildFuncType(const FuncDecl& func_node) const {
    std::vector<TypeId> paramTypes;
    if (func_node.m_params) {
        for (const auto& p : *func_node.m_params) {
            if (!p || p->kind() != ParserToken::Kind::ArgNode) {
                continue;
            }
            const auto& pd = static_cast<const ArgNode&>(*p);
            TypeId pt = INVALID_TYPE_ID;
            if (pd.m_type) {
                pt = resolveType(*pd.m_type).value_or(INVALID_TYPE_ID);
            }
            paramTypes.push_back(pt);
        }
    }

    TypeId returnType = INVALID_TYPE_ID; // INVALID = Void
    if (func_node.m_type) {
        returnType = resolveType(*func_node.m_type).value_or(INVALID_TYPE_ID);
    }

    return m_ctx.types().getOrCreateFunctionType(returnType, paramTypes);
}

bool AnalysisContext::isRegisteredRuntimeSymbol(std::string_view name) const {
    if (!name.empty() && name[0] == '%') {
        name.remove_prefix(1);
    }
    const std::string target(name);
    for (const auto& rs : m_ctx.types().runtimeSymbols()) {
        if (rs.symbol == target) {
            return true;
        }
    }
    return false;
}

} // namespace trust
