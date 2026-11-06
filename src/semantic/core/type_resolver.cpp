// Резолв типов, ключей их регистрации, модульных ссылок и runtime-символов.
#include "types/type_read.hpp"
#include "semantic/core/type_resolver.hpp"

#include "semantic/core/namespace_scope.hpp"
#include "semantic/ref/ref_qualifiers.hpp"
#include "semantic/ref/stack_check.hpp"
#include "attrs/attr.hpp"
#include "attrs/attr_builtin.hpp"
#include "session/context.hpp"
#include "module_api/module_resolver.hpp"
#include "target/target.hpp"
#include "ast/ref_syntax.hpp"
#include "ast/token_type.hpp"
#include "diag/diag.hpp"
#include "types/intrinsics.hpp"
#include "types/type_system.hpp"
#include "types/names.hpp"
#include "target/runtime_symbols.hpp"
#include "utils/strings.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace trust {

TypeResolution::TypeResolution(Context& ctx, TypeSystem& registry, const SymbolTable& symbols, const NamespaceScope& namespaces)
: m_ctx(ctx)
, m_registry(registry)
, m_symbols(symbols)
, m_namespaces(namespaces) {
}

namespace {

/// Итоговая identity аннотации: материализованный resolvedState (const/reftype/policy/impl) либо base.
TypeId annotationIdentity(const AstNodeBase* node, TypeId base) {
    if (node != nullptr && node->kind() == ParserToken::Kind::TypeName) {
        const auto& it = static_cast<const IdentType&>(*node);
        if (it.resolvedState.isValid()) {
            return it.resolvedState.getTypeId();
        }
    }
    return base;
}

/// Аннотация несёт объявленные квалификаторы, требующие материализации.
bool hasDeclarationQualifiers(const AttrPool& attrs, const AstNodeAttr* node) {
    if (node == nullptr) {
        return false;
    }
    const std::string_view names[] = {attr::ReadOnly, attr::Reftype, attr::Deleter, attr::Lifetime, attr::Pin};
    for (const std::string_view name : names) {
        if (node->has_attr(attrs, name)) {
            return true;
        }
    }
    return false;
}

/// Разбор списка размерностей `[...]` (тензор/массив): элемент обязан быть IntLiteral,
/// значение — в диапазоне uint64. Ошибки сообщаются здесь; nullopt — уже диагностировано.
std::optional<std::vector<uint64_t>> parseDimensions(const IdentType& it, DiagnosticEngine& diag, std::string_view nonIntMsg, std::string_view outOfRangeMsg) {
    std::vector<uint64_t> dims;
    if (!it.dims()) {
        return dims;
    }
    dims.reserve(it.dims()->size());
    for (const auto& d : *it.dims()) {
        if (!d || d->kind() != ParserToken::Kind::IntLiteral) {
            diag.report(Severity::Error, d ? d->range() : it.range(), "{}", std::string(nonIntMsg));
            return std::nullopt;
        }
        unsigned long long v = 0;
        try {
            v = std::stoull(stripDigitSeparators(requiredText(*d)), nullptr, 0);
        } catch (...) {
            diag.report(Severity::Error, d->range(), "{} '{}' is out of range", std::string(outOfRangeMsg), std::string(requiredText(*d)));
            return std::nullopt;
        }
        dims.push_back(v);
    }
    return dims;
}

} // namespace

std::vector<StorageKey> TypeResolution::typeStorageKeys(std::string_view name) const {
    if (m_symbols.inFilePrivateScope()) {
        return std::vector<StorageKey>{m_symbols.declarationStorageKey(name)};
    }
    return m_symbols.declarationStorageKeys(utils::qualifyTrustName(m_namespaces.namespacePath(), name), /*moduleRoot=*/true, SiteDeclVisibility::Public);
}

std::vector<StorageKey> TypeResolution::typeStorageKeys(const AstNodeBase& decl, std::string_view name) const {
    const SiteDeclVisibility declVisibility =
        decl.isFilePrivate() ? SiteDeclVisibility::Private
                             : (decl.visibility == AstNodeBase::Visibility::Protected ? SiteDeclVisibility::Protected : SiteDeclVisibility::Public);
    return m_symbols.declarationStorageKeys(utils::qualifyTrustName(m_namespaces.namespacePath(), name), /*moduleRoot=*/true, declVisibility);
}

