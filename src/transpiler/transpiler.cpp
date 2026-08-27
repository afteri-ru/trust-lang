// Generated: src/transpiler/transpiler.cpp (driver)
#include "transpiler/transpiler.hpp"
#include "transpiler/emit_common.hpp"
#include "ast/ast_nodes.hpp"
#include "ast/attr_builtin.hpp"
#include "ast/ident_name.hpp"
#include "ast/kind_visitor.hpp"
#include "ast/token_type.hpp"
#include "diag/context.hpp"
#include "diag/registry.hpp"
#include "diag/base_diags.hpp"
#include "semantic/symbol_table.hpp"
#include "semantic/solver.hpp"
#include "syntax/term.h"
#include "types/registry.hpp"
#include "types/runtime_symbols.hpp"
#include "types/intrinsics.hpp"
#include "types/type_id.hpp"
#include "types/type_names.hpp"
#include "transpiler/diag.hpp"
#include "utils/operators.hpp"
#include "utils/strings.hpp"
#include <format>
#include <memory>

namespace trust {

// -- Пер-компонентная регистрация опций кодогенерации (см. diag/registry.hpp) --
// Регистрирует на static-init feature-флаги, которыми владеет компонент transpiler
// (Comments/Assert/Backtrace), включая их дефолты (включены). ParseError - базовая
// диагностика, регистрируется в diag/applyRegisteredDiagnostics.
namespace {
struct TranspilerDiagnosticsRegistrar {
    TranspilerDiagnosticsRegistrar() {
        registerDiagnostics([](Options& opts) {
            opts.add_flag(transpiler::FlagKind::Comments);
            opts.add_flag(transpiler::FlagKind::Assert);
            opts.add_flag(transpiler::FlagKind::Backtrace);
            // Проверки `assert`/`verify` включены по умолчанию (безопасность по умолчанию);
            // отключаются через `-Wno-assert`.
            opts.set_enabled(transpiler::FlagKind::Assert, true);
            // Стек вызовов при провале assert/verify печатается ПО УМОЛЧАНИЮ; отключение - -Wno-backtrace.
            opts.set_enabled(transpiler::FlagKind::Backtrace, true);
            // Комментарии в C++-выводе выводятся по умолчанию; подавление - через -Wno-comments
            // (флаг «comments» выключен = подавлять). См. TRANSPILER_FLAG_LIST(Comments).
            opts.set_enabled(transpiler::FlagKind::Comments, true);
        });
    }
};
const TranspilerDiagnosticsRegistrar kTranspilerDiagnostics;
} // namespace

CppTranspiler::CppTranspiler(Context& ctx, const SymbolTable* resolvedTypes)
: m_ectx(ctx, resolvedTypes)
, m_type(m_ectx, *this)
, m_decl(m_ectx, *this)
, m_stmt(m_ectx, *this)
, m_expr(m_ectx, *this)
, m_contract(m_ectx, *this) {
}

bool CppTranspiler::isSuppressedDoc(ParserToken::Kind k) const {
    // Флаг «comments» включён = комментарии ВЫВОДЯТСЯ; подавление - флаг выключен (-Wno-comments).
    return k == ParserToken::Kind::Document && !m_ectx.m_ctx.opts().is_enabled(transpiler::FlagKind::Comments);
}

// Эмитит документирующий комментарий, привязанный к узлу объявления (AstNodeBase::documentation).
// Строками с текущим отступом; `##`/`##<` нормализуются в C++-валидные `///`/`///<` (как visit_Document).
// Подавляется флагом -Wno-comments (Comments выключен = не выводить), как и sibling-Document.
void CppTranspiler::emitDocumentation(const AstNodeBase& node, MapperFile output_idx) {
    if (node.documentation.empty() || !m_ectx.m_ctx.opts().is_enabled(transpiler::FlagKind::Comments)) {
        return;
    }
    size_t pos = 0;
    const std::string_view all = node.documentation;
    while (pos <= all.size()) {
        const size_t nl = all.find('\n', pos);
        const std::string_view line = (nl == std::string_view::npos) ? all.substr(pos) : all.substr(pos, nl - pos);
        m_ectx.m_ctx.source().output_append(output_idx, m_ectx.indentPrefix());
        if (line.starts_with("##")) {
            m_ectx.m_ctx.source().output_append(output_idx, "///");
            m_ectx.m_ctx.source().output_append(output_idx, line.substr(2));
        } else {
            m_ectx.m_ctx.source().output_append(output_idx, line);
        }
        m_ectx.m_ctx.source().output_append(output_idx, "\n");
        if (nl == std::string_view::npos) {
            break;
        }
        pos = nl + 1;
    }
}

void CppTranspiler::generateToFile(const std::vector<AstNodePtr>& ast_nodes, MapperFile output_idx) {
    const AstNodeBase* prev = nullptr;
    for (const auto& node : ast_nodes) {
        if (!node) {
            continue;
        }
        if (isSuppressedDoc(node->kind())) {
            continue;
        }
        emitBlockSeparator(prev, *node, output_idx);
        generateNodeToFile(*node, output_idx);
        prev = node.get();
    }
    if (!ast_nodes.empty()) {
        m_ectx.m_ctx.source().output_append(output_idx, "\n");
    }
    // МЕХАНИЗМ №1 - ПО ТИПУ: только ПОСЛЕ полного обхода AST формируем инклуды из собранных
    // типов (m_ectx.m_usedTypes), затем препендим все директивы (emitCollectedIncludes).
    m_type.collectTypeIncludes();
    m_type.emitCollectedIncludes(output_idx);
}

void CppTranspiler::emitBlockSeparator(const AstNodeBase* prev, const AstNodeBase& node, MapperFile output_idx) {
    // Перевод строки между блоками вставляется только если они в исходнике на разных
    // строках: строка конца prev != строка начала node. Если строки совпадают (блоки
    // намеренно на одной строке) - перевод строки не выводится, вместо него пробел.
    if (!prev) {
        return; // первый блок - перевод строки не нужен
    }
    const bool sameSourceLine = m_ectx.m_ctx.source().line(prev->range().end) == m_ectx.m_ctx.source().line(node.range().begin);
    if (!sameSourceLine) {
        m_ectx.m_ctx.source().output_append(output_idx, "\n");
    } else {
        emitSameLineSpace(node.text(), output_idx);
    }
}

void CppTranspiler::emitSameLineSpace(std::string_view nextText, MapperFile output_idx) {
    // Для читаемости между блоками на одной строке ставим пробел, но не дублируем его,
    // если на границе уже есть пробельный символ (например, EMBED-содержимое с ведущими/
    // хвостовыми пробелами).
    const std::string_view body = m_ectx.m_ctx.source().output_body(output_idx);
    const bool prevEndsWithSpace = !body.empty() && (body.back() == ' ' || body.back() == '\t');
    const bool nextStartsWithSpace = !nextText.empty() && (nextText.front() == ' ' || nextText.front() == '\t');
    if (!prevEndsWithSpace && !nextStartsWithSpace) {
        m_ectx.m_ctx.source().output_append(output_idx, " ");
    }
}

void CppTranspiler::emitSequenceBody(const Sequence& node, MapperFile output_idx) {
    // Sequence, ScopeBlock or ModuleNode → walk body. Метки именованных блоков и goto
    // вставляются анализатором (lowering) как отдельные узлы LabelStmt/GotoStmt - здесь
    // только кодогенерация: каждый узел m_body эмитится по своему kind.

    const bool inBlock = m_ectx.indentLevel() > 0;
    const AstNodeBase* prev = nullptr;
    for (const auto& child : node.m_body) {
        if (!child) {
            continue;
        }
        if (isSuppressedDoc(child->kind())) {
            continue;
        }
        if (inBlock) {
            // Внутри блока - каждый оператор с новой строки с отступом (нормальное форматирование).
            // Для блочных детей (ScopeBlock/sequence/ModuleDecl) отступ выставляет их собственный обход.
            // Перевод строки добавляется ПОСЛЕ каждого реально выведенного узла (а не до),
            // чтобы подавленные доки/пустые bundle'ы не оставляли «сиротских» пустых строк.
            const size_t before = m_ectx.m_ctx.source().output_body(output_idx).size();
            // Документирующий комментарий объявления (из term->m_docs) - строками с отступом.
            emitDocumentation(*child, output_idx);
            if (!is_block_kind(child->kind())) {
                m_ectx.m_ctx.source().output_append(output_idx, m_ectx.indentPrefix());
            }
            generateNodeToFile(*child, output_idx);
            if (m_ectx.m_ctx.source().output_body(output_idx).size() != before) {
                const std::string_view body = m_ectx.m_ctx.source().output_body(output_idx);
                if (body.empty() || body.back() != '\n') {
                    m_ectx.m_ctx.source().output_append(output_idx, "\n");
                }
            }
        } else {
            // На верхнем уровне (indent==0) - прежнее поведение (зеркалирование строк исходника).
            emitBlockSeparator(prev, *child, output_idx);
            // Документирующий комментарий объявления (из term->m_docs) - строками без отступа.
            emitDocumentation(*child, output_idx);
            generateNodeToFile(*child, output_idx);
            prev = child.get();
        }
    }
}

// Placeholder для нереализованных expression-only kinds: "{}" только в expression-контексте.
void CppTranspiler::emitPlaceholderExpr(MapperFile output_idx) {
    if (m_ectx.m_exprDepth > 0) {
        m_ectx.m_ctx.source().output_append(output_idx, "{}");
    }
}

// Унифицированный вывод текста как вложенного выражения (только при m_exprDepth>0).
void CppTranspiler::emitExprText(std::string_view text) {
    if (m_ectx.m_exprDepth > 0) {
        m_ectx.m_ctx.source().output_append(m_ectx.m_out, text);
    }
}

void CppTranspiler::generateNodeToFile(const AstNodeBase& node, MapperFile output_idx) {
    if (node.kind() == ParserToken::Kind::END) {
        return;
    }

    // Единая диспетчеризация ПО KIND (см. ast/kind_visitor.hpp): класс - наследник абстрактного
    // KindVisitor, поэтому dispatchKind вызывает соответствующий visit_<Kind> прямо на *this.
    // Каждый kind реализован (или явный no-op): пропуск генерации не даст скомпилироваться.
    m_ectx.m_out = output_idx;
    dispatchKind(node, *this);
}

void CppTranspiler::emitExpr(const AstNodeBase* node) {
    if (!node) {
        m_ectx.m_ctx.source().output_append(m_ectx.m_out, "{}");
        return;
    }
    // Единая диспетчеризация ПО KIND (ast/kind_visitor.hpp) - как и для statement.
    // Различие statement/expression - глубина m_ectx.m_exprDepth: visit_<Kind> добавляет
    // mapStart/mapStop и ';' только на верхнем уровне (m_ectx.m_exprDepth==0); вложенный
    // вызов emitExpr (m_ectx.m_exprDepth>0) - только текст выражения (без ';' и map).
    ++m_ectx.m_exprDepth;
    dispatchKind(*node, *this);
    --m_ectx.m_exprDepth;
}

void CppTranspiler::emitBodyNode(const AstNodePtr& body, MapperFile output_idx, bool mapBlock, const std::string& beforeCloseLabel,
                                 const std::string& afterOpen) {
    std::vector<AstNodePtr> stmts;
    MapperRange blockRange;
    collectBodyStatements(body, stmts, blockRange);
    m_stmt.emitBlockBodyToFile(stmts, blockRange, output_idx, mapBlock, beforeCloseLabel, afterOpen);
}

// -- KindVisitor: visit_<Kind> (statement-контекст, потоковый вывод в m_out) --

// Блоки-обёртки: обход тела; Attr - не обход.
void CppTranspiler::visit_sequence(const Sequence& n) {
    emitSequenceBody(n, m_ectx.m_out);
}

// Return/Throw inline; Break/Continue → goto.
void CppTranspiler::emitJumpValue(std::string_view keyword, const JumpStmt& n) {
    // Синтетические jump-узлы lowering (void-return по имени функции) не имеют Term →
    // range невалиден → маппинг пропускается (нет исходного trust-текста).
    const MapperRange r = n.range();
    std::unique_ptr<MapperScope> scope;
    if (!r.isInvalid()) {
        scope = std::make_unique<MapperScope>(m_ectx.m_ctx.source(), r, m_ectx.m_out);
    }
    if (n.m_value) {
        m_ectx.m_ctx.source().output_append(m_ectx.m_out, keyword);
        m_ectx.m_ctx.source().output_append(m_ectx.m_out, " ");
        emitExpr(n.m_value.get());
        m_ectx.m_ctx.source().output_append(m_ectx.m_out, ";");
    } else {
        m_ectx.m_ctx.source().output_append(m_ectx.m_out, keyword);
        m_ectx.m_ctx.source().output_append(m_ectx.m_out, ";");
    }
}

// AstNodeAttr-kind'ы, не генерируемые как statement → no-op.
void CppTranspiler::visit_Program(const AstNodeAttr&) {
}

// Kind=Unimplemented: конвертер Term→Ast (convertForKind<Unimplemented>) не строит узел и выдаёт
// ошибку, поэтому такой узел в AST не появляется. Метод - no-op (требуется строгим контрактом KindVisitor).
void CppTranspiler::visit_Unimplemented(const AstNodeAttr&) {
}

// Kind=NotApplicable: узел никогда не строится (convertForKind<NotApplicable> → Fatal), в AST не
// появляется. Метод - no-op (требуется строгим контрактом KindVisitor).
void CppTranspiler::visit_NotApplicable(const AstNodeAttr&) {
}

void CppTranspiler::visit_ModuleDecl(const ModuleNode& n) {
    m_decl.visit_ModuleDecl(n);
}

void CppTranspiler::visit_VarDecl(const VarDecl& n) {
    m_decl.visit_VarDecl(n);
}

void CppTranspiler::visit_FuncDecl(const FuncDecl& n) {
    m_decl.visit_FuncDecl(n);
}

void CppTranspiler::visit_TypeDecl(const Binary& n) {
    m_decl.visit_TypeDecl(n);
}

void CppTranspiler::visit_NameDecl(const Binary& n) {
    m_decl.visit_NameDecl(n);
}

void CppTranspiler::visit_EnumDecl(const Sequence& n) {
    m_decl.visit_EnumDecl(n);
}

void CppTranspiler::visit_EnumMember(const Sequence& n) {
    m_decl.visit_EnumMember(n);
}

void CppTranspiler::visit_StructDecl(const Sequence& n) {
    m_decl.visit_StructDecl(n);
}

void CppTranspiler::visit_StructField(const Sequence& n) {
    m_decl.visit_StructField(n);
}

void CppTranspiler::visit_ScopeBlock(const ScopeBlock& n) {
    m_stmt.visit_ScopeBlock(n);
}

void CppTranspiler::visit_DestructureDecl(const DestructureDecl& n) {
    m_stmt.visit_DestructureDecl(n);
}

void CppTranspiler::visit_ReturnStmt(const JumpStmt& n) {
    m_stmt.visit_ReturnStmt(n);
}

void CppTranspiler::visit_ThrowStmt(const JumpStmt& n) {
    m_stmt.visit_ThrowStmt(n);
}

void CppTranspiler::visit_BreakStmt(const JumpStmt& n) {
    m_stmt.visit_BreakStmt(n);
}

void CppTranspiler::visit_ContinueStmt(const JumpStmt& n) {
    m_stmt.visit_ContinueStmt(n);
}

void CppTranspiler::visit_GotoStmt(const LabelRef& n) {
    m_stmt.visit_GotoStmt(n);
}

void CppTranspiler::visit_LabelStmt(const LabelRef& n) {
    m_stmt.visit_LabelStmt(n);
}

void CppTranspiler::visit_SemicolonStmt(const SemicolonStmt& n) {
    m_stmt.visit_SemicolonStmt(n);
}

void CppTranspiler::visit_IfStmt(const IfStmt& n) {
    m_stmt.visit_IfStmt(n);
}

void CppTranspiler::visit_WhileStmt(const WhileStmt& n) {
    m_stmt.visit_WhileStmt(n);
}

void CppTranspiler::visit_DoWhileStmt(const DoWhileStmt& n) {
    m_stmt.visit_DoWhileStmt(n);
}

void CppTranspiler::visit_MatchingStmt(const MatchStmt& n) {
    m_stmt.visit_MatchingStmt(n);
}

void CppTranspiler::visit_AssignmentStmt(const AstNodeAttr& n) {
    m_stmt.visit_AssignmentStmt(n);
}

void CppTranspiler::visit_BlockStmt(const AstNodeAttr& n) {
    m_stmt.visit_BlockStmt(n);
}

void CppTranspiler::visit_ThenBlock(const AstNodeAttr& n) {
    m_stmt.visit_ThenBlock(n);
}

void CppTranspiler::visit_ElseBlock(const AstNodeAttr& n) {
    m_stmt.visit_ElseBlock(n);
}

void CppTranspiler::visit_WhileElseBlock(const AstNodeAttr& n) {
    m_stmt.visit_WhileElseBlock(n);
}

void CppTranspiler::visit_TryCatchStmt(const Sequence& n) {
    m_stmt.visit_TryCatchStmt(n);
}

void CppTranspiler::visit_CatchBlock(const Sequence& n) {
    m_stmt.visit_CatchBlock(n);
}

void CppTranspiler::visit_MatchingCase(const AstNodeAttr& n) {
    m_stmt.visit_MatchingCase(n);
}

void CppTranspiler::visit_MatchingElseBlock(const AstNodeAttr& n) {
    m_stmt.visit_MatchingElseBlock(n);
}

void CppTranspiler::visit_Attr(const Sequence& n) {
    m_expr.visit_Attr(n);
}

void CppTranspiler::visit_ArgNode(const ArgNode& n) {
    m_expr.visit_ArgNode(n);
}

void CppTranspiler::visit_AssignOp(const Binary& n) {
    m_expr.visit_AssignOp(n);
}

void CppTranspiler::visit_AppendStmt(const Binary& n) {
    m_expr.visit_AppendStmt(n);
}

void CppTranspiler::visit_MathOp(const Binary& n) {
    m_expr.visit_MathOp(n);
}

void CppTranspiler::visit_BitwiseOp(const Binary& n) {
    m_expr.visit_BitwiseOp(n);
}

void CppTranspiler::visit_CompareOp(const Binary& n) {
    m_expr.visit_CompareOp(n);
}

void CppTranspiler::visit_LogicalOp(const Binary& n) {
    m_expr.visit_LogicalOp(n);
}

void CppTranspiler::visit_MemberAccess(const Binary& n) {
    m_expr.visit_MemberAccess(n);
}

void CppTranspiler::visit_ArrayAccess(const Binary& n) {
    m_expr.visit_ArrayAccess(n);
}

void CppTranspiler::visit_IntLiteral(const Literal& n) {
    m_expr.visit_IntLiteral(n);
}

void CppTranspiler::visit_RationalLiteral(const Literal& n) {
    m_expr.visit_RationalLiteral(n);
}

void CppTranspiler::visit_StrChar(const Literal& n) {
    m_expr.visit_StrChar(n);
}

void CppTranspiler::visit_StrWide(const Literal& n) {
    m_expr.visit_StrWide(n);
}

void CppTranspiler::visit_FloatLiteral(const Literal& n) {
    m_expr.visit_FloatLiteral(n);
}

void CppTranspiler::visit_ContextMacro(const ContextMacro& n) {
    m_expr.visit_ContextMacro(n);
}

void CppTranspiler::visit_EmbedExpr(const AstNodeAttr& n) {
    m_expr.visit_EmbedExpr(n);
}

void CppTranspiler::visit_Document(const AstNodeAttr& n) {
    m_expr.visit_Document(n);
}

void CppTranspiler::visit_Ident(const IdentName& n) {
    m_expr.visit_Ident(n);
}

void CppTranspiler::visit_TypeName(const IdentType& n) {
    m_expr.visit_TypeName(n);
}

void CppTranspiler::visit_CallExpr(const CallExpr& n) {
    m_expr.visit_CallExpr(n);
}

void CppTranspiler::visit_VarRef(const AstNodeAttr& n) {
    m_expr.visit_VarRef(n);
}

void CppTranspiler::visit_ArrayInit(const DictLiteralNode& n) {
    m_expr.visit_ArrayInit(n);
}

void CppTranspiler::visit_DictLiteral(const DictLiteralNode& n) {
    m_expr.visit_DictLiteral(n);
}

void CppTranspiler::visit_Tuple(const DictLiteralNode& n) {
    m_expr.visit_Tuple(n);
}

void CppTranspiler::visit_RangeExpr(const RangeExpr& n) {
    m_expr.visit_RangeExpr(n);
}

void CppTranspiler::visit_RefMakeExpr(const Sequence& n) {
    m_expr.visit_RefMakeExpr(n);
}

void CppTranspiler::visit_RefTakeExpr(const Sequence& n) {
    m_expr.visit_RefTakeExpr(n);
}

void CppTranspiler::visit_Ellipsis(const Sequence& n) {
    m_expr.visit_Ellipsis(n);
}

void CppTranspiler::visit_TrustContract(const TrustContract& n) {
    m_contract.visit_TrustContract(n);
}

void CppTranspiler::visit_TrustElem(const TrustElem& n) {
    m_contract.visit_TrustElem(n);
}

} // namespace trust
