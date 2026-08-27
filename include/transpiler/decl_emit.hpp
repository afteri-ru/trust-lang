#pragma once

// include/transpiler/decl_emit.hpp
// Компонент кодогенерации: DeclEmitter. Разделяет CppEmitContext с драйвером CppTranspiler;
// рекурсия/вызовы других компонентов идут через драйвер (friend).

#include "transpiler/emit_ctx.hpp"
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

class DeclEmitter {
  public:
    explicit DeclEmitter(CppEmitContext& ectx, CppTranspiler& driver)
    : m_ectx(ectx)
    , m_driver(driver) {}
    void generateVarDeclToFile(const VarDecl& var_node, MapperFile output_idx);
    void generateTypeDeclToFile(const Binary& binary_node, MapperFile output_idx);
    void emitEnumStruct(std::string_view enum_trust, const DictLiteralNode& dict, TypeId enum_id, MapperFile output_idx, MapperRange typeNameRange);
    void emitVariantStruct(std::string_view variant_trust, const DictLiteralNode& dict, TypeId variant_id, MapperFile output_idx, MapperRange typeNameRange);
    void mapDeclaredName(MapperFile output_idx, MapperRange trustRange, uint32_t prefixLen, std::string_view name, std::string_view cppName);
    void generateFuncDeclToFile(const FuncDecl& func_node, MapperFile output_idx);
    void visit_ModuleDecl(const ModuleNode& n);
    void emitModuleImportDecls(const ModuleNode& n);
    void emitImportScope(const std::vector<AstNodePtr>& body, const std::set<const Term*>& terms, MapperFile out);
    std::string buildTrustForwardDecl(const AstNodeBase& node) const;
    void visit_VarDecl(const VarDecl& n);
    void visit_FuncDecl(const FuncDecl& n);
    void visit_TypeDecl(const Binary& n);
    void visit_NameDecl(const Binary& n);
    void visit_EnumDecl(const Sequence&);
    void visit_EnumMember(const Sequence&);
    void visit_StructDecl(const Sequence&);
    void visit_StructField(const Sequence&);

  private:
    CppEmitContext& m_ectx;
    CppTranspiler& m_driver;
};

} // namespace trust