std::optional<TypeId> TypeResolution::lookupTemplateParam(std::string_view name) const {
    for (auto it = m_templateParamScopes.rbegin(); it != m_templateParamScopes.rend(); ++it) {
        auto found = it->find(std::string(name));
        if (found != it->end()) {
            return found->second;
        }
    }
    return std::nullopt;
}

void TypeResolution::pushTemplateParamScope(std::vector<std::pair<std::string, TypeId>> params) {
    m_templateParamScopes.emplace_back();
    auto& top = m_templateParamScopes.back();
    for (auto& [name, id] : params) {
        top.emplace(std::move(name), id);
    }
}

void TypeResolution::popTemplateParamScope() {
    EXPECT(!m_templateParamScopes.empty() && "popTemplateParamScope without push");
    m_templateParamScopes.pop_back();
}

std::optional<TypeId> TypeResolution::resolveTupleAnnotation(const IdentType& it, std::string_view name) const {
    // Параметризованный кортеж `Tuple(:Rational, :Rational)` / `Tuple(sum:Rational, ...)` -
    // аннотация структурного Tuple-типа: строим тип через getOrCreateTupleType (элементы
    // позиционные имя="" или именованные). Такой тип становится возвращаемым типом функции ->
    // getOrCreateFunctionType интернирует функции по нему. Распознавание - по типу из реестра
    // (TypeId), а НЕ по строковому сравнению имени. nullopt - аннотация не кортеж.
    if (!it.params() || it.params()->empty()) {
        return std::nullopt;
    }
    auto& reg = m_registry;
    // Резолв имени — единым путём semantic (`lookupTypeName`: скоуп-стек -> ключи реестра).
    if (auto tid = lookupTypeName(name); tid.has_value() && reg.read().store().desugaredTypeId(*tid) == reg.read().getType(type_category::Tuple)) {
        std::vector<std::pair<std::string, QualType>> elems;
        elems.reserve(it.params()->size());
        for (const auto& p : *it.params()) {
            if (!p) {
                continue;
            }
            TypeId et = INVALID_TYPE_ID;
            std::string ename;
            if (p->kind() == ParserToken::Kind::TypeName) {
                et = resolveTypeRef(*p).value_or(INVALID_TYPE_ID); // позиционный тип-параметр :Rational
            } else if (p->kind() == ParserToken::Kind::ArgNode) {
                // Именованный параметр `sum:Rational` -> ArgNode(name, type).
                const auto& pd = static_cast<const ArgNode&>(*p);
                ename = std::string(pd.text());
                if (pd.m_type) {
                    et = resolveTypeRef(*pd.m_type).value_or(INVALID_TYPE_ID);
                }
            }
            elems.emplace_back(ename, QualType::fromTypeId(getUnqualifiedType(et)));
        }
        return reg.interner().getOrCreateTupleType(std::move(elems));
    }
    return std::nullopt;
}

std::optional<TypeId> TypeResolution::resolveRefAnnotation(const AstNodeBase& type_node) const {
    // Символический сигл ссылочного типа в аннотации (`x : &Int32` / `*Int32` / `&?Int32`):
    // `&`/`&?` -> RefMakeExpr, `*` -> RefTakeExpr (грамматика `COLON STAR NAME` -> TAKE).
    // Единственный ребёнок - pointee-тип; сигл (text()) отображается на вид ссылки и применяется
    // к pointee (первая ссылка - fast-path бит, вложенность - составной узел).
    const auto& seq = static_cast<const Sequence&>(type_node);
    if (seq.m_body.empty()) {
        return INVALID_TYPE_ID;
    }
    const auto kind = refKindOfTypeNode(&type_node);
    if (!kind) {
        m_ctx.diag().report(Severity::Error, type_node.range(), "unsupported reference sigil '{}' in type annotation", ParserToken::name(type_node.kind()));
        return INVALID_TYPE_ID;
    }
    const auto pointee = resolveTypeRef(*seq.m_body[0]);
    if (!pointee.has_value() || *pointee == INVALID_TYPE_ID) {
        return INVALID_TYPE_ID;
    }
    return m_registry.interner().applyRefType(*pointee, *kind);
}

