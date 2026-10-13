#pragma once

// include/transpiler/emit/decl_emit.hpp
// Компонент кодогенерации: DeclEmitter. Разделяет CppEmitContext с драйвером CppTranspiler;
// рекурсия/вызовы других компонентов идут через драйвер (friend).

#include "transpiler/emit/emit_ctx.hpp"
#include "ast/ast_nodes.hpp"
#include "location/location.hpp"
#include "types/type_id.hpp"
#include "types/runtime_symbols.hpp"
#include "types/intrinsics.hpp"
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace trust {

class CppTranspiler;

/// Тело free-функции обязано быть в заголовке модуля: generic (C++ template) или
/// `@[constexpr]` (определение нужно в TU использования).
[[nodiscard]] bool bodyMustBeInModuleHeader(const FuncDecl& func, const AttrPool& attrs);

class DeclEmitter {
  public:
    explicit DeclEmitter(CppEmitContext& ectx, CppTranspiler& driver)
    : m_ectx(ectx)
    , m_driver(driver) {}
    void generateVarDeclToFile(const VarDecl& var_node, MapperFile output_idx);
    void generateTypeDeclToFile(const Binary& binary_node, MapperFile output_idx);
    void emitEnumStruct(std::string_view enum_trust, const DictLiteralNode& dict, TypeId enum_id, MapperFile output_idx, MapperRange typeNameRange);
    void emitVariantStruct(std::string_view variant_trust, const DictLiteralNode& dict, TypeId variant_id, MapperFile output_idx, MapperRange typeNameRange);
    /// Эмиссия пользовательского Record-типа (Struct/Class): `struct c_Name [: public c_Base...] { ... };`.
    /// Struct (Group::kStructs) дополнительно получает static_assert POD.
    void emitRecordDecl(const RecordDecl& rec, TypeId rec_id, MapperFile output_idx, MapperRange typeNameRange);
    /// Тела методов типа, ОПУБЛИКОВАННОГО заголовком модуля: заголовок содержит только прототипы,
    /// поэтому out-of-line определения (`Owner::name`) эмитятся в исполняющем артефакте (`.cppt`).
    /// Не вызывается в forward-режиме (эмиссия самого заголовка).
    void emitPublishedRecordMembers(const Binary& typeDecl);
    void mapDeclaredName(MapperFile output_idx, MapperRange trustRange, uint32_t prefixLen, std::string_view name, std::string_view cppName);
    /// `isMember` — сигнатура члена (влияет на dummy-параметр postfix-оператора);
    /// `memberOwner` — C++-имя владельца для OUT-OF-LINE определения (`Owner::name`, пусто —
    /// объявление в своей области); `suppressDefaults` — не повторять default-аргументы
    /// (они уже выведены в прототипе заголовка; повтор в определении запрещён C++).
    void generateFuncDeclToFile(const FuncDecl& func_node, MapperFile output_idx, bool isMember = false, std::string_view memberOwner = {},
                                bool suppressDefaults = false);
    /// Лямбда-выражение `[captures](params):Ret { body }` как ЗНАЧЕНИЕ: C++ `[c_x](...) -> Ret { ... }`.
    /// НЕ top-level функция - эмитится в позиции выражения (инициализатор/аргумент/немедленный вызов).
    void emitLambdaExpr(const FuncDecl& func_node);
    void visit_ModuleDecl(const ModuleNode& n);
    /// Тело модульного юнита в автоматическом namespace модуля (entry/embed — вне конверта).
    void emitModuleBodyIsolated(const ModuleNode& n, MapperFile out);
    void emitModuleImportDecls(const ModuleNode& n);
    /// Имя-зависимые факты (native-адрес, защита стека) экспорт-набора импортируемого модуля —
    /// без эмиссии C++-объявлений: их публикует заголовок, а кодогену клиента нужны эти факты.
    void registerImportedDeclFacts(const std::vector<AstNodePtr>& nodes);
    /// Интерфейсная проекция модуля для артефакта `<enc>.hpp` по ПОДЛИННОМУ экспорт-набору модуля.
    /// Клиент получает её через `#include` заголовка; `emitImportScope` с `m_mode.forwardDeclOnly`
    /// вызывается только внутри этой генерации. Корневой `ModuleNode` разворачивается
    /// (модуль — не namespace-сегмент).
    void emitModuleHeaderBody(const std::vector<AstNodePtr>& roots, const std::set<const AstNodeBase*>& terms, MapperFile out,
                              std::string_view moduleNamespace);
    /// Группировка объявлений заголовка по «вне/внутри» автоматического namespace модуля
    /// (`staysOutsideModuleNamespace`): нативные декларации и entry — вне конверта.
    void emitModuleHeaderChunks(const std::vector<AstNodePtr>& body, const std::set<const AstNodeBase*>& terms, MapperFile out,
                                std::string_view moduleNamespace, bool types_pass);
    /// `types_pass` — проход по типам (`true`) либо по остальным объявлениям (`false`).
    void emitImportScope(const std::vector<AstNodePtr>& body, const std::set<const AstNodeBase*>& terms, MapperFile out, bool types_pass);
    void visit_VarDecl(const VarDecl& n);
    void visit_FuncDecl(const FuncDecl& n);
    void visit_TypeDecl(const Binary& n);
    void visit_NameDecl(const Binary& n);
    void visit_EnumDecl(const Sequence&);
    void visit_EnumMember(const Sequence&);
    void visit_StructDecl(const RecordDecl&);
    void visit_StructField(const Sequence&);
    void visit_ClassDecl(const ClassDecl&);

  private:
    struct Signature;
    bool prepareFunctionSignature(const FuncDecl& func_node, bool isMember, std::string_view memberOwner, bool suppressDefaults, Signature& sig);
    void emitFunctionSignature(const FuncDecl& func_node, const Signature& sig, MapperFile output_idx);
    void emitFunctionBody(const FuncDecl& func_node, const Signature& sig, MapperFile output_idx);

    CppEmitContext& m_ectx;
    CppTranspiler& m_driver;
};

} // namespace trust
