// include/ast/ast_nodes.hpp
// Специализированные AST-узлы: Literal, Sequence, ScopeBlock, ModuleNode, Binary,
// CallExpr, JumpStmt, ControlFlowStmt/IfStmt/WhileStmt/DoWhileStmt, MatchStmt,
// ArgNode, Decl/VarDecl/FuncDecl, LabelRef, SemicolonStmt, ContextMacro.
//
// Иерархия (полная - см. ast/MEMORY.md «Полная иерархия»; здесь - главные ветви):
//   AstNodeBase (token_base.hpp) - kind, TermPtr m_term
//     └-- AstNodeAttr - +attrs
//           ├-- HasText - +m_text (единый локальный текст узла), text() из m_text
//           │     ├-- Literal      (Int|Float|StrChar|StrWide)
//           │     ├-- ContextMacro (лист-маркер, раскрывается в семантике)
//           │     ├-- ArgNode    (+m_type, m_value; ЕДИНЫЙ узел аргумента)
//           │     ├-- Sequence     (+m_body; text()=метка/текст)
//           │     │     ├-- ScopeBlock  (+m_blockCounter)
//           │     │     └-- ModuleNode  (+m_moduleIndex)
//           │     └-- IdentName   (Ident; идентификатор + методы)  - ident_name.hpp
//           │           ├-- IdentType   (TypeName, +m_dims, m_params)  - token_type.hpp
//           │           └-- Decl        (+m_type)
//           │                 ├-- VarDecl     (+m_initializer)
//           │                 └-- FuncDecl    (+m_params, m_body)
//           ├-- Binary      (TypeDecl|NameDecl|AssignOp|MathOp|BitwiseOp|CompareOp|
//           │                LogicalOp|MemberAccess|ArrayAccess, +m_left, m_right)
//           ├-- CallExpr    (+m_callee, m_args)
//           ├-- JumpStmt    (ReturnStmt|ThrowStmt|BreakStmt|ContinueStmt, +m_label, m_value)
//           ├-- LabelRef    (GotoStmt|LabelStmt, +m_name) - СИНТЕТИЧЕСКИЙ (lowering)
//           ├-- SemicolonStmt (+m_expr) - СИНТЕТИЧЕСКИЙ (lowering)
//           ├-- ControlFlowStmt (+m_cond, m_body, m_else)
//           │     ├-- IfStmt       (+m_elseifs)
//           │     ├-- WhileStmt
//           │     └-- DoWhileStmt
//           └-- AstNodeAttr (leaf-kind'ы: Program|VarRef|EmbedExpr|Document|...)
//
// Все поля, ранее использовавшие SyntaxToken, заменены на AstNodePtr.
// Списки узлов - std::vector<AstNodePtr>.
//
// Конструкторы с TermPtr - основной путь создания (из терминов синтаксического дерева).
// Конструкторы без Term (test-only) существуют для unit-тестов; text() у классов
// с m_text работает, range() для узла без Term вызывает EXPECT.

#pragma once

