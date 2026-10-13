#pragma once

// include/semantic/core/type_resolver.hpp
// Query-сервис резолва типов и имён в модульном пространстве: аннотации типов, ключи
// регистрации типа, материализация «идентификатор обозначает тип», модульные ссылки,
// runtime-символы/интринсики. Владеет только стеком типовых параметров generic-функций.
// Материализованные факты пишутся в ПОЛЯ узлов AST (кодоген их не пересчитывает).

#include "ast/ast_nodes.hpp"
#include "location/module_index.hpp"
#include "symbols/symbol_table.hpp"
#include "types/names.hpp"
#include "types/type_id.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace trust {

class Context;
class NamespaceScope;
class Symbol;

class TypeResolution {
  public:
    TypeResolution(Context& ctx, const SymbolTable& symbols, const NamespaceScope& namespaces);

    // -- Скоупы типовых параметров generic-функций (не SymbolTable: имя функции обязано
    //    регистрироваться во ВНЕШНЕМ скоупе, а resolveTypeRef обязан видеть `T`). --
    void pushTemplateParamScope(std::vector<std::pair<std::string, TypeId>> params);
    void popTemplateParamScope();
    [[nodiscard]] std::optional<TypeId> lookupTemplateParam(std::string_view name) const;

    // -- Ключи регистрации типа --
    [[nodiscard]] std::vector<StorageKey> typeStorageKeys(std::string_view name) const;
    [[nodiscard]] std::vector<StorageKey> typeStorageKeys(const AstNodeBase& decl, std::string_view name) const;
    void aliasTypeKeys(TypeId id, const std::vector<StorageKey>& keys) const;

    // -- Резолв типа-аннотации --
    /// Резолвит аннотацию и МАТЕРИАЛИЗУЕТ результат в узле (`IdentName::resolvedTypeId`).
    [[nodiscard]] std::optional<TypeId> resolveTypeRef(const AstNodeBase& type_node) const;
    /// Поиск типа по имени: скоуп-стек (алиасы/shadowing), затем реестр типов.
    [[nodiscard]] std::optional<TypeId> lookupTypeName(std::string_view name) const;
    /// Материализует «идентификатор обозначает тип» (`IdentName`/`IdentType::resolvedTypeId`).
    void recordTypeName(const AstNodeBase& node, TypeId type_id) const;
    /// Тип, если символ обозначает ТИП, иначе INVALID_TYPE_ID.
    [[nodiscard]] TypeId typeNameOfSymbol(const Symbol& sym) const;
    /// Узел-объявление типа, к которому относится доверенный тип (nullptr - не доверенный).
    [[nodiscard]] const AstNodeBase* trustTypeDeclOf(TypeId typeId) const;
    /// Функциональный тип по сигнатуре объявления (вход `getOrCreateFunctionType`).
    [[nodiscard]] TypeId buildFuncType(const FuncDecl& func_node) const;
    /// Типизация конструктора шаблона по типу-цели контекста (иначе no-op).
    void coerceTemplateCtor(AstNodeBase* init, TypeId target) const;

    // -- Модульные ссылки --
    [[nodiscard]] const Symbol* resolveModuleRef(const IdentName& node) const;
    void reportModuleRefError(const IdentName& node) const;
    void reportAmbiguousName(const AstNodeBase& node, std::string_view name, const std::vector<ModuleIndex>& candidates, std::string_view what) const;
    [[nodiscard]] std::string moduleLabel(ModuleIndex index) const;

    // -- Runtime-символы и интринсики --
    [[nodiscard]] bool isRegisteredRuntimeSymbol(std::string_view name) const;
    [[nodiscard]] bool isRegisteredIntrinsic(std::string_view name) const;

  private:
    [[nodiscard]] std::optional<TypeId> resolveTypeRefImpl(const AstNodeBase& type_node) const;
    /// Единственная точка применения ортогональных квалификаторов аннотации (const + reftype).
    [[nodiscard]] QualType applyAnnotationQualifiers(QualType base, const AstNodeAttr& node, MapperRange range) const;
    [[nodiscard]] std::optional<TypeId> resolveRefAnnotation(const AstNodeBase& type_node) const;
    [[nodiscard]] std::optional<TypeId> resolveTupleAnnotation(const IdentType& it, std::string_view name) const;

    Context& m_ctx;
    const SymbolTable& m_symbols;
    const NamespaceScope& m_namespaces;
    std::vector<std::unordered_map<std::string, TypeId>> m_templateParamScopes;
};

} // namespace trust