std::optional<TypeId> TypeResolution::resolveTypeRef(const AstNodeBase& type_node, TypeId* deleterOutsideType) const {
    // Материализованный результат (кэш): повторный резолв того же узла не переприменяет
    // квалификаторы и не дублирует диагностику.
    if (type_node.kind() == ParserToken::Kind::TypeName) {
        const auto& it = static_cast<const IdentType&>(type_node);
        if (it.resolvedState.isValid() && it.resolvedTypeId.isValid()) {
            return it.resolvedTypeId;
        }
    }
    const std::optional<TypeId> resolved = resolveTypeRefImpl(type_node);
    if (!resolved.has_value() || *resolved == INVALID_TYPE_ID) {
        return resolved;
    }
    // Аннотация с квалификаторами: финальный тип (const/reftype/policy/impl) материализуется
    // ОДИН раз здесь; identity остаётся базовой для проверок, требующих pointee.
    if (type_node.kind() == ParserToken::Kind::TypeName) {
        const AstNodeAttr* attrNode = type_node.as_attr();
        const AttrPool& attrs = m_ctx.attrs();
        if (hasDeclarationQualifiers(attrs, attrNode)) {
            const auto& it = static_cast<const IdentType&>(type_node);
            it.resolvedState = applyAnnotationQualifiers(QualType::fromTypeId(*resolved), *attrNode, type_node.range(), deleterOutsideType);
        }
    }
    // Материализация факта: кодоген рендерит разрешённый тип узла и не резолвит имя заново.
    recordTypeName(type_node, *resolved);
    return resolved;
}

QualType TypeResolution::applyAnnotationQualifiers(QualType base, const AstNodeAttr& node, MapperRange range, TypeId* deleterOutsideType) const {
    return applyDeclarationQualifiers(m_ctx, *this, base, node, range, deleterOutsideType);
}

void TypeResolution::reportAmbiguousName(const AstNodeBase& node, std::string_view name, const std::vector<ModuleIndex>& candidates,
                                         std::string_view what) const {
    std::string modules;
    for (const ModuleIndex index : candidates) {
        if (!modules.empty()) {
            modules.append(", ");
        }
        modules.append(moduleLabel(index));
    }
    const std::string hint = candidates.empty() ? std::string(name) : utils::qualifyTrustName(moduleLabel(candidates.front()), name);
    m_ctx.diag().report(Severity::Error, node.range(), "ambiguous {} '{}': exported by {}; qualify it as '{}'", what, name, modules, hint);
}

void TypeResolution::recordTypeName(const AstNodeBase& node, TypeId type_id) const {
    if (type_id == INVALID_TYPE_ID) {
        return;
    }
    if (!node.is<IdentName>()) {
        return;
    }
    node.as<IdentName>()->resolvedTypeId = type_id;
}

TypeId TypeResolution::typeNameOfSymbol(const Symbol& sym) const {
    if (sym.decl == nullptr) {
        return INVALID_TYPE_ID;
    }
    // Нативный шаблон-ТИП объявляется оператором типа (`<T> Name ::= %cpp { };`), а не FuncDecl
    // (остальные FuncDecl — функции, не типы). Struct/Class — RecordDecl/ClassDecl, алиасы — TypeDecl.
    const bool denotesType = isTypeDeclKind(sym.decl->kind());
    return denotesType ? sym.type.getTypeId() : INVALID_TYPE_ID;
}

void TypeResolution::aliasTypeKeys(TypeId id, const std::vector<StorageKey>& keys) const {
    for (std::size_t i = 1; i < keys.size(); ++i) {
        m_registry.names().aliasKey(id, keys[i]);
    }
}