#include "ast/token_base.hpp"
#include "ast/token.hpp"
#include "ast/ident_name.hpp"
#include "ast/trust_prop.hpp"
#include "ast/z3_term.hpp"
#include "types/type_id.hpp"
#include "utils/error.hpp"
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace trust {

// Forward declaration (full definition in diag/context.hpp). Needed for the
// `Context* ctx = nullptr` parameter of term-constructors that build children.
class Context;

/// Literal - узел AST для константных литералов (IntLiteral, FloatLiteral, StrChar, StrWide).
/// kind = IntLiteral | FloatLiteral | StrChar | StrWide.
/// Переопределяет dump() для форматирования: IntLiteral '42', StrChar "hello".
class Literal : public HasText {
  public:
    Literal() = default;

    /// Терм-конструктор: текст читается из Term (text() работает; range() из term).
    Literal(ParserToken::Kind k, TermPtr term)
    : HasText(k, std::move(term)) {}

    /// Uniform term-constructor for the generated factory (ctx игнорируется - литерал лист).
    Literal(ParserToken::Kind k, TermPtr term, Context* /*ctx*/)
    : HasText(k, std::move(term)) {}

    /// Manual-конструктор: текст задан явно, без TermPtr (range() вернёт invalid range).
    Literal(ParserToken::Kind k, std::string text)
    : HasText(k, std::move(text)) {}

    /// Класс литерала: IntLiteral | FloatLiteral | StrChar ('…', узкая строка) | StrWide ("…", широкая строка).
    /// Ширина строки задаётся Kind (вариант выбирается Kind), поле-флаг не нужен.

    /// Тип литерала (TypeId), вычисленный семантикой (NameResolutionPass::typeExpr →
    /// literalType) и кешированный на узле, чтобы транспилятор (литерал словаря →
    /// TypedValue{kind, значение}) не пересчитывал тип и не дублировал логику диапазонов
    /// литералов. Из TypeId получается и kind (getKindFromId), и C++-имя (TypeRegistry).
    /// INVALID_TYPE_ID - тип не выведен.
    TypeId typeId = INVALID_TYPE_ID;

    [[nodiscard]] std::string dump(size_t indent = 0) const override;
};

/// ContextMacro - контекстный макрос (@__NAMESPACE__, @::, @__FUNCTION__, @__FUNCSIG__,
/// @__FUNCDNAME__). Лист: хранит только text() - имя макроса, с возможными ведущими
/// маркерами стрингификации (@#/@#'/@#"), которых может быть несколько (см. семантику
/// контекст-макросов). kind = ContextMacro.
/// Раскрывается анализатором (заменяется на Literal(StrChar) или имя), поэтому
/// транспилятор НЕ обрабатывает такие узлы.
class ContextMacro : public HasText {
  public:
    ContextMacro() = default;

    /// Uniform term-constructor for the generated factory (ctx игнорируется - лист).
    ContextMacro(ParserToken::Kind k, TermPtr term, Context* /*ctx*/)
    : HasText(k, std::move(term)) {}

    /// Manual-конструктор: текст задан явно, без TermPtr (range() вернёт invalid range).
    ContextMacro(ParserToken::Kind k, std::string text)
    : HasText(k, std::move(text)) {}

    [[nodiscard]] std::string dump(size_t indent = 0) const override;
};

/// Binary - бинарная операция, member access или array access.
/// kind = TypeDecl | NameDecl | AssignOp | MathOp | BitwiseOp | CompareOp |
///        LogicalOp | MemberAccess | ArrayAccess.
/// Текст оператора (text()) берётся из исходного Term.
class Binary : public AstNodeAttr {
  public:
    Binary() = default;

    /// text() читается из m_term - конструктор требует валидный Term.
    /// При `ctx != nullptr` сам строит детей: m_left = TermToAstConverter(term->m_left),
    /// m_right = TermToAstConverter(term->m_right). Объявлен здесь, определён в ast_nodes.cpp.
    Binary(ParserToken::Kind k, TermPtr term, Context* ctx = nullptr);

    /// Manual-конструктор с детьми (без TermPtr; range() вернёт invalid range).
    Binary(ParserToken::Kind k, AstNodePtr left, AstNodePtr right)
    : AstNodeAttr(k)
    , m_left(std::move(left))
    , m_right(std::move(right)) {}

    explicit Binary(ParserToken::Kind k)
    : AstNodeAttr(k) {}

    /// range: [left.begin, right.end] - полный охват выражения/строки (операторный терм несёт
    /// range только оператора). Вычисляется на лету из детей (m_left/m_right) в ast_nodes.cpp,
    /// без мутации исходного Term. Если обеих сторон нет - базовый range из m_term.
    [[nodiscard]] MapperRange range() const override;

    [[nodiscard]] std::string dump(size_t indent = 0) const override;
    void lower(AstNodePtr& self, LowerCtx& ctx) override;

    AstNodePtr m_left;
    AstNodePtr m_right;

    /// Семантические типы операндов и результата (заполняет NameResolutionPass::typeExpr;
    /// INVALID_TYPE_ID - не выведено). Используются транспилятором для тип-зависимой
    /// кодогенерации операторов (напр. std::any_cast для std::any-операндов).
    TypeId lhsType{INVALID_TYPE_ID};
    TypeId rhsType{INVALID_TYPE_ID};
    TypeId resultType{INVALID_TYPE_ID};
    /// Общий тип операндов для any_cast: для арифметики = resultType; для Compare/Logical
    /// (результат Bool) - продвинутый тип конкретного операнда, если один из них std::any.
    TypeId commonType{INVALID_TYPE_ID};
    /// Для MemberAccess/ArrayAccess по кортежу `t.name`/`t.0`: индекс элемента в TupleTypeData
    /// (резолвит семантика в resolveTupleAccess). -1 - не кортежный доступ (словарь/прочее).
    int64_t tupleIndex{-1};

    /// Узел декларации ТИПА (TypeDecl) для присваивания в переменную доверенного типа
    /// (`x = ...`, где `x :MyInt`). Ставит NameResolutionPass (typeBinaryResult, AssignOp) для
    /// typeIsTrusted-целей; условия типа читаются из него как `m_typeDecl->m_trust`. Не владеющая
    /// ссылка в AST модуля - переживает таблицу символов. nullptr - нет/нетрастовый/не-AssignOp.
    const AstNodeBase* m_typeDecl = nullptr;
};

/// Является ли kind «блочным» узлом (имеет собственный обход тела с отступами:
/// ScopeBlock / sequence / ModuleDecl). Используется в транспиляторе для решения,
/// выставлять ли отступ перед дочерним оператором (блочные узлы делают это сами).
/// Генерируется из PARSER_TOKEN_KINDS по node_type: ScopeBlock/ModuleNode, либо
/// kind==sequence (node_type Sequence; Attr - НЕ блочный, исключён явно).
[[nodiscard]] inline bool is_block_kind(ParserToken::Kind k) noexcept {
    switch (k) {
#define IS_BLOCK_KIND_CASE(name, node_type)                                                      \
    case ParserToken::Kind::name:                                                                \
        return std::is_same_v<node_type, ScopeBlock> || std::is_same_v<node_type, ModuleNode> || \
               (std::is_same_v<node_type, Sequence> && ParserToken::Kind::name == ParserToken::Kind::sequence);
        PARSER_TOKEN_KINDS(IS_BLOCK_KIND_CASE)
#undef IS_BLOCK_KIND_CASE
    default:
        return false;
    }
}

/// Является ли kind «типизируемым бинарным выражением» (результат типа выводится
/// пост-порядково анализатором). Единый источник набора Binary-kinds, участвующих в
/// типизации: MathOp | BitwiseOp | CompareOp | LogicalOp | NameDecl | AssignOp.
/// Используется в AnalysisContext::resolvedType и NameResolutionPass::typeExpr.
[[nodiscard]] constexpr bool is_binary_expr_kind(ParserToken::Kind k) noexcept {
    return k == ParserToken::Kind::MathOp || k == ParserToken::Kind::BitwiseOp || k == ParserToken::Kind::CompareOp || k == ParserToken::Kind::LogicalOp ||
           k == ParserToken::Kind::NameDecl || k == ParserToken::Kind::AssignOp || k == ParserToken::Kind::AppendStmt;
}

/// Является ли kind литералом (константным значением). Единый источник набора литеральных
/// kinds: IntLiteral | FloatLiteral | StrChar | StrWide | RationalLiteral. Используется
/// типизацией литералов (type_inference.hpp), resolvedType/dictElementType (семантика) и
/// кодогенерацией (обёртка значения точным C++-типом в trust::TypedValue), чтобы классификация
/// литералов жила в одном месте, а не дублировалась switch/условиями в потребителях.
[[nodiscard]] constexpr bool is_literal_kind(ParserToken::Kind k) noexcept {
    return k == ParserToken::Kind::IntLiteral || k == ParserToken::Kind::FloatLiteral || k == ParserToken::Kind::StrChar || k == ParserToken::Kind::StrWide ||
           k == ParserToken::Kind::RationalLiteral;
}

/// Является ли kind литералом коллекции (набор элементов, хранящийся в m_body). Единая
/// классификация коллекций для унификации нормализации элементов и кодогенерации: элементы
/// любой коллекции нормализуются к ОДНОЙ форме Binary(AssignOp) (left=метка/пусто,
/// right=значение), см. term_to_ast (visit_DICT). DictLiteral - реализован (trust::Dict);
/// Tuple - структурный кортеж (kind==Tuple выставляется анализатором из DictLiteral по типу
/// из реестра; элементы те же Binary(AssignOp)); ArrayInit - структура та же, кодогенерация -
/// отдельной задачей (нужен рантайм-тип контейнера).
[[nodiscard]] constexpr bool is_collection_literal_kind(ParserToken::Kind k) noexcept {
    return k == ParserToken::Kind::DictLiteral || k == ParserToken::Kind::Tuple || k == ParserToken::Kind::ArrayInit;
}

/// CallExpr - вызов функции: callee(args...)
/// kind = CallExpr.
class CallExpr : public AstNodeAttr {
  public:
    CallExpr() = default;

    /// text() читается из m_term - конструктор требует валидный Term.
    CallExpr(ParserToken::Kind k, TermPtr term)
    : AstNodeAttr(k, std::move(term)) {}

    /// Uniform term-constructor for the generated factory (kind = CallExpr).
    /// При ctx != nullptr строит детей: m_callee=IdentName(m_term), m_args=convertChildren(m_term).
    /// Объявлен здесь, определён в ast_nodes.cpp.
    CallExpr(ParserToken::Kind k, TermPtr term, Context* ctx);

    /// Manual-конструктор с callee (без TermPtr; range() вернёт invalid range).
    CallExpr(ParserToken::Kind k, AstNodePtr callee)
    : AstNodeAttr(k)
    , m_callee(std::move(callee)) {}

    [[nodiscard]] std::string dump(size_t indent = 0) const override;
    void lower(AstNodePtr& self, LowerCtx& ctx) override;

    AstNodePtr m_callee;                           ///< Вызываемое выражение
    std::optional<std::vector<AstNodePtr>> m_args; ///< Аргументы вызова
};

/// ArgNode - ЕДИНЫЙ узел позиции списка аргументов: параметр функции, элемент словаря/enum/variant,
/// аргумент вызова. kind = ArgNode.
/// text() - имя (простая строка, не IdentName); "" - безымянный (позиционный аргумент/значение).
/// Голое значение `42` - это ArgNode{name="", value=42}. Явный тип `name:Type` - в m_type;
/// значение (после '=') - в m_value; разрешённый тип значения - в resultType (ставит семантика).
class ArgNode : public HasText {
  public:
    ArgNode() = default;

    /// Терм-конструктор: имя параметра читается из Term (у ARGUMENT-терма - из m_left);
    /// тип/значение задаются отдельно. Объявлен здесь, определён в ast_nodes.cpp.
    ArgNode(TermPtr term, AstNodePtr type = nullptr, AstNodePtr value = nullptr);

    /// Uniform term-constructor for the generated factory (kind = ArgNode).
    /// При ctx != nullptr строит m_type из m_term. Объявлен здесь, определён в ast_nodes.cpp.
    ArgNode(ParserToken::Kind /*k*/, TermPtr term, Context* ctx = nullptr);

    /// Manual-конструктор: имя задано явно, без TermPtr; тип/значение задаются отдельно.
    ArgNode(std::string name, AstNodePtr type = nullptr, AstNodePtr value = nullptr)
    : HasText(ParserToken::Kind::ArgNode, std::move(name))
    , m_type(std::move(type))
    , m_value(std::move(value)) {}

    [[nodiscard]] std::string dump(size_t indent = 0) const override;

    AstNodePtr m_type{nullptr};         ///< Явный тип (name:Type; nullptr - нет)
    AstNodePtr m_value{nullptr};        ///< Значение (после '='; nullptr - безнарный)
    TypeId resultType{INVALID_TYPE_ID}; ///< Разрешённый тип значения (ставит семантика)
};

/// Sequence - узел с телом (последовательностью дочерних токенов).
/// kind = sequence | Attr.
/// text() - метка/текст узла (у ScopeBlock/ModuleNode - имя области/модуля).
class Sequence : public HasText {
  public:
    Sequence() = default;

    /// Терм-конструктор: текст/метка читается из Term.
    /// При `ctx != nullptr` сам строит m_body через TermToAstConverter/TermToAstConverter.
    /// Объявлен здесь, определён в ast_nodes.cpp.
    Sequence(ParserToken::Kind k, TermPtr term, Context* ctx = nullptr);

    /// Manual-конструктор: текст/метка задана явно, без TermPtr.
    Sequence(ParserToken::Kind k, std::string text)
    : HasText(k, std::move(text)) {}

    [[nodiscard]] std::string dump(size_t indent = 0) const override;
    void lower(AstNodePtr& self, LowerCtx& ctx) override;

    /// Утилита для дампа содержимого списка узлов (переиспользуется в ScopeBlock, ModuleNode)
    static void dumpBody(std::string& result, const std::vector<AstNodePtr>& body, size_t indent, size_t child_indent);

    [[nodiscard]] Sequence* as_sequence() noexcept override { return this; }
    [[nodiscard]] const Sequence* as_sequence() const noexcept override { return this; }

    std::vector<AstNodePtr> m_body; ///< Тело/содержимое узла
};

/// DictLiteral - литерал словаря/набора элементов (и типизированная конструкция/кортеж).
/// kind = DictLiteral (механический, ставит term_to_ast) | Tuple (уточняется анализатором).
/// Наследует Sequence: m_body = элементы, нормализованные к Binary(AssignOp)
/// (left=имя или пусто, right=значение), см. term_to_ast (visit_DICT).
///
/// Поле m_type - аннотация типа литерала/конструкции (`(args):Type` / `:Type(args)`),
/// резолвится анализатором через TypeRegistry. По резолвленному типу анализатор решает
/// класс узла: если тип - Tuple (type_category::Tuple) - kind меняется на Tuple, а тип
/// выражения - на интернированный структурный кортеж (getOrCreateTupleType). Для любого
/// другого типа это типизированная конструкция/каст (решение в кодогенерации). m_type==nullptr
/// - обычный словарь (trust::Dict). Никаких строковых дискриминаторов ("Tuple") нет.
class DictLiteralNode : public Sequence {
  public:
    using Sequence::Sequence;

    /// Аннотация типа литерала/конструкции (см. комментарий класса). nullptr - нет аннотации.
    AstNodePtr m_type;

    /// true - префиксная форма `:Type(args)` (type-call/конструкция; голые аргументы - значения).
    /// false - постфиксная форма `(args):Type` (типизированный литерал). Различает объявление
    /// enum/variant (постфикс) от type-call (префикс) в analyzeTypeDecl/analyzeDictLiteral.
    bool prefix = false;

    /// Для kind==ArrayInit (литерал `[...]`): интернированный структурный массив `Array<Elem>`
    /// (тип выражения), вычисленный семантикой (analyzeArrayInit). Для DictLiteral/Tuple - INVALID.
    TypeId arrayType{INVALID_TYPE_ID};

    /// Для конструкции массива `:Array^(...):Elem` - аннотация типа ЭЛЕМЕНТА (хвостовая `:Elem`).
    /// В литерале `[1,2,3,]:Elem` элементная аннотация лежит в m_type (visit_TENSOR). Здесь -
    /// префиксная форма `:Type(...):Elem`, где m_type занят контейнером (`:Array`).
    AstNodePtr arrayElementAnnotation;
};

/// RangeExpr - литерал диапазона `start..stop` / `start..stop..step`.
/// kind = RangeExpr.
/// Наследует Sequence: m_body = [start, stop, (step)] (операнды в порядке появления из
/// терма `range` - грамматика `range: range_val RANGE range_val [RANGE range_val]` кладёт
/// детей в m_args с именами start/stop/step, конвертер читает их в этом порядке).
/// Тип элемента (elementType) - join типов start/stop/step - ставит семантика
/// (NameResolutionPass::analyzeRangeExpr) и читает кодогенерация (visit_RangeExpr), которая
/// эмитит `trust::Range<elementType>`.
class RangeExpr : public Sequence {
  public:
    using Sequence::Sequence;

    /// Manual-конструктор для тестов/codegen: 2 операнда (start, stop), без шага.
    RangeExpr(ParserToken::Kind k, AstNodePtr start, AstNodePtr stop)
    : Sequence(k, "") {
        m_body.push_back(std::move(start));
        m_body.push_back(std::move(stop));
    }

    /// Manual-конструктор: 3 операнда (start, stop, step).
    RangeExpr(ParserToken::Kind k, AstNodePtr start, AstNodePtr stop, AstNodePtr step)
    : Sequence(k, "") {
        m_body.push_back(std::move(start));
        m_body.push_back(std::move(stop));
        m_body.push_back(std::move(step));
    }

    /// Тип элемента диапазона (join start/stop/step), INVALID_TYPE_ID - не выведен.
    TypeId elementType = INVALID_TYPE_ID;

    /// Явные аннотации типа операндов (`start:Type`, `stop:Type`, `step:Type`), по одной на
    /// каждый элемент m_body (nullptr - аннотации нет). Грамматика `digits_literal type_item`
    /// кладёт тип в m_type терма-операнда; конвертер переносит его сюда (см. visit_RANGE),
    /// чтобы семантика (analyzeRangeExpr) учитывала явную аннотацию при join элементного типа
    /// (напр. `0..100:Rational` → Rational). Заполняет терм-конструктор; пуст для manual-узлов.
    std::vector<AstNodePtr> operandTypes;

    /// Начало диапазона (первый операнд). INVARIANT: m_body.size() >= 2.
    [[nodiscard]] const AstNodePtr& start() const { return m_body.at(0); }
    /// Конец диапазона (второй операнд).
    [[nodiscard]] const AstNodePtr& stop() const { return m_body.at(1); }
    /// true - задан явный шаг (3 операнда).
    [[nodiscard]] bool hasStep() const { return m_body.size() >= 3; }
    /// Шаг (третий операнд); EXPECT, если шаг не задан.
    [[nodiscard]] const AstNodePtr& step() const {
        EXPECT(hasStep() && "RangeExpr::step(): range has no explicit step");
        return m_body.at(2);
    }

    [[nodiscard]] std::string dump(size_t indent = 0) const override;
};

/// ScopeBlock - блок с меткой/именем области.
/// kind = ScopeBlock.
/// Наследует Sequence (+m_body от родителя).
/// Имя блока хранится в text().
///   ""       - блок без метки { ... } (анонимный)
///   "_"      - скрытая область реализации (имена из нее не эспортируются) _ { ... }
///   "name"   - именованная область name { ... }
///   "::" - глобальная область имен :: { ... }
///   "::ns::name" - глобальная именованная область имен ::ns::name { ... }
///   "ns::name::" - именованная область имен ns::name:: { ... }
/// Поле m_blockCounter - глобально уникальный идентификатор блока,
/// автоматически присваиваемый из Context::nextBlockCounter().
class ScopeBlock : public Sequence {
  public:
    ScopeBlock() = default;

    /// Терм-конструктор: метка блока читается из Term.
    /// При `ctx != nullptr` сам строит m_body (делегирует Sequence-конструктору).
    /// Объявлен здесь, определён в ast_nodes.cpp.
    ScopeBlock(TermPtr term, Context* ctx = nullptr, int blockCounter = 0);

    /// Uniform term-constructor for the generated factory (kind всегда ScopeBlock).
    ScopeBlock(ParserToken::Kind /*k*/, TermPtr term, Context* ctx = nullptr)
    : ScopeBlock(std::move(term), ctx) {}

    /// Manual-конструктор: метка задана явно, без TermPtr.
    ScopeBlock(std::string name, int blockCounter = 0)
    : Sequence(ParserToken::Kind::ScopeBlock, std::move(name))
    , m_blockCounter(blockCounter) {}

    /// Блок без пользовательской метки (анонимный `{ ... }`). Парсер хранит text() = "{"
    /// (терм LBRACE), а не пустую строку (см. документированный инвариант «пустой text =
    /// анонимный»), поэтому анонимным считается пустой text или ровно "{". Именованные
    /// области ("name::", "::ns::name") и скрытая "_" - не анонимные.
    [[nodiscard]] bool is_anonymous() const noexcept { return text().empty() || text() == "{"; }
    [[nodiscard]] bool is_hidden() const noexcept { return text() == "_"; }
    [[nodiscard]] std::string_view name() const noexcept { return text(); }
    [[nodiscard]] int blockCounter() const noexcept { return m_blockCounter; }

    [[nodiscard]] std::string dump(size_t indent = 0) const override;
    void lower(AstNodePtr& self, LowerCtx& ctx) override;

  private:
    int m_blockCounter{0}; ///< Глобально уникальный идентификатор блока
};

/// ModuleNode - узел AST, представляющий загруженный модуль.
/// Наследует Sequence (через m_body содержит тело модуля из cacheBody).
/// Хранит индекс записи в ModuleRegistry.
/// Имя модуля доступно через text().
/// kind = ModuleDecl.
class ModuleNode : public Sequence {
  public:
    ModuleNode() = default;

    /// Терм-конструктор: имя модуля читается из Term.
    ModuleNode(std::size_t moduleIndex, TermPtr term)
    : Sequence(ParserToken::Kind::ModuleDecl, std::move(term))
    , m_moduleIndex(moduleIndex) {}

    /// Uniform term-constructor for the generated factory. При ctx != nullptr строит m_body
    /// из term->m_sequence (loader-free, рекурсивная конвертация данных модуля). index=0.
    ModuleNode(ParserToken::Kind k, TermPtr term, Context* ctx = nullptr);

    /// Manual-конструктор: имя задано явно, без TermPtr (range() вернёт invalid range).
    ModuleNode(std::size_t moduleIndex, std::string name)
    : Sequence(ParserToken::Kind::ModuleDecl, std::move(name))
    , m_moduleIndex(moduleIndex) {}

    [[nodiscard]] std::size_t moduleIndex() const noexcept { return m_moduleIndex; }
    void setModuleIndex(std::size_t idx) { m_moduleIndex = idx; }
    [[nodiscard]] std::string_view moduleId() const noexcept { return text(); }

    /// Отфильтрованный экспорт-интерфейс модуля (декларации-термы, фактически вносимые
    /// в скоуп на данном сайте импорта). Заполняется анализатором. Пуст для корневого
    /// модуля главного файла (корень эмитит полное тело).
    [[nodiscard]] const std::vector<TermPtr>& exports() const noexcept { return m_exports; }
    void setExports(std::vector<TermPtr> exports) { m_exports = std::move(exports); }

    /// Сайт импорта (`\module(mod, masks)`), а не корневой модуль главного файла.
    /// Устанавливается в терм-конструкторе (visit_MODULE). Для корня - false.
    [[nodiscard]] bool isImport() const noexcept { return m_isImport; }

    /// Список масок фильтра интерфейса (glob, через запятую). Пусто = импортировать все экспорты.
    [[nodiscard]] std::string_view importMasks() const noexcept { return m_importMasks; }
    void setImportMasks(std::string masks) { m_importMasks = std::move(masks); }

    [[nodiscard]] std::string dump(size_t indent = 0) const override;
    void lower(AstNodePtr& self, LowerCtx& ctx) override;

  private:
    std::size_t m_moduleIndex{0};   ///< Индекс записи в ModuleRegistry
    std::vector<TermPtr> m_exports; ///< Отфильтрованный экспорт-интерфейс для сайта импорта
    bool m_isImport{false};         ///< true - сайт импорта `\module(...)`, а не корень
    std::string m_importMasks;      ///< Маски фильтра интерфейса (glob, через запятую)
};

/// ТИПИЗИРОВАННЫЕ классы для kinds, ранее отображавшихся на «catch-all» AstNodeAttr/Sequence.
/// Каждый kind получает детерминированный класс (one kind → one class). Все - наследники
/// Sequence, чтобы нести дочерние узлы (поля/тело) в унаследованном m_body.
/// Правило конструкторов: терм-конструктор (Kind, TermPtr) + manual (Kind, text) без TermPtr.

/// Добавляет m_type - тип объявления (может быть nullptr).
class Decl : public IdentName {
  public:
    Decl() = default;

    /// Терм-конструктор: имя объявления читается из Term.
    Decl(TermPtr term)
    : IdentName(std::move(term)) {}

    /// Manual-конструктор: имя задано явно, без TermPtr.
    Decl(std::string text)
    : IdentName(std::move(text)) {}

    [[nodiscard]] std::string dump(size_t indent = 0) const override;

    /// Тип объявления (nullptr если тип не указан или выводится).
    AstNodePtr m_type;
};

/// FuncDecl - объявление функции.
/// kind = FuncDecl.
/// Наследует Decl: имя функции (с % префиксом) в text(), тип в m_type.
/// Поддерживает:
///   %func(params):Type := { body }  - полное определение
///   %func(params):Type := ... ;     - предварительное объявление (m_body = nullopt)
class FuncDecl : public Decl {
  public:
    FuncDecl() { m_kind = ParserToken::Kind::FuncDecl; }

    /// Терм-конструктор: имя функции читается из Term (у операторного терма `::=` - из m_left).
    /// Объявлен здесь, определён в ast_nodes.cpp.
    FuncDecl(TermPtr term);

    /// Uniform term-constructor for the generated factory (kind = FuncDecl).
    /// При ctx != nullptr строит m_params/m_body/m_type; для CREATE_NAME-функции (сигнатура
    /// в m_left) - ещё и мутацию term->m_mapperRange до [имя, оператор] (признак функции).
    /// Объявлен здесь, определён в ast_nodes.cpp.
    FuncDecl(ParserToken::Kind /*k*/, TermPtr term, Context* ctx = nullptr);

    /// Manual-конструктор: имя задано явно, без TermPtr.
    FuncDecl(std::string text)
    : Decl(std::move(text)) {
        m_kind = ParserToken::Kind::FuncDecl;
    }

    [[nodiscard]] std::string dump(size_t indent = 0) const override;
    void lower(AstNodePtr& self, LowerCtx& ctx) override;

    /// Строка сигнатуры функции (для контекст-макроса @__FUNCSIG__):
    /// "ns::name(params):Ret". namespace_path - текущая область имён (для полного имени),
    /// пустая - имя без префикса. Имя без ведущего '%'.
    [[nodiscard]] std::string signature(std::string_view namespace_path) const;

    /// range: для функции с сигнатурой в m_left (CREATE_NAME `:=`) - [имя, оператор] без тела;
    /// иначе базовый range. Вычисляется на лету (без мутации Term) в ast_nodes.cpp.
    [[nodiscard]] MapperRange range() const override;

    /// Диапазон тела функции - блока `{ ... }` (m_term->m_right), если доступен.
    /// Используется для зеркалирования раскладки исходника ({ и } на тех же строках).
    /// Возвращает invalid range, если терм/блок недоступны (test-only узлы).
    [[nodiscard]] MapperRange blockRange() const noexcept;

    /// Параметры функции (каждый - ArgNode)
    std::optional<std::vector<AstNodePtr>> m_params;

    /// Тело функции (опционально - для предварительных объявлений)
    std::optional<std::vector<AstNodePtr>> m_body;

    /// Импорт нативной функции `<name>(...):T := %native...;` - АЛИАС: C++-функция НЕ эмитится,
    /// вызовы trust-имени `name(...)` переписываются в прямой вызов `native(...)` (см. m_nativeName).
    /// В отличие от forward-декларации `%name(...) := ...;`, здесь имя/аргументы могут отличаться.
    bool m_isNativeImport = false;

    /// C++-имя импортируемой нативной функции (без ведущего '%'), напр. "abs" или "std::sqrt".
    std::string m_nativeName;
};

/// JumpStmt - инструкция перехода (return / throw).
/// kind = ReturnStmt | ThrowStmt.
/// Синтаксис:
///   ++ [value] ++ ;     - return
///   -- [value] -- ;     - throw
///   label :: ++ ;       - return с меткой
/// Текст оператора (text()) и диапазон (range()) берутся из исходного Term.
class JumpStmt : public AstNodeAttr {
  public:
    JumpStmt() = default;

    /// text()/range() читаются из m_term - конструктор требует валидный Term.
    /// Роль (return/break/continue/throw) и метка определяются по TermID/m_text;
    /// при `ctx != nullptr` строит m_value из детей. Объявлен здесь, определён в ast_nodes.cpp.
    JumpStmt(ParserToken::Kind k, TermPtr term, Context* ctx = nullptr);

    explicit JumpStmt(ParserToken::Kind k)
    : AstNodeAttr(k) {}

    /// range: из m_term. Для синтетических jump-узлов (без Term, созданы ручным конструктором
    /// по kind) - невалиден (не маппятся), т.к. исходного trust-текста нет.
    [[nodiscard]] MapperRange range() const override { return m_term ? AstNodeAttr::range() : MapperRange{}; }

    [[nodiscard]] std::string dump(size_t indent = 0) const override;
    void lower(AstNodePtr& self, LowerCtx& ctx) override;

    AstNodePtr m_label{}; ///< Optional label (IdentName) before ::
    AstNodePtr m_value{}; ///< Optional return/throw value expression (nullptr = void)

    /// Определение функции, в которой находится этот return (для ReturnStmt). Заполняется
    /// семантическим анализатором (NameResolutionPass) для КАЖДОГО return: узел самодостаточен -
    /// транспилятор знает функцию (пост-условия, имя функции = возвращаемое значение) без
    /// отслеживания текущего контекста. nullptr для не-return jump'ов и ручных/тестовых узлов.
    const FuncDecl* m_funcDecl = nullptr;
};

/// VarDecl - объявление переменной.
/// kind = VarDecl.
/// text() - имя переменной (чистое, без префиксов).
/// Поддерживает:
///   x := expr        - text()="x", m_type=nullptr, m_initializer=expr
///   x:Type := expr   - text()="x", m_type=IdentType, m_initializer=expr
///   x := expr mut    - мутабельность выражается атрибутом узла (future)
class VarDecl : public Decl {
  public:
    VarDecl() { m_kind = ParserToken::Kind::VarDecl; }

    /// Терм-конструктор: имя читается из Term (у операторного терма `:=` - из m_left);
    /// тип/инициализатор задаются отдельно. Объявлен здесь, определён в ast_nodes.cpp.
    VarDecl(TermPtr term, AstNodePtr type = nullptr, AstNodePtr initializer = nullptr);

    /// Uniform term-constructor for the generated factory (kind = VarDecl).
    /// При ctx != nullptr строит m_type (из m_left->m_type) и m_initializer (из m_right).
    /// Объявлен здесь, определён в ast_nodes.cpp.
    VarDecl(ParserToken::Kind /*k*/, TermPtr term, Context* ctx = nullptr);

    /// Manual-конструктор: имя задано явно, без TermPtr; тип/инициализатор задаются отдельно.
    VarDecl(std::string name, AstNodePtr type = nullptr, AstNodePtr initializer = nullptr)
    : Decl(std::move(name))
    , m_initializer(std::move(initializer)) {
        m_kind = ParserToken::Kind::VarDecl;
        m_type = std::move(type);
    }

    [[nodiscard]] std::string dump(size_t indent = 0) const override;
    void lower(AstNodePtr& self, LowerCtx& ctx) override;

    /// range: [имя, инициализатор] - полный охват оператора `x := expr`. Вычисляется на лету
    /// из детей (m_left/m_right) без мутации Term. Если обеих сторон нет - базовый range.
    [[nodiscard]] MapperRange range() const override;

    /// Диапазон исходного имени переменной (для source-map/имени).
    /// Базовый m_term для `x := ...` - это терм оператора ':=' (CREATE_NAME):
    /// его m_mapperRange указывает на оператор, а реальное имя лежит в m_term->m_left.
    /// Возвращает диапазон m_term->m_left (имени) или невалидный, если имя не из исходника.
    [[nodiscard]] MapperRange nameRange() const noexcept;

    AstNodePtr m_initializer; ///< Инициализатор (nullptr = нет инициализатора)

    /// Выведенный анализатором конкретный тип (для нетипизированных `x := expr`, inferred).
    /// INVALID_TYPE_ID - тип не выведен. Заполняется NameResolutionPass (typeExpr) и читается
    /// транспилятором для кодогенерации (чтобы не зависеть от скоуп-стека, сброшенного после анализа).
    TypeId inferredType{INVALID_TYPE_ID};

    /// Узел декларации ТИПА (TypeDecl) для типизированных переменных доверенного типа
    /// (`x :MyInt := ...`). Ставит NameResolutionPass (analyzeVarDecl) для typeIsTrusted-типов;
    /// условия типа читаются из него как `m_typeDecl->m_trust` (источник один, без копий).
    /// Не владеющая ссылка в AST модуля - переживает таблицу символов. nullptr - нет/нетрастовый.
    const AstNodeBase* m_typeDecl = nullptr;
};

/// DestructureDecl - деструктуризация из коллекции/кортежа: `t1, ..., tN := ... source;` (spread,
/// коллекция: pop_front) или `t1, ..., tN := source;` (структурно: кортеж, std::get). Присваивание
/// в существующие цели - `t1, ..., tN = ... source;` (m_isAssign=true). Семантика: без маркера -
/// ТОЧНАЯ привязка (каждая цель - один элемент; кортеж - арность равна, спред - число целей ==
/// числу элементов для статически-известного размера). Суффикс `...` у имени цели (`rest...`,
/// C++-pack-стиль) - «остаток»: связывает оставшиеся элементы (спред - Dict, кортеж - под-кортеж);
/// `_...` - извлечь элементы, остаток отбросить; одиночный `_` - пропустить ровно один элемент.
/// kind = DestructureDecl.
class DestructureDecl : public AstNodeAttr {
  public:
    DestructureDecl() { m_kind = ParserToken::Kind::DestructureDecl; }

    /// Терм-конструктор: m_targets из цепочки m_left (список имён), m_source из m_right,
    /// m_isSpread - был ли RHS `... source`. rest-цель (`rest...`) - lval с суффиксом ELLIPSIS
    /// в m_right (грамматика assign_item: `lval ELLIPSIS`); m_targetIsRest[i] для неё = true.
    /// m_isAssign - true для `=` (присваивание в существующие цели), false для `:=` (объявление).
    /// m_targetTypeNodes[i] - явная аннотация типа цели (`a:Int32`; из lval->m_type), nullptr если нет.
    /// Определён в ast_nodes.cpp.
    DestructureDecl(ParserToken::Kind /*k*/, TermPtr term, Context* ctx = nullptr);

    [[nodiscard]] std::string dump(size_t indent = 0) const override;

    /// Полный охват оператора `t1, ..., tN := [... ]source;` - [первая цель (первый lval терма),
    /// конец источника]. Вычисляется на лету по терму (без его мутации), как VarDecl::range().
    /// Базовый m_term для `:=` - терм CREATE_NAME, чей m_mapperRange указывает на оператор `:=`;
    /// без переопределения маппинг оператора деструктуризации сужался бы до одного `:=`.
    [[nodiscard]] MapperRange range() const override;

    std::vector<AstNodePtr> m_targets; ///< Имена слева (Ident): [t1, t2, ..., tN] (или `_` = skip)
    std::vector<bool> m_targetIsRest;  ///< per-target: суффикс `...` (rest/«остаток»), параллельно m_targets
    /// per-target явная аннотация типа (`a:Int32`): узел типа (как в lval->m_type), nullptr если нет.
    std::vector<AstNodePtr> m_targetTypeNodes;
    /// per-target тип (для вывода типов целей spread): однородный тип элемента словаря (конкретный
    /// скаляр) → цель типизируется (кодген: any_cast<T>); INVALID → std::any. Параллельно m_targets.
    std::vector<TypeId> m_targetTypes;
    /// per-target тип ДЕКЛАРИРУЕМОЙ переменной (для словаря): явная аннотация (`a:Int32`) или
    /// выведенный natural runtime тип элемента. Отличается от m_targetTypes (тип any_cast = как хранит
    /// Dict, int → int64_t): кодген объявляет переменную этим типом, а any_cast<T> берёт из
    /// m_targetTypes (чтобы соответствовать хранению Dict). Для кортежа не используется.
    std::vector<TypeId> m_targetDeclaredTypes;
    AstNodePtr m_source;     ///< Источник (справа от `...`, коллекция/кортеж)
    bool m_isSpread = false; ///< true: `... source` (коллекция, pop); false: кортеж (std::get)
    bool m_isAssign = false; ///< true: `t1, ..., tN = source;` (присваивание в существующие цели)
    /// Статическая арность источника, вычисленная семантикой (кортеж - число элементов;
    /// спред-словарь - размер, если известен, иначе 0). Для кортеж-rest в кодогенерации
    /// (make_tuple оставшихся std::get<k>), т.к. скоуп-стек к моменту кодогенерации сброшен.
    size_t m_sourceArity = 0;
    /// Деструктуризация ВНУТРИ цикла (в стеке скоупов есть WhileStmt/DoWhileStmt). Семантика
    /// расширяет тип элемента до максимального (Integer/Double) - для foreach-паттерна; кодген
    /// использует runtime-конвертеры (anyToInt64/anyToDouble), а не строгий any_cast.
    bool m_inLoop = false;
};

/// ControlFlowStmt - общий базовый класс для операторов управления потоком
/// (if / while / do-while). Концентрирует общие поля m_cond (условие),
/// m_body (тело) и m_else (ветка else, nullptr если нет).
/// Производные добавляют специфичные поля (IfStmt::m_elseifs) и переопределяют dump().
class ControlFlowStmt : public AstNodeAttr {
  public:
    ControlFlowStmt() = default;

    ControlFlowStmt(ParserToken::Kind k, TermPtr term)
    : AstNodeAttr(k, std::move(term)) {}

    /// range: [min-begin, max-end] по всем детям (m_left/m_right/m_sequence) - полный охват
    /// statement'а. Вычисляется на лету (без мутации Term) в ast_nodes.cpp.
    [[nodiscard]] MapperRange range() const override;

    /// Дамп общих полей cond/body/else в порядке WhileStmt (cond → body → else).
    [[nodiscard]] std::string dumpControlFlow(size_t indent) const;

    AstNodePtr m_cond{}; ///< Условие
    AstNodePtr m_body{}; ///< Тело
    AstNodePtr m_else{}; ///< Ветка else (nullptr если нет)
};

/// IfStmt - условный оператор.
/// kind = IfStmt.
/// Единая раскладка из Term (parser.y): m_left=условие, m_right=else,
/// m_sequence=[тело then, branch2, branch3, ...] (branch_i = терм cond_i→body_i).
/// Здесь хранятся сконвертированные AST-дети: m_cond, m_body(=then), m_elseifs, m_else.
class IfStmt : public ControlFlowStmt {
  public:
    IfStmt() = default;

    IfStmt(ParserToken::Kind k, TermPtr term)
    : ControlFlowStmt(k, std::move(term)) {}

    /// Терм-конструктор: при `ctx != nullptr` сам строит детей (m_cond/m_body/m_elseifs/m_else)
    /// через TermToAstConverter (единая раскладка из parser.y). Объявлен здесь, определён в ast_nodes.cpp.
    IfStmt(ParserToken::Kind k, TermPtr term, Context* ctx);

    [[nodiscard]] std::string dump(size_t indent = 0) const override;
    void lower(AstNodePtr& self, LowerCtx& ctx) override;

    /// range: полный охват if-оператора - минимум begin по cond/телу, максимум end по
    /// телу/elseif/else (охват включает все ветки). Вычисляется на лету в ast_nodes.cpp.
    [[nodiscard]] MapperRange range() const override;

    std::vector<std::pair<AstNodePtr, AstNodePtr>> m_elseifs; ///< (условие, тело) для else-if
};

/// WhileStmt - цикл while с опциональным else.
/// kind = WhileStmt.
class WhileStmt : public ControlFlowStmt {
  public:
    WhileStmt() = default;

    WhileStmt(ParserToken::Kind k, TermPtr term)
    : ControlFlowStmt(k, std::move(term)) {}

    /// Терм-конструктор: при `ctx != nullptr` сам строит детей (m_cond/m_body/m_else)
    /// через TermToAstConverter. Объявлен здесь, определён в ast_nodes.cpp.
    WhileStmt(ParserToken::Kind k, TermPtr term, Context* ctx);

    [[nodiscard]] std::string dump(size_t indent = 0) const override;
    void lower(AstNodePtr& self, LowerCtx& ctx) override;
};

/// DoWhileStmt - цикл do-while.
/// kind = DoWhileStmt.
class DoWhileStmt : public ControlFlowStmt {
  public:
    DoWhileStmt() = default;

    DoWhileStmt(ParserToken::Kind k, TermPtr term)
    : ControlFlowStmt(k, std::move(term)) {}

    /// Терм-конструктор: при `ctx != nullptr` сам строит детей (m_body/m_cond)
    /// через TermToAstConverter (do-while: m_left=cond, m_sequence=[body]).
    /// Объявлен здесь, определён в ast_nodes.cpp.
    DoWhileStmt(ParserToken::Kind k, TermPtr term, Context* ctx);

    [[nodiscard]] std::string dump(size_t indent = 0) const override;
    void lower(AstNodePtr& self, LowerCtx& ctx) override;
};

/// MatchStmt - оператор сопоставления (match/switch).
/// kind = MatchingStmt.
/// Раскладка из Term (parser.y): m_left=значение, m_right=match_body (BLOCK);
/// m_right->m_sequence = [item1, item2, ..., elseItem]. Каждый item: m_left=шаблоны
/// (m_sequence = список паттернов), m_right=тело; else: m_left = ELLIPSIS.
class MatchStmt : public AstNodeAttr {
  public:
    struct MatchCase {
        std::vector<AstNodePtr> patterns; ///< Шаблоны ветки (объединяются через ||)
        AstNodePtr body{};                ///< Тело ветки
    };

    MatchStmt() = default;

    MatchStmt(ParserToken::Kind k, TermPtr term)
    : AstNodeAttr(k, std::move(term)) {}

    /// Терм-конструктор: при `ctx != nullptr` сам строит детей (m_value/m_cases/m_default)
    /// через TermToAstConverter (раскладка MATCHING-терма из parser.y).
    /// Объявлен здесь, определён в ast_nodes.cpp.
    MatchStmt(ParserToken::Kind k, TermPtr term, Context* ctx);

    [[nodiscard]] std::string dump(size_t indent = 0) const override;
    void lower(AstNodePtr& self, LowerCtx& ctx) override;

    /// range: [min-begin, max-end] по детям MATCHING-терма (m_left/m_right/m_sequence) - полный
    /// охват match-оператора. Вычисляется на лету (без мутации Term) в ast_nodes.cpp.
    [[nodiscard]] MapperRange range() const override;

    AstNodePtr m_value{};           ///< Выражение для сопоставления
    std::vector<MatchCase> m_cases; ///< Ветки (порядок важен)
    AstNodePtr m_default{};         ///< Тело else (nullptr если нет)
};

/// GotoStmt - безусловный переход к метке. СИНТЕТИЧЕСКИЙ узел: вставляется анализатором
/// (lowering) вместо именованных break/continue. Парсер его НЕ создаёт, Term отсутствует,
/// поэтому range хранится явно (берётся от исходного JumpStmt/блока).
/// LabelRef - СИНТЕТИЧЕСКИЙ узел lowering для работы с метками:
///   - kind=GotoStmt  → `goto <name>;`  (переход к метке);
///   - kind=LabelStmt → `<name>:;`       (определение метки).
/// Оба kinds несут ОДНИ и те же данные - имя метки (одна строка), поэтому используют ОДИН класс
/// (конвенция: разные kinds с одинаковой формой данных → один класс, см. ast/MEMORY.md).
/// Парсер его НЕ создаёт. kind задаётся при ручном создании.
class LabelRef : public AstNodeAttr {
  public:
    LabelRef() = default;

    /// Manual-конструктор (синтетический узел): kind (GotoStmt|LabelStmt) + имя метки.
    /// Range НЕ хранится: сопоставлять сгенерированные goto/метку с исходным текстом не нужно.
    LabelRef(ParserToken::Kind k, std::string name)
    : AstNodeAttr(k)
    , m_name(std::move(name)) {}

    /// Синтетический узел без Term и без source-range - range невалиден (не маппится).
    [[nodiscard]] MapperRange range() const noexcept override { return {}; }
    [[nodiscard]] std::string_view text() const noexcept override { return m_name; }

    [[nodiscard]] std::string dump(size_t indent = 0) const override;

    std::string m_name; ///< Имя метки (уже содержит суффикс _break/_continue)
};

/// SemicolonStmt - выражение в позиции оператора с завершающей точкой с запятой.
/// СИНТЕТИЧЕСКИЙ узел: вставляется анализатором (lowering) вокруг statement-позиции
/// выражений, чтобы решение о финальной ';' было явным в AST, а не выводилось
/// транспилятором из контекста. kind = SemicolonStmt.
class SemicolonStmt : public AstNodeAttr {
  public:
    SemicolonStmt() = default;

    /// Manual-конструктор (синтетический узел): оборачивает выражение-оператор.
    explicit SemicolonStmt(AstNodePtr expr)
    : AstNodeAttr(ParserToken::Kind::SemicolonStmt)
    , m_expr(std::move(expr)) {}

    /// range делегируется обёрнутому выражению (SemicolonStmt не имеет собственного Term).
    [[nodiscard]] MapperRange range() const noexcept override { return m_expr ? m_expr->range() : MapperRange{}; }

    /// text делегируется обёрнутому выражению (SemicolonStmt не имеет собственного Term).
    [[nodiscard]] std::string_view text() const noexcept override { return m_expr ? m_expr->text() : std::string_view{}; }

    [[nodiscard]] std::string dump(size_t indent = 0) const override;

    AstNodePtr m_expr{}; ///< Выражение-оператор (эмитится без скобок, затем ';')
};

/// TrustContract - единый узел trust-контракта (pre/post/assert/invariant/type).
/// Хранит СТРОГО ОДНО логическое выражение (m_expr), разобранное парсером как полноценное
/// логическое выражение (нетерминал `logical`). Тип контракта - в поле kind (PropertyKind);
/// может быть задан явно (`@{ kind: expr @}`) либо выведен из места привязки. Может находиться:
///   - в m_trust узла-объявления (функция, переменная, тип, цикл);
///   - автономным узлом в последовательности (assert в позиции выражения, `@{ expr @};`).
/// КОНТРАКТ: m_expr НЕ входит в children()/collectChildren (AstNodeBase по умолчанию пуст) -
/// анализатор и транспилятор полностью игнорируют узел и его содержимое.
class TrustContract : public AstNodeBase {
  public:
    TrustContract() { m_kind = ParserToken::Kind::TrustContract; }

    /// Терм-конструктор: выражение строится из term->m_right (грамматика
    /// `trust_contract: BEGIN [kind COLON] logical END`); kind резолвится из префикса
    /// `IDENT COLON` (см. ast_nodes.cpp). Объявлен здесь, определён в ast_nodes.cpp.
    TrustContract(ParserToken::Kind k, TermPtr term, Context* ctx = nullptr);

    /// Manual-конструктор: выражение задано явно, без TermPtr (test-only/синтетический).
    TrustContract(ParserToken::Kind k, AstNodePtr expr, PropertyKind kind = PropertyKind::kUnknown)
    : AstNodeBase(k)
    , m_expr(std::move(expr))
    , kind(kind) {}

    [[nodiscard]] std::string dump(size_t indent = 0) const override;

    AstNodePtr m_expr{};                        ///< Логическое выражение контракта (или nullptr для ручного пустого)
    PropertyKind kind = PropertyKind::kUnknown; ///< Тип контракта (kUnknown - автовывод из места)
};

/// TrustElem - узел термина решателя (SMT/Z3) внутри выражения trust-контракта:
/// `@( term, args... @)` → old/forall/exists/fresh/length/result. Первый аргумент
/// (маркер) резолвится в Z3TermKind::kind; остальные - в m_args. Для кванторов (Forall/Exists)
/// первый аргумент m_args - переменная-связка, последний - тело. НЕ входит в children().
class TrustElem : public AstNodeBase {
  public:
    TrustElem() { m_kind = ParserToken::Kind::TrustElem; }

    /// Терм-конструктор: kind резолвится из первого аргумента (маркера), остальные аргументы -
    /// в m_args. Объявлен здесь, определён в ast_nodes.cpp.
    TrustElem(ParserToken::Kind k, TermPtr term, Context* ctx = nullptr);

    /// Manual-конструктор (test-only/синтетический).
    TrustElem(ParserToken::Kind k, Z3TermKind term, std::vector<AstNodePtr> args)
    : AstNodeBase(k)
    , kind(term)
    , m_args(std::move(args)) {}

    [[nodiscard]] std::string dump(size_t indent = 0) const override;

    Z3TermKind kind = Z3TermKind::kUnknown; ///< Маркер термина решателя (резолвится из первого аргумента).
    std::vector<AstNodePtr> m_args;         ///< Аргументы (для forall/exists: [var, body]).
    /// Разрешённый тип переменной-связки квантора (`@( forall, i, P @)`): берётся из объявления
    /// `i` ранее (разрешение имён), НЕ выводится. Ставит семантика; solver читает его как сорт.
    /// INVALID_TYPE_ID - не установлен (ошибка на этапе semantic). Переживает таблицу символов.
    TypeId m_boundVarType = INVALID_TYPE_ID;
};

} // namespace trust