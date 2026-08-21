#pragma once

#include "ast/ast_nodes.hpp"
#include "ast/kind_visitor.hpp"
#include "location/location.hpp"
#include "types/runtime_symbols.hpp"
#include "types/type_id.hpp"
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace trust {

class Context;

/// CppTranspiler - генератор C++ кода из AST, НАСЛЕДНИК KindVisitor.
/// visit_<Kind> - члены класса (потоковый вывод прямо в m_ctx.source(), не std::string).
/// Единый контекст: dispatchKind(node, *this) - без локальных struct-визиторов.
/// Использует Context для доступа к TypeRegistry и для маппинга позиций.
/// Опционально принимает разрешённую семантикой таблицу символов (SymbolTable),
/// чтобы кодогенерация использовала тот же TypeId, что и анализ (а не перерезолв по имени).
class SymbolTable;

class CppTranspiler : public KindVisitor {
  public:
    explicit CppTranspiler(Context& ctx, const SymbolTable* resolvedTypes = nullptr);

    /// Генерация C++ кода непосредственно в выходной файл с построением source map.
    /// Для каждого узла AST создаётся маппинг trust-range → cpp-range через mapStart/mapStop.
    /// @param ast_nodes Выход парсера (вектор AstNodePtr).
    /// @param output_idx Индекс выходного C++ файла (должен быть создан через ctx.add_output()).
    void generateToFile(const std::vector<AstNodePtr>& ast_nodes, MapperFile output_idx);

    /// Экспортированный символ: original trust-имя, сгенерированное C++ имя и trust-source
    /// предварительного объявления (например `x:Int32 := ...;`) - семантическая конструкция языка,
    /// пригодная для парсинга (используется в поле `__trust_export_decls` при сборке .trust).
    struct ExportEntry {
        std::string trustName; ///< Имя в языке Trust
        std::string cppName;   ///< Имя в сгенерированном C++ коде
        std::string fwdDecl{}; ///< Предварительное объявление в Trust-синтаксисе (:= ...;)
    };

    /// Список всех экспортированных символов (собран в процессе generateToFile).
    const std::vector<ExportEntry>& exports() const noexcept { return m_exports; }

    /// Пути рантайм-заголовков (например "trust/rational.hpp"), которые реально
    /// понадобились сгенерированному коду (маркер '@' в preprocInclude типа).
    /// Только использованные - pipeline извлечёт их из trust-runtime.so.
    const std::set<std::string>& runtimeHeaders() const noexcept { return m_runtimeHeaders; }

    /// Флаги линковки нативных библиотек (`-l<имя>`), собранные из атрибутов
    /// `@[link("имя")]` на нативных декларациях. Уникальные; pipeline добавит их
    /// в `LIBS` (build.conf) на этапе сборки.
    const std::set<std::string>& linkLibs() const noexcept { return m_linkLibs; }

  private:
    /// Единая диспетчеризация по kind: dispatchKind(node, *this).
    void generateNodeToFile(const AstNodeBase& node, MapperFile output_idx);

    /// Вывод выражения в поток source() на текущую позицию (без statement-терминатора).
    /// Для null-узла выводит "{}" (как прежний generateExpr(nullptr)).
    void emitExpr(const AstNodeBase* node);

    /// Генерация тела блока (Sequence/ScopeBlock/ModuleNode): обход m_body.
    void emitSequenceBody(const Sequence& node, MapperFile output_idx);

    /// Выводит "{}" (placeholder) только в expression-контексте (m_exprDepth>0);
    /// в statement-контексте - no-op. Для нереализованных expression-only kinds.
    void emitPlaceholderExpr(MapperFile output_idx);

    /// Унифицированный вывод текста как вложенного выражения: печатает `text`
    /// ТОЛЬКО в expression-контексте (m_exprDepth>0); в statement-контексте - no-op.
    /// Используется для «имя/литерал/embed как выражение» (visit_Ident/TypeName/
    /// EmbedExpr/IntLiteral/FloatLiteral), устраняя повторяющуюся идиому
    /// `if (m_exprDepth > 0) output_append(text)`.
    void emitExprText(std::string_view text);

    /// Унифицированная генерация return/throw: `keyword` = "return"/"throw",
    /// при наличии значения выводит `keyword <expr>;`, иначе `keyword;`.
    /// Устраняет дублирование visit_ReturnStmt/visit_ThrowStmt (одинаковые кроме ключевого слова).
    void emitJumpValue(std::string_view keyword, const JumpStmt& node);

    /// Записывает директиву инклуда в наборы (const-версия, mutable-члены):
    /// '@'-заголовок → m_runtimeHeaders (bare, для извлечения) + m_requiredIncludes;
    /// обычная директива → m_requiredIncludes. Препенд выполняется в emitCollectedIncludes.
    void recordRequiredInclude(std::string_view include) const;

    /// МЕХАНИЗМ №1 - ПО ТИПУ (TypeRegistry). Отмечает тип как использованный: в m_usedTypes
    /// кладётся КАНОНИЧЕСКИЙ TypeId (не файлы!). Инклуды из собранных типов формируются
    /// ПОСЛЕ обхода AST (collectTypeIncludes), а не в момент резолва. Вызывается из
    /// resolveCppTypeId (emitTypeName / emitTypeNameForNode).
    void recordUsedType(TypeId type_id) const;

    /// МЕХАНИЗМ №1 - ПО ТИПУ. После обхода AST формирует директивы инклудов из собранных
    /// в m_usedTypes типов (полный TypeRegistry::getPreprocIncludes каждого типа). Вызывается
    /// в конце generateToFile перед emitCollectedIncludes; '@'-заголовки при этом попадают
    /// в m_runtimeHeaders (для извлечения из trust-runtime).
    void collectTypeIncludes() const;

    /// Единая точка эмиссии C++-имени типа: резолвит имя (resolveCppTypeId) и записывает ВСЕ
    /// инклуды типа. Возвращает nullopt, если у типа нет C++-имени (caller сообщает ошибку).
    std::optional<std::string> emitTypeName(TypeId type_id, std::string_view displayName);

    /// Единая точка эмиссии C++-имени типа из узла-типа (TypeName): записывает инклуды (resolveCppType)
    /// и возвращает C++-имя. Не-тип/нерезолвящееся имя → ВСЕГДА ошибка с диагностикой и возврат "".
    /// None/Void обрабатываются явно на сайтах return/каста (не здесь, не как fallback).
    std::string emitTypeNameForNode(const AstNodeBase* type_node);

    /// МЕХАНИЗМ №2 - по рантайм-символу (типизированный идентификатор): записывает
    /// заголовки символа из компайлтайм-таблицы (types/runtime_symbols.hpp). Enum вместо
    /// строкового литерала - опечатка в имени символа невозможна (ошибка компиляции).
    /// ЕДИНСТВЕННЫЙ способ записи заголовков рантайм-символа (строковая перегрузка убрана).
    /// Используется для известных рантайм-функций кодогенерации (trust::any_to /
    /// trust::checked_cast в emitTypedConstruction) и для найденных по имени символов
    /// (visit_CallExpr) / по тексту вставки (visit_EmbedExpr через recordRuntimeSymbolsInText).
    void recordRuntimeSymbolHeaders(RuntimeSymbolId id) const;

    /// МЕХАНИЗМ №2 - для EMBED-вставок ({% %}): сканирует текст на присутствие имён
    /// рантайм-символов (substring) и записывает их заголовки через recordRuntimeSymbolHeaders(id).
    /// Это не перегрузка записи, а отдельный хелпер для случая «только текст, типа нет».
    void recordRuntimeSymbolsInText(std::string_view text) const;

    /// В конце генерации: препендит все собранные директивы (m_requiredIncludes) в начало файла.
    void emitCollectedIncludes(MapperFile output_idx);

    /// Собирает флаг линковки `-l<имя>` из атрибута `@[link("имя")]` на узле (если есть).
    /// Имя библиотеки читается через AstNodeAttr::attr_args(attr::Link)[0].
    void collectLinkLib(const AstNodeAttr& node);

    /// Выводит перевод строки между последовательными блоками при генерации C++ кода,
    /// если их строки в исходнике различаются. Если строка конца prev совпадает со строкой
    /// начала node (блоки на одной строке исходника) - перевод строки не выводится, вместо
    /// этого для читаемости ставится пробел (emitSameLineSpace).
    /// prev == nullptr означает первый блок (перевод строки не нужен).
    void emitBlockSeparator(const AstNodeBase* prev, const AstNodeBase& node, MapperFile output_idx);

    /// Для блоков на одной строке исходника: вставляет пробел между ними, если на границе
    /// ещё нет пробельного символа (не дублирует пробелы из EMBED-содержимого и т.п.).
    /// nextText - первый фрагмент следующего блока (его text() или "{" / "}").
    void emitSameLineSpace(std::string_view nextText, MapperFile output_idx);

    /// Генерация для объявления переменной (VarDecl).
    void generateVarDeclToFile(const VarDecl& var_node, MapperFile output_idx);

    /// Генерация для объявления типа (BinaryOp ::=).
    void generateTypeDeclToFile(const Binary& binary_node, MapperFile output_idx);

    /// Эмиссия enum-типа (правая часть `::=` - DictLiteral с аннотацией «Enum»):
    /// `struct c_Color : trust::Enum<Value,N>{...}` + static const члены + out-of-class
    /// определения; записывает инклуд `@trust/enum.hpp` (рантайм-шаблон).
    void emitEnumStruct(std::string_view enum_trust, const DictLiteralNode& dict, TypeId enum_id, MapperFile output_idx, MapperRange typeNameRange);

    /// Эмиссия Variant-типа (аннотация «Variant», гетерогенный): `struct c_Value { using Variant =
    /// std::variant<...>; static const <T> c_MEMBER; ... }` + out-of-class определения членов
    /// (значения из DictLiteral RHS); члены - константы своих типов; `#include <variant>`.
    void emitVariantStruct(std::string_view variant_trust, const DictLiteralNode& dict, TypeId variant_id, MapperFile output_idx, MapperRange typeNameRange);

    /// Генерация для объявления функции (FuncDecl).
    void generateFuncDeclToFile(const FuncDecl& func_node, MapperFile output_idx);

    /// Генерация условного оператора (IfStmt): if/else-if/else с маппингом диапазона.
    void generateIfToFile(const IfStmt& node, MapperFile output_idx);

    /// Генерация цикла while (WhileStmt), включая опциональный else.
    void generateWhileToFile(const WhileStmt& node, MapperFile output_idx);

    /// Генерация цикла do-while (DoWhileStmt).
    void generateDoWhileToFile(const DoWhileStmt& node, MapperFile output_idx);

    /// Генерация оператора match (MatchStmt): временная переменная + if/else-if/else.
    void generateMatchToFile(const MatchStmt& node, MapperFile output_idx);

    /// Генерация тела блока { ... } с зеркалированием строк '{' и '}' по исходнику.
    /// body - операторы тела, blockRange - диапазон блока (скобок) из исходника
    /// (невалидный, если блок/скобки недоступны - тогда между { и } перевод строки).
    /// mapBlock=false - тело не оборачивается собственным mapStart/mapStop (используется для
    /// do-while, где range statement'а и тела начинаются с '{' и их begin совпадают, что
    /// приводило бы к коллизии ключа в mapStop).
    /// beforeCloseLabel - если не пуст, перед '}' вставляется метка '<beforeCloseLabel>:;'
    /// (используется для continue-метки do-while).
    /// Отступ берётся из стека m_scopeStack; на время тела пушится дочерний контекст (отступ +1).
    void emitBlockBodyToFile(const std::vector<AstNodePtr>& body, MapperRange blockRange, MapperFile output_idx, bool mapBlock = true,
                             const std::string& beforeCloseLabel = "", const std::string& afterOpen = "");

    /// Генерация тела { ... } для одного узла-тела (ScopeBlock/Sequence или одиночный statement):
    /// собирает операторы и диапазон блока, затем вызывает emitBlockBodyToFile.
    /// beforeCloseLabel - continue-метка, вставляемая перед '}' (для do-while).
    /// afterOpen - текст, вставляемый сразу после '{' (например, установка флага while-else).
    void emitBodyNode(const AstNodePtr& body, MapperFile output_idx, bool mapBlock = true, const std::string& beforeCloseLabel = "",
                      const std::string& afterOpen = "");

    /// Единая генерация бинарного узла: statement-root (m_exprDepth==0, как ребёнок
    /// SemicolonStmt: текст без скобок; ';' добавляет SemicolonStmt) и expression-контекст
    /// (m_exprDepth>0: '(lhs op rhs)'). Для бинарных kinds с одинаковой формой - один visit_<Kind>.
    void emitBinaryStmtOrExpr(const Binary& binary_node);

    /// Эмиссия «сырого» текста бинарной операции без внешних скобок и без ';' -
    /// общий для statement/expression. Дети эмитятся через emitExpr (вложенно, с глубиной).
    /// Учитывает '//' и '//=' (целочисленное деление static_cast<int64_t>).
    void emitBinaryOpRaw(const Binary& binary_node);

    /// Эмиссия операнда бинарной операции: std::any-операнд приводится к конкретному типу
    /// результата. Элемент словаря (TypedValue) → типизированный доступ .getAs<Cpp>();
    /// переменная типа std::any → std::any_cast<Cpp>(...).
    void emitBinaryOperand(const AstNodeBase* operand, TypeId operandType, TypeId resultType);

    /// Разрешает trust-имя типа в C++-имя через TypeRegistry (canonical chain).
    /// Единый источник последовательности findType→getCanonicalTypeId→getCppTypeName→getPreprocInclude
    /// (используется в resolveCppTypeId / emitTypeName).
    /// @return {cppName, preprocInclude}, или nullopt если тип не найден / без C++-имени.
    std::optional<std::pair<std::string, std::string_view>> resolveCppType(std::string_view trustName) const;

    /// Резолвит trust-имя в TypeId: семантика (m_resolvedTypes) приоритетнее, иначе builtin-имя
    /// в реестре. Единый источник резолва по имени (используется resolveCppType и
    /// emitTypeNameForNode).
    [[nodiscard]] std::optional<TypeId> resolveTypeIdByName(std::string_view trustName) const;

    /// Разрешение по уже известному TypeId. displayName - trust-имя, сохраняемое для
    /// пользовательского алиаса; встроенные - каноническое C++-имя.
    /// Внутренний хелпер для resolveCppType.
    std::optional<std::pair<std::string, std::string_view>> resolveCppTypeId(TypeId type_id, std::string_view displayName) const;

    /// Добавляет name-маппинг для объявленного имени (hover-ссылки).
    /// Имя выводится сразу после prefixLen байт от начала текущего mapStart
    /// (prefixLen - длина уже выведенного текста перед именем, напр. "using " или "int32_t ").
    /// Оффсет всегда считается от mapStackTop().outputBegin.offset() (см. memory: инклуды
    /// output_prepend сдвигают начало вывода, нельзя предполагать offset 1).
    /// trustRange - диапазон имени в исходнике; name/cppName - trust и C++ имена.
    void mapDeclaredName(MapperFile output_idx, MapperRange trustRange, uint32_t prefixLen, std::string_view name, std::string_view cppName);

    /// True, если документирующие комментарии подавлены в C++-выводе (флаг -Wno-comments,
    /// т.е. FlagKind::Comments выключен). AST при этом всегда хранит Document-узлы; подавление -
    /// только на этапе кодогенерации. Определён в transpiler.cpp (нужен полный тип Context/Options).
    [[nodiscard]] bool isSuppressedDoc(ParserToken::Kind k) const;

    /// Эмитит документирующий комментарий, привязанный к узлу объявления
    /// (AstNodeBase::documentation, из term->m_docs грамматики) - строками с текущим
    /// отступом, с нормализацией `##`→`///`. Подавляется флагом -Wno-comments (как sibling-Document).
    /// Определён в transpiler.cpp.
    void emitDocumentation(const AstNodeBase& node, MapperFile output_idx);

    // -- KindVisitor: visit_<Kind> - члены класса (потоковый вывод в m_ctx.source()) --

    void visit_sequence(const Sequence& node) override;
    void visit_Attr(const Sequence& node) override;
    void visit_ScopeBlock(const ScopeBlock& node) override;
    void visit_TypeDecl(const Binary& node) override;
    void visit_NameDecl(const Binary& node) override;
    void visit_AssignOp(const Binary& node) override;
    void visit_AppendStmt(const Binary& node) override;
    void visit_MathOp(const Binary& node) override;
    void visit_BitwiseOp(const Binary& node) override;
    void visit_CompareOp(const Binary& node) override;
    void visit_LogicalOp(const Binary& node) override;
    void visit_MemberAccess(const Binary& node) override;
    void visit_ArrayAccess(const Binary& node) override;
    void visit_Ident(const IdentName& node) override;
    void visit_TypeName(const IdentType& node) override;
    void visit_CallExpr(const CallExpr& node) override;
    void visit_ReturnStmt(const JumpStmt& node) override;
    void visit_ThrowStmt(const JumpStmt& node) override;
    void visit_Program(const AstNodeAttr& node) override;
    void visit_VarRef(const AstNodeAttr& node) override;
    void visit_EmbedExpr(const AstNodeAttr& node) override;
    void visit_Document(const AstNodeAttr& node) override;
    void visit_IntLiteral(const Literal& node) override;
    void visit_FloatLiteral(const Literal& node) override;
    void visit_StrChar(const Literal& node) override;
    void visit_StrWide(const Literal& node) override;
    void visit_RationalLiteral(const Literal& node) override;
    void visit_ArrayInit(const DictLiteralNode& node) override;
    /// Эмитит литерал/конструкцию массива как `std::vector<Elem>{...}` (mutable) или
    /// `std::array<Elem,N>{...}` (константная). Общий для visit_ArrayInit и emitTypedConstruction.
    void emitArrayLiteral(const DictLiteralNode& n, TypeId arrayType);
    void visit_DictLiteral(const DictLiteralNode& node) override;
    void visit_Tuple(const DictLiteralNode& node) override;
    void visit_RangeExpr(const RangeExpr& node) override;
    /// Тело словаря trust::Dict{ {"name", TypedValue}, ... } - общее для голого `(...)` и
    /// типизированного с аннотацией, резолвящейся в сам Dict. Используется visit_DictLiteral.
    void emitDictLiteralBody(const DictLiteralNode& n);
    /// Типизированная конструкция/каст `:Type(...)`/`(...):Type` (не Tuple, kind==DictLiteral):
    /// по типу из реестра - словарь / каст (checked_cast/any_to) / конструктор Type(args).
    void emitTypedConstruction(const DictLiteralNode& n);
    /// Эмитит `trust::TypedValue{kind, значение}` для элемента словаря: kind - TypeId значения
    /// (Bool/Int8/…/StrChar/…/Dict). Конструктор TypedValue сам размещает значение в быструю
    /// ветку std::variant (по группе kind) либо в std::any-ветку (открытые типы, Dict). tid -
    /// единый источник типа значения (семантика сохраняет его на элементе-AssignOp:
    /// Binary::resultType из resolvedType), покрывает литералы, вложенные словари и (если
    /// выведен) другие выражения.
    void emitTypedDictValue(const AstNodeBase* valueNode, TypeId tid);
    /// Эмитит `std::format(fmt, args...)` для строки-формата: callee - строковый литерал
    /// (StrWide → wide `std::format(L"…", …)`, StrChar → узкий `std::format("…", …)`).
    /// Записывает `#include <format>`. Вызывается из visit_CallExpr.
    void emitFormatCall(const CallExpr& n);
    /// Эмитит доступ к элементу кортежа `t.name`/`t.0`/`t[idx]` → `std::get<index>(obj)`.
    /// index из Binary::tupleIndex (резолвит семантика); записывает `#include <tuple>`.
    void emitTupleElementAccess(const Binary& n);
    /// Эмитит доступ к элементу словаря: для конкретного типа поля -
    /// `obj.at(key).getAs<Cpp>()` (типизированный доступ по значению), иначе `obj.at(key)`
    /// (TypedValue). Возвращает true, если тип поля конкретный (не Any).
    bool emitDictElementAccess(const Binary& n);
    void visit_RefMakeExpr(const Sequence& node) override;
    void visit_RefTakeExpr(const Sequence& node) override;
    void visit_Ellipsis(const Sequence& node) override;
    void visit_IfStmt(const IfStmt& node) override;
    void visit_WhileStmt(const WhileStmt& node) override;
    void visit_AssignmentStmt(const AstNodeAttr& node) override;
    void visit_SemicolonStmt(const SemicolonStmt& node) override;
    void visit_BlockStmt(const AstNodeAttr& node) override;
    void visit_ThenBlock(const AstNodeAttr& node) override;
    void visit_ElseBlock(const AstNodeAttr& node) override;
    void visit_DoWhileStmt(const DoWhileStmt& node) override;
    void visit_WhileElseBlock(const AstNodeAttr& node) override;
    void visit_BreakStmt(const JumpStmt& node) override;
    void visit_ContinueStmt(const JumpStmt& node) override;
    void visit_GotoStmt(const LabelRef& node) override;
    void visit_LabelStmt(const LabelRef& node) override;
    void visit_TryCatchStmt(const Sequence& node) override;
    void visit_CatchBlock(const Sequence& node) override;
    void visit_MatchingStmt(const MatchStmt& node) override;
    void visit_MatchingCase(const AstNodeAttr& node) override;
    void visit_MatchingElseBlock(const AstNodeAttr& node) override;
    void visit_FuncDecl(const FuncDecl& node) override;
    void visit_VarDecl(const VarDecl& node) override;
    void visit_DestructureDecl(const DestructureDecl& node) override;
    /// Деструктуризация спреда-коллекции (`... source`, Dict). Без маркера - точная привязка:
    /// каждая цель → `pop_front()` (std::any); суффикс `...` у цели (`rest...`) - «остаток»
    /// (копия источника после pop; если rest-цель == источнику - мутация на месте); `_` - skip,
    /// `_...` - отброс остатка. Источник-выражение оценивается один раз во временную переменную.
    void emitDestructureDict(const DestructureDecl& node);
    /// Деструктуризация кортежа (без `...`): `auto c_ti = std::get<idx>(source);` (`#include <tuple>`);
    /// `_...` - отброс оставшихся; `rest...` - `std::make_tuple(std::get<k>(source)...)` остатка.
    void emitDestructureTuple(const DestructureDecl& node);
    void visit_ArgNode(const ArgNode& node) override;
    void visit_EnumDecl(const Sequence& node) override;
    void visit_EnumMember(const Sequence& node) override;
    void visit_StructDecl(const Sequence& node) override;
    void visit_StructField(const Sequence& node) override;
    void visit_ModuleDecl(const ModuleNode& node) override;
    // Kind=Unimplemented - узел не строится (convertForKind<Unimplemented> → ошибка); no-op.
    void visit_Unimplemented(const AstNodeAttr& node) override;
    // Kind=NotApplicable - узел никогда не строится (convertForKind<NotApplicable> → Fatal); no-op.
    void visit_NotApplicable(const AstNodeAttr& node) override;
    // Kind=ContextMacro - раскрывается анализатором до транспиляции; до транспилятора не доходит.
    void visit_ContextMacro(const ContextMacro& node) override;

    Context& m_ctx;

    /// Текущий выходной C++ файл (устанавливается в generateNodeToFile/generateToFile).
    /// Используется emitExpr для потокового вывода выражений.
    MapperFile m_out;

    /// Контекст вложенности генерации: уровень отступа. Единый стек вместо вложенных
    /// CppTranspiler-объектов и ручного проброса indent аргументами. Индентация
    /// определяется по вершине стека (см. indentLevel()). Стек пушится при входе
    /// в тело блока/функции и при каждом вложенном блоке.
    struct ScopeContext {
        int indent = 0; ///< уровень отступа (4 пробела на уровень)
    };
    std::vector<ScopeContext> m_scopeStack;

    /// Текущий уровень отступа из вершины стека (0 = top-level).
    [[nodiscard]] int indentLevel() const noexcept { return m_scopeStack.empty() ? 0 : m_scopeStack.back().indent; }

    /// Глубина вложенности выражения. 0 = statement-root (ребёнок SemicolonStmt: текст без
    /// скобок; ';' добавляет SemicolonStmt); >0 = вложенное выражение (только текст, без ';',
    /// бинарные - в скобках). Инкрементируется в emitExpr перед dispatchKind.
    int m_exprDepth = 0;

    /// Префикс отступа для текущего уровня (4 пробела на уровень).
    [[nodiscard]] std::string indentPrefix() const { return std::string(static_cast<size_t>(indentLevel()) * 4, ' '); }

    /// True, если текущая генерация идёт ВНУТРИ C++ compound statement (тело функции или
    /// тело управляющей конструкции if/while/do-while/match). Используется для:
    /// (1) формы блока кода `{ ... }` (только внутри функции/класса);
    /// (2) валидности: безымянный блок/метка разрешены только внутри функции, область имён -
    ///     только вне (на верхнем уровне модуля). Устанавливается в emitBlockBodyToFile.
    bool m_inCppBlock = false;

    /// Стек имён вложенных областей имён (для квалификации экспорта). Пушится при входе
    /// в именованную область имён (`ns::`), попается при выходе. Глобальная `::` и скрытая `_`
    /// имя в стек не добавляют (корень / анонимная область).
    std::vector<std::string> m_namespaceStack;

    /// Глубина вложенности скрытых (анонимных) областей имён `_`. >0 ⇒ мы внутри скрытой
    /// области: экспорт подавлен (в т.ч. для вложенных в неё областей имён).
    int m_hiddenNamespaceDepth = 0;

    /// Полное квалифицированное C++-имя (с учётом стека областей имён). Для верхнего уровня
    /// модуля / глобальной области `::` - само `name`.
    [[nodiscard]] std::string qualifiedCppName(std::string_view name) const;

    /// Имя C++ namespace из text() области имён: убирает ведущий и завершающий "::"
    /// (например "ns::" → "ns", "::ns::name" → "ns::name").
    static std::string namespaceCppName(std::string_view text);

    /// Эмиссия compound statement `{ ... }` (безымянный блок кода / именованная метка внутри
    /// функции). Содержимое - с отступом +1.
    void emitCompoundScope(const ScopeBlock& n);

    /// Эмиссия области имён: `namespace ns { ... }` (name != "") либо анонимное
    /// `namespace { ... }` (name == "", скрытая `_`). Содержимое - с отступом +1.
    void emitNamespaceScope(const ScopeBlock& n, const std::string& name);

    /// Счётчик временных переменных для операторов match.
    uint32_t m_matchCounter = 0;

    /// Счётчик флагов для эмуляции while-else (в C++ нет 'while...else').
    uint32_t m_whileElseCounter = 0;

    /// Счётчик временных переменных для деструктуризации спреда (`... source`): источник-выражение
    /// оценивается один раз во временную переменную `__trust_dst_<N>`, из которой делаются pop_front
    /// и rest-копия (избегаем многократной оценки источника-выражения).
    uint32_t m_destructureCounter = 0;

    /// В режиме forward-decl-only кодогенерация объявлений (var/func) подавляет определение
    /// (инициализатор/тело) - используется на сайте импорта модуля (только объявления экспортов).
    bool m_forwardDeclOnly = false;

    /// Эмиссия forward-decl экспортируемого интерфейса на сайте импорта модуля
    /// (`ModuleNode::isImport()`). Прототипы функций / extern переменных / алиасы типов.
    void emitModuleImportDecls(const ModuleNode& n);
    /// Рекурсивная эмиссия forward-decl внутри областей имён (обход m_body импортируемого модуля).
    void emitImportScope(const std::vector<AstNodePtr>& body, const std::set<const Term*>& terms, MapperFile out);
    /// Предварительное объявление экспортируемого узла в Trust-синтаксисе (напр. `x:Int32 := ...;`),
    /// пригодное для парсинга (для `ExportEntry::fwdDecl` / `__trust_export_decls`).
    std::string buildTrustForwardDecl(const AstNodeBase& node) const;

    /// Экспортированные символы (пополняется в generateVarDeclToFile).
    std::vector<ExportEntry> m_exports;

    /// Заголовки рантайма (bare-имена, маркер '@'), реально использованные кодом - для извлечения
    /// из trust-runtime.so. mutable: записываются и из const-методов (resolveCppTypeId).
    mutable std::set<std::string> m_runtimeHeaders;

    /// Собранные за время эмиссии полные директивы #include (дедуп). Препендятся в конце
    /// (emitCollectedIncludes). mutable: записываются из const-методов.
    mutable std::set<std::string> m_requiredIncludes;

    /// МЕХАНИЗМ №1 - ПО ТИПУ: канонические TypeId типов, реально использованных при эмиссии.
    /// Инклуды из них выводятся ПОСЛЕ обхода AST (collectTypeIncludes), а не в момент резолва.
    /// mutable: записываются из const-методов (resolveCppTypeId / recordUsedType).
    mutable std::set<TypeId> m_usedTypes;

    /// Флаги линковки нативных библиотек (`-l<имя>`), собранные из `@[link("имя")]`.
    std::set<std::string> m_linkLibs;

    /// Импорты нативных функций: trust-имя → нативное C++-имя (`fabs`→"abs", `fabs`→"std::sqrt").
    /// Заполняется при обработке FuncDecl с m_isNativeImport; вызовы trust-имени переписываются
    /// в прямой вызов нативного имени (C++-функция не эмитится).
    std::unordered_map<std::string, std::string> m_nativeImports;

    /// Разрешённая семантикой таблица символов (необязательно). Если задана,
    /// resolveCppType сначала пробует взять TypeId из неё (единый источник с анализом),
    /// иначе - резолв по имени через TypeRegistry.
    const SymbolTable* m_resolvedTypes = nullptr;
};
} // namespace trust