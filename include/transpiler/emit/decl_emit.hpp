#pragma once

// Компонент кодогенерации: DeclEmitter. Разделяет CppEmitContext с драйвером CppTranspiler;
// рекурсия/вызовы других компонентов идут через драйвер (friend).

#include "transpiler/emit/emit_ctx.hpp"
#include "transpiler/emit/emit_common.hpp"
#include "ast/ast_nodes.hpp"
#include "location/decl_id.hpp"
#include "location/location.hpp"
#include "types/type_id.hpp"
#include "target/runtime_symbols.hpp"
#include "types/intrinsics.hpp"
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace trust {

class CppTranspiler;

/// Спецификация C++-класса для ЕДИНОЙ эмиссии (`DeclEmitter::emitCppClass`). Пользовательский
/// Record (Struct/Class) и обёртка/генерируемый builtin-класс различаются ТОЛЬКО этими данными:
/// ключевым словом, namespace-сегментами, базами, прологом и источником полей/членов.
struct CppClassSpec {
    std::string_view keyword;            ///< "struct" (record) | "class" (обёртка/builtin)
    std::string templateHeader;          ///< "" либо "template <...>\n"
    std::vector<std::string> namespaces; ///< namespace-сегменты C++-имени (обёртка); record — пусто
    std::string className;               ///< локальное C++-имя класса
    std::vector<CppBase> bases;
    std::string preamble;            ///< после `{\n` (обёртка: `public:` + using/ctor); "" для record
    bool emitMethods = true;         ///< эмитить методы (обёртка без собственных членов — false)
    bool recordIsTemplate = false;   ///< влияет на «тело обязано быть в заголовке»
    std::string_view ownerTrustName; ///< trust-имя record-типа (диагностики методов)
    std::vector<AstNodePtr> members; ///< члены тела в порядке объявления (поле VarDecl/метод FuncDecl)
    /// Поля record из РЕЕСТРА (форма): ОДИН контейнер записей, ключ — `DeclId` объявления поля
    /// (R14: связь «член AST ↔ поле формы» по ключу, не по индексу). Пусто у обёртки.
    struct CppFieldSpec {
        DeclId declId;         ///< ключ: id члена AST (поле формы)
        std::string cppType;   ///< C++-тип поля
        std::string trustName; ///< trust-имя поля
    };
    std::vector<CppFieldSpec> fields;
    bool podStruct = false;    ///< Struct: поля без NSDMI (POD); Class/обёртка — NSDMI из инициализатора
    std::string trailer;       ///< после `};\n` (POD-assert Struct)
    MapperRange typeNameRange; ///< диапазон имени типа (mapping); может быть invalid
};

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
    /// Эмиссия пользовательского Record-типа (Struct/Class): `struct Name [: public Base...] { ... };`.
    /// Struct (Group::kStructs) дополнительно получает static_assert POD.
    void emitRecordDecl(const RecordDecl& rec, TypeId rec_id, MapperFile output_idx, MapperRange typeNameRange);
    /// Эмиссия класса-обёртки нативного класса (под (B)): `class Name : public Base { ... };`.
    /// Обёртка — только когда trust-имя класса ОТЛИЧАЕТСЯ от RHS (`MyStr ::= %std::string`); при
    /// совпадении (`CountDel ::= %CountDel`) класс определён в C++-заголовке (ничего не эмитится).
    void emitNativeClassWrapper(const Binary& typeDecl, const ClassDecl& cls, MapperFile output_idx);
    /// Тела методов типа, ОПУБЛИКОВАННОГО заголовком модуля: заголовок содержит только прототипы,
    /// поэтому out-of-line определения (`Owner::name`) эмитятся в исполняющем артефакте (`.cppt`).
    /// Не вызывается в forward-режиме (эмиссия самого заголовка).
    void emitPublishedRecordMembers(const Binary& typeDecl);
    void mapDeclaredName(MapperFile output_idx, MapperRange trustRange, uint32_t prefixLen, std::string_view name, std::string_view cppName);
    /// Декларация члена Enum/Variant `    static const <memberCpp> c_NAME;` в буфер + name-маппинг
    /// (hover). ЕДИНАЯ точка для обоих: C++-тип и trust-имя — из реестра, диапазон — из AST-элемента.
    void emitStaticConstMember(std::string& out, MapperFile output_idx, std::string_view memberCpp, std::string_view trustName, const DictElement& element);
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
    /// Тело модульного юнита (символы — в их trust-namespace; entry/embed — вне областей).
    void emitModuleBodyIsolated(const ModuleNode& n, MapperFile out);
    void emitModuleImportDecls(const ModuleNode& n);
    /// Имя-зависимые факты (native-адрес, защита стека) экспорт-набора импортируемого модуля —
    /// без эмиссии C++-объявлений: их публикует заголовок, а кодогену клиента нужны эти факты.
    void registerImportedDeclFacts(const std::vector<AstNodePtr>& nodes);
    /// Интерфейсная проекция модуля для артефакта `<enc>.hpp` по ПОДЛИННОМУ экспорт-набору модуля.
    /// Клиент получает её через `#include` заголовка; `emitImportScope` с `m_mode.forwardDeclOnly`
    /// вызывается только внутри этой генерации. Корневой `ModuleNode` разворачивается
    /// (модуль — не namespace-сегмент; автоматического C++ namespace у модуля нет).
    void emitModuleHeaderBody(const std::vector<AstNodePtr>& roots, const std::set<DeclId>& terms, MapperFile out);
    /// Проход по типам/остальным объявлениям заголовка модуля.
    enum class HeaderPass : uint8_t {
        kForwardDecls, ///< forward-декларации классов (сигнатуры могут ссылаться на тип ниже)
        kTypes,        ///< определения типов
        kRest,         ///< прочие объявления (функции/переменные)
    };
    /// Проход по типам (`kTypes`), forward-декларациям (`kForwardDecls`) либо прочим объявлениям.
    void emitImportScope(const std::vector<AstNodePtr>& body, const std::set<DeclId>& terms, MapperFile out, HeaderPass pass);
    /// Forward-декларация класса для типа, чьё определение печатается в этом заголовке: сигнатуры
    /// членов могут ссылаться на тип, определённый НИЖЕ (`StrChar::toWide` → `StrWide`).
    void emitClassForwardDecl(const AstNodeBase& node, MapperFile out);
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
    /// ЕДИНЫЙ генератор C++-класса: каркас (`cppClassFrame`), пролог, обход членов в ПОРЯДКЕ
    /// ОБЪЯВЛЕНИЯ (поля VarDecl + методы FuncDecl) с эмиссией секций доступа `public:`/`protected:`/
    /// `private:` при смене `AstNodeBase::visibility`, POD-assert. Используется и пользовательским
    /// Record (`emitRecordDecl`), и обёрткой/генерируемым builtin-классом (`emitNativeClassWrapper`) —
    /// различия вынесены в `CppClassSpec`, отдельных путей эмиссии класса нет.
    void emitCppClass(const CppClassSpec& spec, MapperFile output_idx);
    /// Секция доступа `public:/protected:/private:` в буфер — печатается при смене уровня.
    void emitAccessSection(std::string& buffer, MapperFile output_idx, Visibility level, Visibility& current) const;
    /// Смещение имени внутри буфера относительно точки mapStart (source-map).
    [[nodiscard]] uint32_t declaredNameOffset(MapperFile output_idx, std::string_view buffer) const;
    /// Слить накопленный буфер в вывод.
    void flushOutput(MapperFile output_idx, std::string& buffer) const;
    /// ЕДИНЫЙ цикл out-of-line тел методов объявления, ОПУБЛИКОВАННОГО заголовком (`.cppt`).
    /// `namespaces` — C++-namespace-сегменты владельца (генерируемый builtin-класс живёт в
    /// native-namespace своего C++-имени); определения открываются внутри них.
    void emitOutOfLineMethods(const std::vector<AstNodePtr>& members, std::string_view ownerCpp, bool recordIsTemplate,
                              const std::vector<std::string>& namespaces = {});
    bool prepareFunctionSignature(const FuncDecl& func_node, bool isMember, std::string_view memberOwner, bool suppressDefaults, Signature& sig);
    void emitFunctionSignature(const FuncDecl& func_node, const Signature& sig, MapperFile output_idx);
    void emitFunctionBody(const FuncDecl& func_node, const Signature& sig, MapperFile output_idx);
    /// ЕДИНЫЙ рендер C++-типа параметра (функции и лямбды): аннотация → Void/None/Any/const/pack.
    /// false — тип невыразим (диагностика уже выдана TypeEmitter).
    bool resolveParamCppType(const ArgNode& param, std::string& cppType) const;
    /// ЕДИНЫЙ рендер C++-типа возврата (функции и лямбды). false — тип невыразим (диагностика выдана).
    bool resolveReturnCppType(const AstNodePtr& typeNode, std::string& cppType) const;
    /// C++-имя явно аннотированного ссылочного типа. false — диагностика уже выдана
    /// (тип не материализован семантикой либо C++-имя невыразимо).
    bool declaredRefCppType(const VarDecl& var_node, QualType declared, std::string& cpp_type);
    /// Pointee ссылочного типа (`ref_base_id` + C++-имя `ref_pointee_cpp`). false — диагностика выдана.
    bool resolveRefPointee(const VarDecl& var_node, QualType refType, std::optional<TypeId>& ref_base_id, std::string& ref_pointee_cpp);

    CppEmitContext& m_ectx;
    CppTranspiler& m_driver;
};

} // namespace trust