std::optional<TypeId> TypeResolution::lookupTypeName(std::string_view name) const {
    if (const Symbol* s = m_symbols.resolve(name);
        s != nullptr && s->decl != nullptr && isTypeDeclKind(s->decl->kind()) && s->type.getTypeId() != INVALID_TYPE_ID) {
        return s->type.getTypeId();
    }
    // Ключи visibility: private/hidden текущего модуля, затем квалифицированное имя, затем плоское.
    const std::string qualified = utils::qualifyTrustName(m_namespaces.namespacePath(), name);
    for (const auto& key : m_symbols.storageKeyVariants(qualified)) {
        if (auto id = m_ctx.readTypes().findType(key)) {
            return id;
        }
    }
    if (qualified != name) {
        for (const auto& key : m_symbols.storageKeyVariants(name)) {
            if (auto id = m_ctx.readTypes().findType(key)) {
                return id;
            }
        }
    }
    return std::nullopt;
}

std::string TypeResolution::moduleLabel(ModuleIndex index) const {
    const ModuleResolver* resolver = m_ctx.moduleResolver();
    EXPECT(resolver != nullptr && "moduleLabel: Pipeline must inject ModuleLoader into Context");
    return std::string(resolver->moduleLogicalName(index));
}

std::optional<TypeId> TypeResolution::resolveTypeRefImpl(const AstNodeBase& type_node) const {
    if (type_node.kind() == ParserToken::Kind::RefMakeExpr || type_node.kind() == ParserToken::Kind::RefTakeExpr) {
        return resolveRefAnnotation(type_node);
    }
    if (type_node.kind() != ParserToken::Kind::TypeName) {
        return std::nullopt;
    }
    const auto& it = static_cast<const IdentType&>(type_node);
    std::string_view name = utils::stripTypeSigil(it.text());
    // Неоднозначное голое имя типа (импортировано из нескольких модулей) — ошибка на сайте;
    // выбор только квалификацией `\модуль::T`.
    if (const std::vector<ModuleIndex>* candidates = m_symbols.ambiguousModules(name); candidates != nullptr) {
        reportAmbiguousName(type_node, name, *candidates, "type name");
        return INVALID_TYPE_ID;
    }
    // Нативный шаблон-тип `vector<Int32>` (IdentType::isTemplate): резолв абстрактного шаблона по
    // имени + интернирование конкретной инстанциации. Встроенные контейнеры (`std::vector` →
    // `:Array`) инстанцируются через Array-структуру (объединение с `:Array`); прочие - через
    // NativeTemplateTypeData. Если базы-шаблона нет - это ошибка (в т.ч. shift→template
    // переработал сравнение `:Int8 < 5` в `:Int8<5>`, где Int8 не шаблон).
    if (it.isTemplate()) {
        auto& reg = m_registry;
        auto base = lookupTypeName(name);
        // Резолв типовых аргументов (общий для record- и native-шаблонов): `:Int32`, `:MyClass<:Int8>`
        // либо голое имя `Int32`.
        const auto resolveArgs = [&]() {
            std::vector<TypeId> args;
            if (it.templateArgs()) {
                const auto& targs = *it.templateArgs();
                size_t packCount = 0;
                for (size_t idx = 0; idx < targs.size(); ++idx) {
                    const auto& a = targs[idx].node;
                    if (!a) {
                        continue;
                    }
                    TypeId at = INVALID_TYPE_ID;
                    if (a->kind() == ParserToken::Kind::TypeName) {
                        at = resolveTypeRef(*a).value_or(INVALID_TYPE_ID);
                    } else if (a->kind() == ParserToken::Kind::Ident) {
                        // Голое `T` в типовых аргументах generic-функции/шаблона: тип живёт
                        // в template-param scope, а не в глобальном реестре.
                        const std::string_view an = utils::stripTypeSigil(requiredText(*a));
                        if (auto tp = lookupTemplateParam(an)) {
                            at = *tp;
                        } else if (const Symbol* s = m_symbols.resolve(an);
                                   s != nullptr && s->type.getTypeId() != INVALID_TYPE_ID &&
                                   reg.read().store().isTemplateParamType(reg.read().store().desugaredTypeId(s->type.getTypeId()))) {
                            // Типовой параметр шаблон-КЛАССА (`<T> Iterator ::= ... { Iterator<T> ... }`):
                            // живёт в SymbolTable класса, а не в template-param scope функций.
                            at = s->type.getTypeId();
                        } else {
                            at = lookupTypeName(an).value_or(INVALID_TYPE_ID);
                        }
                    }
                    if (at == INVALID_TYPE_ID) {
                        // Неразрешённый ТИПОВОЙ аргумент - ошибка, а не тихий пропуск
                        // (value-аргументы: digits/literal/paren - отдельные kind, сюда не входят).
                        if (a->kind() == ParserToken::Kind::TypeName || a->kind() == ParserToken::Kind::Ident) {
                            m_ctx.diag().report(Severity::Error, a->range(), "unknown type argument '{}'", requiredText(*a));
                        }
                        continue;
                    }
                    at = getUnqualifiedType(at);
                    const bool isPackArg = targs[idx].isPack;
                    if (isPackArg) {
                        ++packCount;
                        if (!reg.read().store().isTemplateParamType(reg.read().store().desugaredTypeId(at))) {
                            m_ctx.diag().report(Severity::Error, a->range(), "type parameter pack expansion requires a type parameter");
                            continue;
                        }
                        at = reg.interner().getOrCreatePackExpansionType(at);
                    }
                    args.push_back(at);
                }
                if (packCount > 1) {
                    m_ctx.diag().report(Severity::Error, it.range(), "zip of multiple type parameter packs is not implemented; at most one pack is allowed");
                }
            }
            return args;
        };
        // Пользовательский шаблон-класс (`<T> :Box ::= :Class{...}`): инстанциация record-шаблона.
        if (base.has_value() && reg.read().store().isRecordTemplate(*base)) {
            const std::size_t expected = reg.read().store().templateParamsOf(*base).size();
            std::vector<TypeId> args = resolveArgs();
            for (const TypeId a : args) {
                if (reg.read().store().isPackExpansionType(a)) {
                    m_ctx.diag().report(Severity::Error, it.range(), "record template '{}' cannot take a type parameter pack", name);
                    return INVALID_TYPE_ID;
                }
            }
            if (args.size() != expected) {
                m_ctx.diag().report(Severity::Error, it.range(), "template '{}' expects {} type argument(s), but {} provided", name, expected, args.size());
                return INVALID_TYPE_ID;
            }
            // Контекст инстанциации (R30/R50): диагностики внутри (лимит глубины, цикл, подстановка
            // полей) идут с цепочкой Notes «in instantiation of …» + точкой запроса. Стек — у
            // владельца диагностики (`diag`), а не в AST/узле.
            DiagnosticEngine::ContextScope instantiationScope(m_ctx.diag(), it.range(), std::format("in instantiation of template '{}'", name));
            return reg.interner().getOrCreateRecordTemplateInstance(*base, std::move(args));
        }
        // Имя не найдено либо найдено, но не является шаблоном (record-шаблоны обработаны выше).
        if (!base.has_value() || !reg.read().store().isNativeTemplateType(*base)) {
            m_ctx.diag().report(Severity::Error, it.range(), "type '{}' is not a template", name);
            return INVALID_TYPE_ID;
        }
        const TypeReadModel trend = m_ctx.readTypes();
        const CppTypeFacts* baseFacts = m_ctx.target().typeCppFacts().factsOf(trend.store().cppFactsKeyOf(trend.store().desugaredTypeId(*base)));
        const std::string cppTpl = baseFacts != nullptr ? baseFacts->cppTemplate : std::string{};
        std::vector<TypeId> args = resolveArgs();
        // Встроенные контейнеры: `std::vector` → `:Array` (mutable, std::vector<Elem>); `std::array`
        // → константный `:Array^` (B2, std::array<Elem,N>). Инстанциация объединяется с `:Array`.
        if (cppTpl == "std::vector" && !args.empty()) {
            if (reg.read().store().isPackExpansionType(args[0])) {
                m_ctx.diag().report(Severity::Error, it.range(), "a type parameter pack cannot be used as an array element type");
                return INVALID_TYPE_ID;
            }
            return reg.interner().getOrCreateArrayType(args[0]);
        }
        // Общий нативный шаблон (`std::pair` и т.п.): идентичность — языковой ключ (абстрактный
        // шаблон `*base`); C++-имя/include — целевые факты абстрактного шаблона (on-use).
        return reg.interner().getOrCreateNativeTemplateType(*base, std::move(args));
    }
    if (auto tuple = resolveTupleAnnotation(it, name); tuple.has_value()) {
        return tuple;
    }
    // Типовой параметр активной generic-функции (`<T> f(x: T)`): его TypeId живёт в
    // template-param scope AnalysisContext, а не в SymbolTable (имя функции должно
    // регистрироваться во внешнем скоупе).
    if (auto tp = lookupTemplateParam(name)) {
        return *tp;
    }
    // Пользовательские алиасы, связанные в скоуп-стеке (с учётом shadowing).
    if (const Symbol* s = m_symbols.resolve(name)) {
        // Типовой параметр шаблона (Symbol с типом Group::kTemplateParam): возвращаем его тип,
        // чтобы поля/методы `: T` резолвились внутри определения шаблон-класса.
        if (s->type.getTypeId() != INVALID_TYPE_ID && m_ctx.readTypes().store().isTemplateParamType(s->type.getTypeId())) {
            return s->type.getTypeId();
        }
        if (s->decl && s->decl->kind() == ParserToken::Kind::TypeDecl && s->type.getTypeId() != INVALID_TYPE_ID) {
            // Абстрактный пользовательский шаблон-класс без аргументов (`:Box` вместо `Box<...>`).
            if (m_ctx.readTypes().store().isRecordTemplate(s->type.getTypeId())) {
                m_ctx.diag().report(Severity::Error, it.range(), "template '{}' requires type arguments (e.g. {}<Int32>)", name, name);
                return INVALID_TYPE_ID;
            }
            return s->type.getTypeId();
        }
    }
    // Builtin-типы и глобальные алиасы - в реестре типов (объявления скоупа - выше).
    auto base = lookupTypeName(name);
    if (!base.has_value()) {
        return std::nullopt;
    }
    // Абстрактный пользовательский шаблон-класс без аргументов (`:Box` вместо `Box<...>`) - ошибка.
    if (m_ctx.readTypes().store().isRecordTemplate(*base)) {
        m_ctx.diag().report(Severity::Error, it.range(), "template '{}' requires type arguments (e.g. {}<Int32>)", name, name);
        return INVALID_TYPE_ID;
    }
    // Тензорный тип `:Tensor[dims]`: dtype выводится из инициализатора (аннотации элементов
    // или значений). `:Tensor(...)` с типовым аргументом не является синтаксисом языка.
    const bool tensorBase =
        (m_ctx.readTypes().store().desugaredTypeId(*base) == m_ctx.readTypes().store().desugaredTypeId(m_ctx.readTypes().getType(type::Tensor)) ||
         m_ctx.readTypes().store().desugaredTypeId(*base) == m_ctx.readTypes().store().desugaredTypeId(m_ctx.readTypes().getType(type::SparseTensor)));
    if (tensorBase) {
        const bool sparse =
            m_ctx.readTypes().store().desugaredTypeId(*base) == m_ctx.readTypes().store().desugaredTypeId(m_ctx.readTypes().getType(type::SparseTensor));
        if (it.params() && !it.params()->empty()) {
            m_ctx.diag().report(Severity::Error, it.range(), "tensor element type is specified by the initializer/annotation, not by ':Tensor(...)'");
            return INVALID_TYPE_ID;
        }
        std::vector<uint64_t> shape;
        if (it.dims() && !it.dims()->empty()) {
            auto parsed = parseDimensions(it, m_ctx.diag(), "tensor shape dimensions must be integer literals", "tensor dimension");
            if (!parsed) {
                return INVALID_TYPE_ID;
            }
            shape = std::move(*parsed);
        }
        if (shape.empty()) {
            return *base; // `:Tensor` без формы - абстрактный (методы/динамический тип)
        }
        // dtype неизвестен: временный тип с Any; coerceArrayInitToTarget заменит его конкретным
        // по аннотации элемента/инициализатору. Без инициализатора кодогенерация даст диагностику.
        return m_registry.interner().getOrCreateTensorType(m_ctx.readTypes().getType(type_generic::Auto), std::move(shape), sparse);
    }
    // `:Elem[d1,d2,...]` (более одной размерности) - тензор с элементным типом Elem.
    // Одномерное `:Elem[n]` остаётся массивом (фиксированный std::array).
    if (it.dims() && it.dims()->size() > 1) {
        const TypeId baseCanon = m_ctx.readTypes().store().desugaredTypeId(*base);
        const Group elemGroup = typeGroupOf(baseCanon);
        const bool scalarElem = elemGroup == Group::kLogical || elemGroup == Group::kIntegers || elemGroup == Group::kUnsigned || elemGroup == Group::kNumbers;
        if (scalarElem) {
            auto parsed = parseDimensions(it, m_ctx.diag(), "tensor shape dimensions must be integer literals", "tensor dimension");
            if (!parsed) {
                return INVALID_TYPE_ID;
            }
            return m_registry.interner().getOrCreateTensorType(*base, std::move(*parsed));
        }
    }
    // Тип-определение массива `:Elem[3]` / `:Elem[3,4]`: размерности из `[...]` (без финальной
    // запятой) → структурный Array<Elem, dims> (ArrayTypeData). Определения N-D (`[3,4]`) работают
    // без кодогенерации (диагностика «не реализовано» при генерации); 1D (`[3]`) - фиксированный
    // (std::array). Размерности кладутся в данные типа.
    if (it.dims() && !it.dims()->empty()) {
        std::vector<uint64_t> dims;
        auto parsed = parseDimensions(it, m_ctx.diag(), "array dimension must be an integer literal", "array dimension");
        if (!parsed) {
            return INVALID_TYPE_ID;
        }
        dims = std::move(*parsed);
        if (!dims.empty()) {
            // Определение типа массива `:Elem[3]`: изменяемый массив (std::vector) с известной
            // размерностью (для проверки границ при доступе). N-D определения работают без
            // кодогенерации (при генерации - диагностика «не реализовано»).
            return m_registry.interner().getOrCreateArrayType(*base, std::move(dims));
        }
    }
    return base;
}
TypeId TypeResolution::buildFuncType(const FuncDecl& func_node) const {
    std::vector<TypeId> paramTypes;
    bool variadic = false;
    TypeId paramPackType = INVALID_TYPE_ID;
    uint8_t paramPackPosition = 0;
    if (func_node.m_params) {
        const size_t nparams = func_node.m_params->size();
        for (size_t i = 0; i < nparams; ++i) {
            const auto& p = (*func_node.m_params)[i];
            if (!p || p->kind() != ParserToken::Kind::ArgNode) {
                continue;
            }
            const auto& pd = static_cast<const ArgNode&>(*p);
            // Замыкающее многоточие ПРОТОТИПА (`f(a:Int32, ...)`) - «произвольное число аргументов
            // в последней позиции», а НЕ параметр: в paramTypes НЕ входит, кодируется variadicType
            // (= Any: тип не фиксирован). В C++ эмитится как чистая variadic-метка `...` (по AST).
            if (isVariadicParamMarker(p.get())) {
                variadic = true;
                continue;
            }
            TypeId pt = INVALID_TYPE_ID;
            if (pd.m_type) {
                pt = annotationIdentity(pd.m_type.get(), resolveTypeRef(*pd.m_type).value_or(INVALID_TYPE_ID));
            }
            // Типовой пакет параметров (`xs: T...`): отдельный слот сигнатуры. Форму
            // (один pack, trailing) валидирует analyzeFuncDecl ДО buildFuncType - здесь только
            // инвариант, без дублирования диагностик.
            if (pd.isTypePack) {
                EXPECT(paramPackType == INVALID_TYPE_ID && "multiple type parameter packs must be rejected before buildFuncType");
                EXPECT(i + 1 == nparams && "non-trailing type parameter pack must be rejected before buildFuncType");
                paramPackType = pt;
                paramPackPosition = 1; // trailing
                continue;
            }
            paramTypes.push_back(pt);
        }
    }

    TypeId returnType = INVALID_TYPE_ID; // INVALID = Void
    if (func_node.m_type) {
        returnType = annotationIdentity(func_node.m_type.get(), resolveTypeRef(*func_node.m_type).value_or(INVALID_TYPE_ID));
    }
    const TypeId variadicType = variadic ? m_ctx.readTypes().getType(type_generic::Auto) : INVALID_TYPE_ID;

    // Функция с trust-условиями (пред/пост, m_trust) получает ОТДЕЛЬНЫЙ функциональный TypeId
    // от идентичной сигнатуры без условий (бит kTrustFlag в TypeKind, см. registry.hpp).
    return m_registry.interner().getOrCreateFunctionType(returnType, paramTypes, variadicType, !func_node.m_trust.empty(), paramPackType, paramPackPosition);
}

