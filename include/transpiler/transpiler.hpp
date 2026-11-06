#pragma once

#include "analysis/modes.hpp"
#include "ast/ast_nodes.hpp"
#include "ast/kind_dispatch.hpp"
#include "diag/options.hpp"
#include "ir/hir_module.hpp"
#include "location/decl_id.hpp"
#include "location/location.hpp"
#include "transpiler/emit/emit_ctx.hpp"
#include "transpiler/type/type_emit.hpp"
#include "transpiler/emit/decl_emit.hpp"
#include "transpiler/emit/stmt_emit.hpp"
#include "transpiler/emit/expr_emit.hpp"
#include "transpiler/emit/contract_emit.hpp"
#include "target/runtime_symbols.hpp"
#include "types/type_id.hpp"
#include <cstdint>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace trust {

/// Поведенческие режимы кодогена (владелец флагов — `transpiler`). ЕДИНСТВЕННАЯ точка разрешения
/// `-foverflow-check`/`-fcomments`/`-Wbacktrace` из `Options`: потребители (кодоген, стадии
/// бэкенда) получают значения ДАННЫМИ (R39), а не читают `diag::Options` сами.
[[nodiscard]] analysis::BehavioralModes codegenModes(analysis::BehavioralModes modes, const Options& opts);

/// CppTranspiler - генератор C++ кода из AST (exhaustive consumer, см. ast/kind_dispatch.hpp).
/// Вся тяжёлая кодогенерация вынесена в компоненты-эмиттеры (TypeEmitter/DeclEmitter/
/// StmtEmitter/ExprEmitter/ContractEmitter), разделяющие общий CppEmitContext (m_ectx).
/// Здесь - публичный API, обход/диспетчеризация (visit_<Kind> делегируют эмиттерам) и
/// рекурсивные точки emitExpr/emitSequenceBody. Полнота обработчиков доказывается
/// `static_assert(hasAllKindHandlers<CppTranspiler>())` (transpiler.cpp): новый kind без
/// решения не компилируется. `dispatchEmit` получает `KindCategory`: statement-only kind в
/// `emitExpr` отвергается, expression-kind допустим как standalone-выражение.
/// «Недостижимо здесь» - явный `faultUnexpectedKind`, а не пустое тело;
/// «осознанно пропустить» - явный `skipNode`. Эмиттеры - друзья: им нужен доступ к
/// приватной рекурсии (emitExpr) и друг к другу (через ссылки на эмиттеры m_type/...).
class CppTranspiler {
  public:
    explicit CppTranspiler(CodegenServices services, analysis::BehavioralModes behavioral = {});

    /// Генерация C++ кода непосредственно в выходной файл с построением source map.
    /// Вход — HIR (понижённая форма): AST напрямую в кодоген не передаётся.
    /// Для каждого узла HIR создаётся маппинг trust-range → cpp-range через mapStart/mapStop.
    void generateToFile(const HirModule& hir, MapperFile output_idx);

    /// Генерация заголовка модуля (`<enc>.hpp`): `#pragma once`, `#include` заголовков
    /// РЕЭКСПОРТИРУЕМЫХ модулей (umbrella) и экспорт-набор БЕЗ автоматического namespace модуля
    /// (прототипы функций, extern-переменные, определения экспортных типов).
    /// `exports` — объявления экспорт-набора; сопоставление с HIR по `DeclId` (R23, клон сохраняет id);
    /// `moduleIncludes` — имена `.hpp` реэкспортируемых модулей.
    /// Source-map не ведётся: канонические позиции объявлений принадлежат `.cppt` модуля.
    void generateHeaderToFile(const HirModule& hir, MapperFile output_idx, ModuleIndex module, const std::vector<AstNodePtr>& exports,
                              const std::vector<std::string>& moduleIncludes, const std::set<DeclId>& publishedDecls, std::string ownHeaderName);

    /// Настройка кодогенерации единицы трансляции модуля: `#include "<enc>.hpp"` первой директивой
    /// и набор узлов (AST), которые заголовок уже опубликовал (их не эмитим повторно — иначе
    /// redefinition в том же TU). Вызывается из Pipeline перед `generateToFile`.
    void configureModuleUnit(std::string include, const std::vector<const AstNodeBase*>& published);

    /// Пути рантайм-заголовков (например "stdlib/builtin/rational.hpp"), которые реально
    /// понадобились сгенерированному коду (маркер '@' в preprocInclude типа).
    const std::set<std::string>& packageHeaders() const noexcept { return m_ectx.m_accumulators.packageHeaders; }

    /// Флаги линковки нативных библиотек (`-l<имя>`) из атрибутов `@[link("имя")]`.
    const std::set<std::string>& linkLibs() const noexcept { return m_ectx.m_accumulators.linkLibs; }

