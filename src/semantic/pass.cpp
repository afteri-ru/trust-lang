#include "semantic/pass.hpp"
#include "ast/token_type.hpp"

#include "semantic/analysis_common.hpp"
#include "semantic/type_inference.hpp"
#include "diag/diag.hpp"
#include "types/intrinsics.hpp"
#include "types/registry.hpp"
#include "utils/strings.hpp"

namespace trust {

AnalysisContext::AnalysisContext(Context& ctx)
: m_ctx(ctx) {
}

bool AnalysisContext::hasErrors() const {
    return m_ctx.diag().errorCount() > 0;
}

// -- Контекст области имён и текущей функции (из скоуп-стека SymbolTable) --

std::string AnalysisContext::namespacePath() const {
    // Сегменты каждой области имён собираются по скоупам (внутри наружу), затем
    // последовательность скоупов разворачивается: путь - от внешней к внутренней.
    std::vector<std::vector<std::string>> scopeSegs;
    m_symbols.forEachScope([&](const SymbolTable::Scope& s) {
        // Класс (скоуп с creator=ClassDecl, см. analyzeClassDecl): имя класса - сегмент области
        // имён, чтобы `@::`/`@__NAMESPACE__` внутри класса раскрывались в `ns::Class`, а статический
        // член `@::field` → `ns::Class::field` (затем регистрируется как статическая переменная).
        if (s.creator && s.creator->kind() == ParserToken::Kind::ClassDecl) {
            scopeSegs.push_back({std::string(static_cast<const ClassDecl*>(s.creator)->text())});
            return;
        }
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
        // Область имён: "ns::name::" / "::ns::name". Сегменты - непустые части по "::".
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
    // forEachScope идёт от внутреннего скоупа к глобальному - первый FuncDecl и есть текущий.
    m_symbols.forEachScope([&](const SymbolTable::Scope& s) {
        if (!result && s.creator && s.creator->kind() == ParserToken::Kind::FuncDecl) {
            result = static_cast<const FuncDecl*>(s.creator);
        }
    });
    return result;
}

const ClassDecl* AnalysisContext::currentClass() const {
    const ClassDecl* result = nullptr;
    // forEachScope идёт от внутреннего скоупа к глобальному - первый ClassDecl и есть текущий.
    m_symbols.forEachScope([&](const SymbolTable::Scope& s) {
        if (!result && s.creator && s.creator->kind() == ParserToken::Kind::ClassDecl) {
            result = static_cast<const ClassDecl*>(s.creator);
        }
    });
    return result;
}

std::string AnalysisContext::funcShortName() const {
    const FuncDecl* f = currentFunc();
    if (!f) {
        return {};
    }
    // Нативный %-префикс срезается (единый источник - utils::strip_native_prefix).
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

// -- Резолв типов и runtime-символов --

std::optional<TypeId> AnalysisContext::resolveType(const AstNodeBase& type_node) const {
    // Символический сигл ссылочного типа в аннотации (`x : &Int32` / `*Int32` / `&?Int32`):
    // `&`/`&?` → RefMakeExpr, `*` → RefTakeExpr (грамматика `COLON STAR NAME` → TAKE). Единственный
    // ребёнок - pointee-тип; сигл (text()) отображается на вид ссылки и применяется к pointee
    // (первая ссылка - fast-path бит, вложенность - составной узел, см. TypeRegistry::applyRefType).
    if (type_node.kind() == ParserToken::Kind::RefMakeExpr || type_node.kind() == ParserToken::Kind::RefTakeExpr) {
        const auto& seq = static_cast<const Sequence&>(type_node);
        if (seq.m_body.empty()) {
            return INVALID_TYPE_ID;
        }
        const auto kind = refTypeFromTypeSigil(type_node.text());
        if (!kind) {
            m_ctx.diag().report(Severity::Error, type_node.range(), "unsupported reference sigil '{}' in type annotation", type_node.text());
            return INVALID_TYPE_ID;
        }
        const auto pointee = resolveType(*seq.m_body[0]);
        if (!pointee.has_value() || *pointee == INVALID_TYPE_ID) {
            return INVALID_TYPE_ID;
        }
        return m_ctx.types().applyRefType(*pointee, *kind);
    }
    if (type_node.kind() != ParserToken::Kind::TypeName) {
        return std::nullopt;
    }
    const auto& it = static_cast<const IdentType&>(type_node);
    std::string_view name = it.text();
    if (!name.empty() && name[0] == ':') {
        name.remove_prefix(1);
    }
    // Нативный шаблон-тип `vector<Int32>` (IdentType::isTemplate): резолв абстрактного шаблона по
    // имени + интернирование конкретной инстанциации. Встроенные контейнеры (`std::vector` →
    // `:Array`) инстанцируются через Array-структуру (объединение с `:Array`); прочие - через
    // NativeTemplateTypeData. Если базы-шаблона нет - это ошибка (в т.ч. shift→template
    // переработал сравнение `:Int8 < 5` в `:Int8<5>`, где Int8 не шаблон).
    if (it.isTemplate()) {
        auto& reg = m_ctx.types();
        auto base = reg.findType(name);
        if (!base.has_value() || !reg.isNativeTemplateType(*base)) {
            m_ctx.diag().report(Severity::Error, it.range(), "type '{}' is not a native template", name);
            return INVALID_TYPE_ID;
        }
        const std::string cppTpl = std::string(reg.nativeTemplateCppName(*base));
        std::vector<TypeId> args;
        if (it.templateArgs()) {
            for (const auto& a : *it.templateArgs()) {
                if (!a) {
                    continue;
                }
                TypeId at = INVALID_TYPE_ID;
                if (a->kind() == ParserToken::Kind::TypeName) {
                    at = resolveType(*a).value_or(INVALID_TYPE_ID); // `:Int32`, `:MyClass<:Int8>`
                } else if (a->kind() == ParserToken::Kind::Ident) {
                    at = reg.findType(a->text()).value_or(INVALID_TYPE_ID); // голое `Int32`
                }
                if (at != INVALID_TYPE_ID) {
                    args.push_back(clearFlag(at, SymbolFlag::Inferred));
                }
            }
        }
        // Встроенные контейнеры: `std::vector` → `:Array` (mutable, std::vector<Elem>); `std::array`
        // → константный `:Array^` (B2, std::array<Elem,N>). Инстанциация объединяется с `:Array`.
        if (cppTpl == "std::vector" && !args.empty()) {
            return reg.getOrCreateArrayType(args[0]);
        }
        // Общий нативный шаблон (`std::pair` и т.п.): инклуд из абстрактного шаблона (on-use).
        std::string_view inc = reg.getPreprocInclude(*base);
        return reg.getOrCreateNativeTemplateType(cppTpl, std::move(args), inc);
    }
    // Параметризованный кортеж `Tuple(:Rational, :Rational)` / `Tuple(sum:Rational, ...)` -
    // аннотация структурного Tuple-типа: строим тип через getOrCreateTupleType (элементы
    // позиционные имя="" или именованные). Такой тип становится возвращаемым типом функции →
    // getOrCreateFunctionType интернирует функции по нему. Распознавание - по типу из реестра
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
                elems.emplace_back(ename, clearFlag(et, SymbolFlag::Inferred));
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
    // Builtin-типы и глобальные алиасы - в реестре типов.
    auto base = m_ctx.types().findType(name);
    if (!base.has_value()) {
        return std::nullopt;
    }
    // Тип-определение массива `:Elem[3]` / `:Elem[3,4]`: размерности из `[...]` (без финальной
    // запятой) → структурный Array<Elem, dims> (ArrayTypeData). Определения N-D (`[3,4]`) работают
    // без кодогенерации (диагностика «не реализовано» при генерации); 1D (`[3]`) - фиксированный
    // (std::array). Размерности кладутся в данные типа.
    if (it.dims() && !it.dims()->empty()) {
        std::vector<uint64_t> dims;
        for (const auto& d : *it.dims()) {
            if (!d || d->kind() != ParserToken::Kind::IntLiteral) {
                continue;
            }
            unsigned long long v = 0;
            try {
                v = std::stoull(stripDigitSeparators(d->text()), nullptr, 0);
            } catch (...) {
                v = 0;
            }
            dims.push_back(v);
        }
        if (!dims.empty()) {
            // Определение типа массива `:Elem[3]`: изменяемый массив (std::vector) с известной
            // размерностью (для проверки границ при доступе). N-D определения работают без
            // кодогенерации (при генерации - диагностика «не реализовано»).
            return m_ctx.types().getOrCreateArrayType(*base, std::move(dims));
        }
    }
    return base;
}

TypeId AnalysisContext::resolvedType(const AstNodeBase& node) const {
    // Ident — ЖИВОЙ тип символа в скоуп-стеке, мутирующий по мере анализа (записи снимают/взводят
    // Uninit, widening меняет тип). Поэтому обрабатываем его ДО общего кеша m_exprTypes: кеш вернул
    // бы зафиксированный ранее тип и «спрятал» бы текущее чтение (ложное отсутствие диагностики
    // «чтение до инициализации»). Чистая запись `x = <expr>` LHS здесь не приходит (typeBinaryResult
    // берёт тип цели из символа напрямую), поэтому данная ветка = реальное чтение значения.
    if (node.kind() == ParserToken::Kind::Ident) {
        const Symbol* s = m_symbols.resolve(node.text());
        if (s && testFlag(s->type, SymbolFlag::Uninit) && m_uninitReadReported.insert(&node).second) {
            // Чтение значения переменной, объявленной без инициализации (`x := _` / сброс `x = _`)
            // до гарантированной записи → Error (никаких silent-fallback/UB). Срабатывает только
            // при реально взведённом Uninit, поэтому существующие тесты (без `_`) не затрагиваются.
            std::string_view disp = node.text();
            if (!disp.empty() && disp.front() == '$') { // DSL-сигил: показываем имя без служебного '$'
                disp.remove_prefix(1);
            }
            m_ctx.diag().report(Severity::Error, node.range(),
                                "variable '{}' is read before it is initialized (declared/reset without a value); "
                                "assign a value before reading it",
                                disp);
            if (s->decl) {
                m_uninitVarReportedDecls.insert(s->decl);
            }
        }
        return s ? clearFlag(s->type, SymbolFlag::Uninit) : INVALID_TYPE_ID;
    }
    // Составное выражение уже типизировано пост-порядково → из кеша.
    auto it = m_exprTypes.find(&node);
    if (it != m_exprTypes.end()) {
        return it->second;
    }
    // Типизируемое бинарное выражение → результат типа из поля узла.
    if (is_binary_expr_kind(node.kind())) {
        return static_cast<const Binary&>(node).resultType;
    }
    // Литерал → выведенный тип (единый предикат литералов is_literal_kind). Для литерала с
    // постфиксной аннотацией `literal :Type` тип выбирает ЕДИНЫЙ решатель annotatedLiteralType
    // (annotation-aware, БЕЗ fallback на текст-тип): невалидная аннотация → INVALID (диагностику
    // даёт авторитетный typeExpr/reportLiteralAnnotProblem). Обычно литерал уже в кеше
    // (типизирован typeExpr) — эта ветка лишь запасной путь для ещё не типизированных узлов.
    if (is_literal_kind(node.kind())) {
        const auto& lit = static_cast<const Literal&>(node);
        const TypeRegistry& reg = m_ctx.types();
        if (lit.typeAnnotation) {
            return annotatedLiteralType(lit, resolveType(*lit.typeAnnotation), reg).type;
        }
        return literalType(lit, reg);
    }
    switch (node.kind()) {
    case ParserToken::Kind::VarDecl:
        return static_cast<const VarDecl&>(node).inferredType;
    case ParserToken::Kind::DictLiteral:
    case ParserToken::Kind::Tuple: {
        // Кортеж (`kind==Tuple`): структурный тип уже создан и закеширован в analyzeDictLiteral
        // (мутирующем); здесь - const-фолбэк при пустом кеше → плоский Tuple-тип. В C++ → auto/std::tuple.
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
        // Результат доступа к элементу словаря - std::any (Any).
        return m_ctx.types().getType(type_generic::Any);
    default:
        return INVALID_TYPE_ID;
    }
}

void AnalysisContext::setExprType(const AstNodeBase* node, TypeId id) {
    if (node) {
        m_exprTypes[node] = id;
    }
}

bool AnalysisContext::uninitVarReported(const AstNodeBase* decl) const {
    return decl != nullptr && m_uninitVarReportedDecls.count(decl) != 0;
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

    // Функция с trust-условиями (пред/пост, m_trust) получает ОТДЕЛЬНЫЙ функциональный TypeId
    // от идентичной сигнатуры без условий (бит kTrustFlag в TypeKind, см. registry.hpp).
    return m_ctx.types().getOrCreateFunctionType(returnType, paramTypes, INVALID_TYPE_ID, !func_node.m_trust.empty());
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

bool AnalysisContext::isRegisteredIntrinsic(std::string_view name) const {
    // Интринсик - НЕ нативная функция (без префикса '%'); имя сопоставляется дословно.
    if (findIntrinsicByName(name).has_value()) {
        return true;
    }
    // Отдельные функции контроля стека (%trust_stack_check, %trust_stack_check_set_reserve,
    // %trust_stack_check_get_reserve, %trust_stack_check_get_limit, %trust_stack_check_set_limit) -
    // реальные C++-функции рантайма (transpiler: ExprEmitter::handleStackCheckNative); не должны
    // давать «undefined name».
    return name == "%trust_stack_check" || name == "%trust_stack_check_set_reserve" || name == "%trust_stack_check_get_reserve" ||
           name == "%trust_stack_check_get_limit" || name == "%trust_stack_check_set_limit";
}

} // namespace trust