void TypeResolution::coerceTemplateCtor(AstNodeBase* init, TypeId target) const {
    if (init == nullptr || target == INVALID_TYPE_ID || init->kind() != ParserToken::Kind::CallExpr) {
        return;
    }
    auto& call = static_cast<CallExpr&>(*init);
    if (!call.m_callee || call.m_callee->kind() != ParserToken::Kind::Ident) {
        return;
    }
    TypeSystem& reg = m_registry;
    // Callee обозначает тип? Предпочтителен материализованный `resolvedTypeId`; если обход ещё
    // не записал его, резолвим символ в скоупе (учитывает shadowing) и записываем факт.
    TypeId calleeType = call.m_callee->as<IdentName>()->resolvedTypeId;
    if (calleeType == INVALID_TYPE_ID) {
        if (const Symbol* calleeSym = m_symbols.resolve(requiredText(*call.m_callee))) {
            calleeType = typeNameOfSymbol(*calleeSym);
            if (calleeType != INVALID_TYPE_ID) {
                recordTypeName(*call.m_callee, calleeType);
            }
        }
    }
    if (calleeType == INVALID_TYPE_ID) {
        return;
    }
    // Record-шаблон: тип-цель обязан быть инстанциацией ИМЕННО этого шаблона.
    if (reg.read().store().isRecordTemplate(calleeType)) {
        const auto inst = reg.read().store().instanceOf(target);
        if (!inst) {
            return;
        }
        if (reg.read().store().desugaredTypeId(inst->templateOf) != reg.read().store().desugaredTypeId(calleeType)) {
            return;
        }
        call.resultType = target;
        call.resultState = QualType::fromTypeId(target);
        return;
    }
    // Native-шаблон (Iterator<T>): цель - его инстанциация с тем же C++-шаблоном.
    if (reg.read().store().isNativeTemplateType(calleeType) && reg.read().store().isNativeTemplateType(target)) {
        // Языковая идентичность (абстрактный шаблон `templateOf`, R37): совпадение abstract-шаблонов.
        if (reg.read().store().nativeTemplateOwner(target) != reg.read().store().nativeTemplateOwner(calleeType)) {
            return;
        }
        call.resultType = target;
        call.resultState = QualType::fromTypeId(target);
    }
}

