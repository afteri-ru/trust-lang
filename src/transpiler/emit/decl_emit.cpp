#include "transpiler/emit/decl_emit.hpp"
#include "transpiler/emit/decl_emit_common.hpp"
#include "transpiler/transpiler.hpp"
#include "transpiler/emit/emit_common.hpp"
#include "ast/ast_nodes.hpp"
#include "attrs/attr_builtin.hpp"
#include "ast/ref_syntax.hpp"
#include "ast/ident_name.hpp"
#include "ast/kind_dispatch.hpp"
#include "ast/token_type.hpp"
#include "session/context.hpp"
#include "diag/registry.hpp"
#include "diag/base_diags.hpp"
#include "syntax/term/term.h"
#include "types/type_system.hpp"
#include "types/types.hpp"
#include "types/runtime_symbols.hpp"
#include "types/intrinsics.hpp"
#include "types/type_id.hpp"
#include "types/names.hpp"
#include "transpiler/emit/diag.hpp"
#include "utils/strings.hpp"
#include <cstdint>
#include <format>
#include <limits>
#include <memory>
#include <optional>
#include <vector>

namespace trust {

bool bodyMustBeInModuleHeader(const FuncDecl& func, const AttrPool& attrs) {
    return methodBodyMustBeInHeader(func, /*recordIsTemplate=*/false, attrs);
}

// Объявления.
void DeclEmitter::visit_VarDecl(const VarDecl& n) {
    // Заголовок модуля публикует только ОБЪЯВЛЕНИЕ (`extern`), поэтому определение переменной
    // эмитится здесь всегда — `isPublishedByHeader` касается типов и generic/constexpr функций.
    generateVarDeclToFile(n, m_ectx.m_output.out);
}

void DeclEmitter::visit_FuncDecl(const FuncDecl& n) {
    // Лямбда-выражение в позиции значения: специализированный эмиттер (НЕ top-level определение).
    if (n.isLambda()) {
        emitLambdaExpr(n);
        return;
    }
    // Определение уже эмитировано заголовком модуля (generic/constexpr) — повтор дал бы redefinition.
    if (!m_ectx.m_mode.forwardDeclOnly && m_ectx.m_declFacts.isPublishedByHeader(&n)) {
        return;
    }
    // В заголовке тело generic/constexpr free-функции обязательно; прочие тела выносит `.cppt`.
    if (m_ectx.m_mode.forwardDeclOnly && bodyMustBeInModuleHeader(n, m_ectx.m_ctx.attrs())) {
        if (bodyUsesNonPublishedEntity(n, m_ectx.m_declFacts.headerPublishedDecls, m_ectx.m_output.currentModule)) {
            m_ectx.m_ctx.report(n.range(), diag::DiagId::ParseError,
                                "function '{}' references a module-level entity that the module header does not publish "
                                "(e.g. a '__::' helper): a template or constexpr function must be defined in the header; "
                                "move the helper out of '__::' or drop the template",
                                n.text());
            return;
        }
        BodyEmitInHeaderGuard guard(m_ectx);
        generateFuncDeclToFile(n, m_ectx.m_output.out);
        return;
    }
    generateFuncDeclToFile(n, m_ectx.m_output.out);
}

// Binary: TypeDecl → объявление типа; прочие → expression statement.
void DeclEmitter::visit_TypeDecl(const Binary& n) {
    // Тип уже определён в заголовке модуля (TU начинается с его `#include`) — повтор дал бы
    // redefinition. Касается и объявлений внутри блока интерфейса (он эмитится как контейнер).
    // Тела методов, вынесенные из заголовка, определяются здесь (исполняющий артефакт модуля).
    if (m_ectx.m_declFacts.isPublishedByHeader(&n)) {
        emitPublishedRecordMembers(n);
        return;
    }
    generateTypeDeclToFile(n, m_ectx.m_output.out);
}

// Бинарные statement/expression kinds - единая генерация (m_mode.exprDepth различает контекст).
void DeclEmitter::visit_NameDecl(const Binary& n) {
    m_driver.m_expr.emitBinaryStmtOrExpr(n);
}

void DeclEmitter::visit_EnumDecl(const Sequence& node) {
    // Enum-объявление всегда представлено TypeDecl; самостоятельный EnumDecl не производится.
    faultUnexpectedKind("DeclEmitter", node);
}

void DeclEmitter::visit_EnumMember(const Sequence& node) {
    // Enum строится из TypeDecl; самостоятельный EnumMember не производится.
    faultUnexpectedKind("DeclEmitter", node);
}

void DeclEmitter::visit_StructDecl(const RecordDecl& node) {
    // Struct/Class эмитится только как RHS TypeDecl (generateTypeDeclToFile → emitRecordDecl).
    faultUnexpectedKind("DeclEmitter", node);
}

void DeclEmitter::visit_ClassDecl(const ClassDecl& node) {
    // Forward-объявление нативного класса эмитится только как RHS TypeDecl; самостоятельный
    // ClassDecl не производится (C++-имя - из resolveCppTypeId, инклуд - on-use).
    faultUnexpectedKind("DeclEmitter", node);
}

void DeclEmitter::visit_StructField(const Sequence& node) {
    // Поля эмитятся внутри emitRecordDecl; самостоятельный StructField не производится.
    faultUnexpectedKind("DeclEmitter", node);
}
} // namespace trust