    /// C++-типы параметров entry-функции (`<модуль>__main__`, без имён, через \", \").
    /// Пусто, если entry без параметров. Pipeline использует их для генерации совпадающего
    /// extern в `_main.cppt`.
    const std::string& entryParams() const noexcept { return m_ectx.m_unit.entryParams; }

    /// True, если в ходе generateToFile эмитилась entry-функция (`<модуль>__main__`).
    /// Pipeline использует это, чтобы отличить программу с точкой входа от библиотеки/скрипта
    /// без `__main__` (например, в однофайловом режиме генерации `-fsingle-file`).
    bool sawEntry() const noexcept { return m_ectx.m_unit.sawEntry; }

    /// Включает однофайловый режим (`-fsingle-file`) для кодогенерации корневого модуля и задаёт
    /// имя синтезируемого implicit-entry (`<модуль>__main__`), которое ожидает pipeline-обёртка.
    /// Вызывается из Pipeline перед generateToFile; для модульных единиц (модульный `.cppt`) и
    /// генерации заголовка (`generateHeaderToFile`) не вызывается.
    void configureSingleFile(bool on, std::string entryName) {
        m_ectx.m_unit.singleFileMode = on;
        m_ectx.m_unit.singleFileEntryName = std::move(entryName);
    }

  private:
    /// Единая диспетчеризация по kind: dispatchEmit(node, *this) - см. ast/kind_dispatch.hpp.
    void generateNodeToFile(const AstNodeBase& node, MapperFile output_idx);

    /// Вывод выражения в поток source() на текущую позицию (без statement-терминатора).
    void emitExpr(const AstNodeBase* node);

    /// Эмитит одного ребёнка блока: док-комментарий, отступ (кроме блочных узлов),
    /// узел и завершающий перевод строки. Единая точка для тела последовательности
    /// и синтезируемого script-entry в single-file режиме.
    void emitBlockChild(const AstNodeBase& child, MapperFile output_idx);

    /// Рендер выражения в строку (для default-аргументов в сигнатуре). Использует временный
    /// output, который удаляется сразу после рендера; source-map не меняется.
    [[nodiscard]] std::string renderExprToString(const AstNodeBase* node);

    /// Генерация тела блока (Sequence/ScopeBlock/ModuleNode): обход m_body.
    void emitSequenceBody(const Sequence& node, MapperFile output_idx);

    /// Однофайловый режим (`-fsingle-file`) для корневого модуля БЕЗ entry-функции (модуль-скрипт):
    /// module-level определения (функции/типы/scope) эмитятся на namespace, top-level операторы -
    /// телом синтезированной entry-функции `m_unit.singleFileEntryName` (python-подобно).
    void emitSingleFileModuleBody(const ModuleNode& node, MapperFile output_idx);

    /// Выводит "{}" (placeholder) только в expression-контексте (m_mode.exprDepth>0).
    void emitPlaceholderExpr(MapperFile output_idx);

    /// Унифицированный вывод текста как вложенного выражения (только при m_mode.exprDepth>0).
    void emitExprText(std::string_view text);

    /// Унифицированная генерация return/throw: `keyword` = "return"/"throw".
    void emitJumpValue(std::string_view keyword, const JumpStmt& node);

    /// Выводит перевод строки между последовательными блоками, если строки в исходнике различаются.
    void emitBlockSeparator(const AstNodeBase* prev, const AstNodeBase& node, MapperFile output_idx);

    /// Для блоков на одной строке исходника: вставляет пробел между ними.
    void emitSameLineSpace(std::string_view nextText, MapperFile output_idx);

    /// Эмиссия тела блока `{ ... }`: body - тело (ScopeBlock/Sequence → m_body, иначе одиночный
    /// statement). Дефолты позволяют управляющим конструкциям вызывать с 2/3/5 аргументами.
    void emitBodyNode(const AstNodePtr& body, MapperFile output_idx, bool mapBlock = false, const std::string& beforeCloseLabel = "",
                      const std::string& afterOpen = "");

    /// True, если документирующие комментарии подавлены (флаг -fno-comments).
    [[nodiscard]] bool isSuppressedDoc(ParserToken::Kind k) const;

    /// Эмитит документирующий комментарий, привязанный к узлу объявления.
    void emitDocumentation(const AstNodeBase& node, MapperFile output_idx);