bool TypeResolution::isRegisteredRuntimeSymbol(std::string_view name) const {
    // Имя в trust-коде сопоставляется с ИСТОЧНИК-именем реестра (C++-символ отличается
    // пространством имён, напр. `trust__print__` ↔ `trust::trust__print__`).
    return findRuntimeSymbolBySourceName(name).has_value();
}

bool TypeResolution::isRegisteredIntrinsic(std::string_view name) const {
    // Интринсик - не нативная функция: имя сопоставляется дословно (без '%').
    // Функции контроля стека (`trust_stack_check*`) — тоже записи реестра интринсиков.
    return findIntrinsicByName(name).has_value();
}

const AstNodeBase* TypeResolution::trustTypeDeclOf(TypeId typeId) const {
    if (typeId == INVALID_TYPE_ID || !isTrustedType(typeId)) {
        return nullptr;
    }
    const TypeReadModel trend = m_ctx.readTypes();
    const std::string_view typeName = trend.store().nameOf(getUnqualifiedType(typeId));
    if (typeName.empty()) {
        return nullptr;
    }
    const Symbol* s = m_symbols.resolve(typeName);
    if (s == nullptr || s->decl == nullptr || s->decl->kind() != ParserToken::Kind::TypeDecl) {
        return nullptr;
    }
    return s->decl;
}

} // namespace trust