    // Kind-dispatch: каждый kind имеет visit_<Kind>; полнота набора проверяется
    // static_assert(hasAllKindHandlers<CppTranspiler>()).
    void visit_sequence(const Sequence& node);
    void visit_Attr(const Sequence& node);
    void visit_ScopeBlock(const ScopeBlock& node);
    void visit_TypeDecl(const Binary& node);
    void visit_NameDecl(const Binary& node);
    void visit_AssignOp(const Binary& node);
    void visit_AppendStmt(const Binary& node);
    void visit_MathOp(const Binary& node);
    void visit_IncDecOp(const Binary& node);
    void visit_BitwiseOp(const Binary& node);
    void visit_CompareOp(const Binary& node);
    void visit_LogicalOp(const Binary& node);
    void visit_MemberAccess(const Binary& node);
    void visit_ArrayAccess(const Binary& node);
    void visit_TypeSet(const Sequence& node);
    void visit_Ident(const IdentName& node);
    void visit_TypeName(const IdentType& node);
    void visit_CallExpr(const CallExpr& node);
    void visit_ReturnStmt(const JumpStmt& node);
    void visit_ThrowStmt(const JumpStmt& node);
    void visit_Program(const AstNodeAttr& node);
    void visit_VarRef(const AstNodeAttr& node);
    void visit_EmbedExpr(const RawText& node);
    void visit_Document(const RawText& node);
    void visit_IntLiteral(const Literal& node);
    void visit_FloatLiteral(const Literal& node);
    void visit_StrChar(const Literal& node);
    void visit_StrWide(const Literal& node);
    void visit_RationalLiteral(const Literal& node);
    void visit_ArrayInit(const DictLiteralNode& node);
    void visit_DictLiteral(const DictLiteralNode& node);
    void visit_Tuple(const DictLiteralNode& node);
    void visit_RangeExpr(const RangeExpr& node);
    void visit_RefMakeExpr(const RefMakeExpr& node);
    void visit_RefTakeExpr(const RefTakeExpr& node);
    void visit_RefLockExpr(const Sequence& node);
    void visit_RefLockDeref(const Sequence& node);
    void visit_Ellipsis(const Sequence& node);
    void visit_Filling(const Sequence& node);
    void visit_IfStmt(const IfStmt& node);
    void visit_WhileStmt(const WhileStmt& node);
    void visit_AssignmentStmt(const AstNodeAttr& node);
    void visit_SemicolonStmt(const SemicolonStmt& node);
    void visit_LastResultCapture(const LastResultCapture& node);
    void visit_ErrorExpr(const ErrorExpr& node);
    void visit_BlockStmt(const AstNodeAttr& node);
    void visit_ThenBlock(const AstNodeAttr& node);
    void visit_ElseBlock(const AstNodeAttr& node);
    void visit_DoWhileStmt(const DoWhileStmt& node);
    void visit_WhileElseBlock(const AstNodeAttr& node);
    void visit_BreakStmt(const JumpStmt& node);
    void visit_ContinueStmt(const JumpStmt& node);
    void visit_GotoStmt(const LabelRef& node);
    void visit_LabelStmt(const LabelRef& node);
    void visit_TryCatchStmt(const TryCatchStmt& node);
    void visit_CatchBlock(const CatchBlock& node);
    void visit_MatchingStmt(const MatchStmt& node);
    void visit_WithStmt(const WithStmt& node);
    void visit_MatchingCase(const AstNodeAttr& node);
    void visit_MatchingElseBlock(const AstNodeAttr& node);
    void visit_FuncDecl(const FuncDecl& node);
    void visit_VarDecl(const VarDecl& node);
    void visit_DestructureDecl(const DestructureDecl& node);
    void visit_ArgNode(const ArgNode& node);
    void visit_EnumDecl(const Sequence& node);
    void visit_EnumMember(const Sequence& node);
    void visit_StructDecl(const RecordDecl& node);
    void visit_StructField(const Sequence& node);
    void visit_ClassDecl(const ClassDecl& node);
    void visit_ModuleDecl(const ModuleNode& node);
    void visit_Unimplemented(const AstNodeAttr& node);
    void visit_NotApplicable(const AstNodeAttr& node);
    void visit_ContextMacro(const ContextMacro& node);
    void visit_CheckAreaStmt(const CheckAreaStmt& node);
    void visit_DebugStmt(const DebugStmt& node);
    void visit_TrustContract(const TrustContract& node);
    void visit_TrustElem(const TrustElem& node);

    /// Общий контекст кодогенерации (состояние + низкоуровневые операции вывода/маппинга).
    CppEmitContext m_ectx;

    /// Счётчик временных output-файлов для рендера выражения в строку (default-аргументы).
    uint32_t m_exprRenderCounter = 0;

    /// Компоненты-эмиттеры (владеют тяжёлой логикой кодогенерации).
    TypeEmitter m_type;
    DeclEmitter m_decl;
    StmtEmitter m_stmt;
    ExprEmitter m_expr;
    ContractEmitter m_contract;

    friend class TypeEmitter;
    friend class DeclEmitter;
    friend class StmtEmitter;
    friend class ExprEmitter;
    friend class ContractEmitter;

    // Механизм диспетчеризации (ast/kind_dispatch.hpp) вызывает приватные visit_<Kind>:
    // дружба на уровне шаблона, чтобы не расширять публичный API 77 методами.
    template <class C>
    friend void dispatchEmit(const AstNodeBase& node, C& self, KindCategory expected);
    template <class C>
    friend consteval bool hasAllKindHandlers();
};
} // namespace trust
