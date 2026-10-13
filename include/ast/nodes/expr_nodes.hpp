#pragma once
// include/ast/nodes/expr_nodes.hpp
// Expression/storage AST nodes: literals, context markers, Binary, CallExpr, ArgNode,
// Sequence and its value-like subtypes (dict/array/range), ScopeBlock, ModuleNode,
// plus the kind-classification helpers used by semantic and codegen.

#include "ast/token_base.hpp"
#include "ast/slots/slot_engine.hpp"
#include "ast/binary_op.hpp"
#include "location/namespace_alias.hpp"
#include "location/module_index.hpp"
#include "ast/token.hpp"
#include "syntax/check_area.hpp"
#include "ast/ident_name.hpp"
#include "ast/token_type.hpp"
#include "diag/severity.hpp"
#include "ast/trust_prop.hpp"
#include "ast/nodes/z3_term.hpp"
#include "types/type_id.hpp"
#include "utils/error.hpp"
#include "utils/import_masks.hpp"
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace trust {

class Context;

class Literal : public WithText<AstNodeAttr> {
  public:
    /// Слоты узла (единственный источник — ast/slots/node_slots_x.hpp).
    AST_SLOT_METHOD(AstNodeAttr, SLOTS_Literal)
    Literal() = default;

    /// Терм-конструктор: текст читается из Term (text() работает; range() из term).
    Literal(ParserToken::Kind k, TermPtr term)
    : WithText<AstNodeAttr>(k, term) {}

    /// Uniform term-constructor for the generated factory (ctx игнорируется - литерал лист).
    Literal(ParserToken::Kind k, TermPtr term, Context* /*ctx*/)
    : WithText<AstNodeAttr>(k, term) {}

    /// Manual-конструктор: текст задан явно, без TermPtr (range() вернёт invalid range).
    Literal(ParserToken::Kind k, std::string text)
    : WithText<AstNodeAttr>(k, std::string_view(text)) {}

    /// Класс литерала: IntLiteral | FloatLiteral | StrChar ("…", узкая строка) | StrWide ('…', широкая строка).
    /// Ширина строки задаётся Kind (вариант выбирается Kind), поле-флаг не нужен.

    /// Тип литерала (QualType), вычисленный семантикой (NameResolutionPass::typeExpr → единый
    /// решатель annotatedLiteralType / literalType) и кешированный на узле, чтобы транспилятор
    /// (литерал словаря → TypedValue{kind, значение}) не пересчитывал тип и не дублировал логику
    /// диапазонов литералов. Из identity получается kind (getTypeKind) и C++-имя (TypeSystem).
    /// Invalid QualType - тип не выведен.
    QualType typeState{};

    /// Постфиксная аннотация типа литерала `literal :Type` (напр. `0 :Bool`, `5 :Rational`,
    /// `5 :BigInteger`, `256 :Int16`). Переносится из term->m_type (грамматика
    /// `digits_literal type_item`) в term_to_ast; семантика задаёт литералу аннотированный
    /// тип (статическая типизация литералов). nullptr - аннотации нет.
    AstNodePtr typeAnnotation = nullptr;

    [[nodiscard]] std::string dump(size_t indent = 0) const override;
};

/// ContextMacro - контекстный макрос (@__NAMESPACE__, @::, @__FUNCTION__, @__FUNCSIG__,
/// @__FUNCDNAME__). Лист: хранит только text() - имя макроса, с возможными ведущими
/// маркерами стрингификации (@#/@#'/@#"), которых может быть несколько (см. семантику
/// контекст-макросов). kind = ContextMacro.
/// Раскрывается анализатором (заменяется на Literal(StrChar) или имя), поэтому
/// транспилятор НЕ обрабатывает такие узлы.
class ContextMacro : public WithText<AstNodeAttr> {
  public:
    ContextMacro() = default;

    /// Uniform term-constructor for the generated factory (ctx игнорируется - лист).
    ContextMacro(ParserToken::Kind k, TermPtr term, Context* /*ctx*/)
    : WithText<AstNodeAttr>(k, term) {}

    /// Manual-конструктор: текст задан явно, без TermPtr (range() вернёт invalid range).
    ContextMacro(ParserToken::Kind k, std::string text)
    : WithText<AstNodeAttr>(k, std::string_view(text)) {}

    [[nodiscard]] std::string dump(size_t indent = 0) const override;
};

/// CheckAreaStmt - встроенный контекст-маркер `@__CHECK_AREA__(<area> [, <behavior>]
/// [, <attr>...])`, встречающийся в теле макроса (на сайте раскрытия). Лист-маркер:
/// НЕ генерирует код; проверяется анализатором (NameResolutionPass) по текущей области,
/// выводимой из единого скоуп-стека (создатели скоупов), и затем УДАЛЯЕТСЯ
/// (заменяется пустым узлом). kind = CheckAreaStmt.
/// area - требуемая область; behavior - переопределение severity (empty/\"default\" ->
/// брать из -W<опции>); requiredAttrs - имена атрибутов, которыми должна обладать текущая область.
class CheckAreaStmt : public WithText<AstNodeAttr> {
  public:
    CheckAreaStmt() = default;

    /// Uniform term-constructor for the generated factory (ctx игнорируется - лист;
    /// поля-ограничения заполняются конвертером/семантикой, т.к. из Term напрямую не читаются).
    CheckAreaStmt(ParserToken::Kind k, TermPtr term, Context* /*ctx*/)
    : WithText<AstNodeAttr>(k, term) {}

    /// Manual-конструктор: текст задан явно, без TermPtr (range() вернёт invalid range).
    CheckAreaStmt(ParserToken::Kind k, std::string text)
    : WithText<AstNodeAttr>(k, std::string_view(text)) {}

    /// Требуемая область применения (первый аргумент макроса); nullopt - не задана.
    std::optional<AreaKind> area;
    /// Переопределение severity (второй аргумент). nullopt (Default) - дефолт из -W-опции
    /// (semantic::DiagId::CheckArea). Значение Severity (вкл. Severity::Ignore) - явный override:
    /// напр. Severity::Error ужесточает, Severity::Ignore подавляет независимо от -W.
    std::optional<Severity> behavior;
    // Требуемые атрибуты текущей области НЕ хранятся отдельным полем: они кладутся в собственный
    // список атрибутов узла (AstNodeAttr::add_attr / attrs()) - AttrId уже известны на момент
    // анализа (AttrPool). Семантика читает required-список как st->attrs() узла-маркера.

    [[nodiscard]] std::string dump(size_t indent = 0) const override;
};

/// Режим встроенного системного макроса отладочного вывода (DebugStmt).
enum class DebugMode {
    Filter, ///< @__DEBUG__(<masks>) / @__DEBUG__() - установить (пустой => сбросить) фильтр вывода
    Scope,  ///< @__DEBUG_SCOPE__(<keywords> [, <spec>]) - дамп состояния скоупа анализатора
};

/// DebugStmt - встроенный системный маркер отладочного вывода (УРОВЕНЬ 1): `@__DEBUG__` /
/// `@__DEBUG_SCOPE__`. Лист-маркер: НЕ генерирует код; анализатор применяет эффект
/// (устанавливает фильтр / печатает дамп скоупа) и УДАЛЯЕТ узел. kind = DebugStmt.
/// args: Filter - 0/1 элемент (маски); Scope - [keywords] или [keywords, spec].
class DebugStmt : public WithText<AstNodeAttr> {
  public:
    DebugStmt() = default;

    /// Uniform term-constructor (ctx игнорируется - лист; поля заполняет конвертер).
    DebugStmt(ParserToken::Kind k, TermPtr term, Context* /*ctx*/)
    : WithText<AstNodeAttr>(k, term) {}

    /// Manual-конструктор: текст задан явно, без TermPtr (range() вернёт invalid range).
    DebugStmt(ParserToken::Kind k, std::string text)
    : WithText<AstNodeAttr>(k, std::string_view(text)) {}

    DebugMode mode = DebugMode::Filter;
    /// Аргументы макроса (маски/опции) - интернированные view в пул строк компиляции:
    /// копий текста в узле нет (см. conversion в term_to_ast.cpp).
    std::vector<std::string_view> args;

    [[nodiscard]] std::string dump(size_t indent = 0) const override;
};

/// Binary - бинарная операция, member access или array access.
/// kind = TypeDecl | NameDecl | AssignOp | MathOp | BitwiseOp | CompareOp |
///        LogicalOp | MemberAccess | ArrayAccess | IncDecOp.
/// IncDecOp (`++`/`--`) унарный: операнд в m_left (постфикс) ИЛИ m_right (префикс);
/// конкретика в m_op (PreIncrement/PostIncrement/PreDecrement/PostDecrement).
/// Текст НЕ хранится: глиф оператора материализован в `m_op` (единственный источник для
/// потребителей: семантика/кодоген/дамп; для диагностики - `binaryOpSpelling(m_op)`).
class Binary : public AstNodeAttr {
  public:
    /// Слоты узла (единственный источник — ast/slots/node_slots_x.hpp).
    AST_SLOT_METHOD(AstNodeAttr, SLOTS_Binary)
    Binary() = default;

    /// Терм-конструктор: `m_op` заполняется из глифа term (конструктор требует валидный Term).
    /// При `ctx != nullptr` сам строит детей: m_left = TermToAstConverter(term->m_left),
    /// m_right = TermToAstConverter(term->m_right). Объявлен здесь, определён в ast_expr.cpp.
    Binary(ParserToken::Kind k, TermPtr term, Context* ctx = nullptr);

    /// Manual-конструктор с детьми (без TermPtr; range() вернёт invalid range).
    Binary(ParserToken::Kind k, AstNodePtr left, AstNodePtr right)
    : AstNodeAttr(k)
    , m_left(std::move(left))
    , m_right(std::move(right)) {}

    explicit Binary(ParserToken::Kind k)
    : AstNodeAttr(k) {}

    /// true для TypeDecl, построенного из `Name = <class_def>` (TermID::TYPE_DEFINE):
    /// определение ранее объявленного типа, а не создание нового (`::=`).
    [[nodiscard]] bool isTypeDefine() const noexcept { return m_isTypeDefine; }

    bool m_isTypeDefine = false; ///< cached TermID::TYPE_DEFINE (Term освобождается)

    /// true для TypeDecl, построенного из `::-` (TermID::ALIAS_TYPE): ВТОРОЕ имя существующего
    /// типа (тот же TypeId). Семантика различает создание (`::=`), определение (`=`) и алиас.
    [[nodiscard]] bool isTypeAlias() const noexcept { return m_isTypeAlias; }

    bool m_isTypeAlias = false; ///< cached TermID::ALIAS_TYPE (Term освобождается)

    [[nodiscard]] std::string dump(size_t indent = 0) const override;

    AstNodePtr m_left;
    AstNodePtr m_right;

    /// Индексы `a[i,j,...]` (INDEX-терм с m_args). Для обычного Binary пусто; для
    /// ArrayAccess по тензору хранит ВСЕ индексы (m_right дублирует первый для совместимости).
    std::vector<AstNodePtr> m_indices;

    /// Конкретный оператор узла (enum-opcode; аналог BinaryOperatorKind). Заполняется
    /// при Term->AST (Binary::Binary) из текста оператора (parseBinaryOp); None - вне
    /// покрытия (сравнение/логика/битовые). Потребители различают операцию только по m_op.
    BinaryOp m_op{BinaryOp::None};

    /// Семантические типы операндов и результата (заполняет NameResolutionPass::typeExpr;
    /// invalid QualType - не выведено). Используются транспилятором для тип-зависимой
    /// кодогенерации операторов (напр. std::any_cast для std::any-операндов).
    QualType lhsType{};
    QualType rhsType{};
    QualType resultType{};
    /// Общий тип операндов для any_cast: для арифметики = resultType; для Compare/Logical
    /// (результат Bool) - продвинутый тип конкретного операнда, если один из них std::any.
    QualType commonType{};
    /// Для MemberAccess/ArrayAccess по кортежу `t.name`/`t.0`: индекс элемента в TupleTypeData
    /// (резолвит семантика в resolveTupleAccess). -1 - не кортежный доступ (словарь/прочее).
    int64_t tupleIndex{-1};

    /// Для MemberAccess-ВЫЗОВА `obj.method(args)`: C++-суффикс ВЫБРАННОЙ семантикой перегрузки
    /// МЕТОДА (совпадает с `FuncDecl::m_cppSuffix` объявления; `FunctionIdentity::cppSuffix`).
    /// Заполняется для пользовательских Record-методов ВСЕГДА (имя метода = identity объявления);
    /// пусто - нативный/встроенный метод (имя фиксировано внешней библиотекой) или оператор.
    std::string resolvedMethodSuffix;

    /// Узел декларации ТИПА (TypeDecl) для присваивания в переменную доверенного типа
    /// (`x = ...`, где `x :MyInt`). Ставит NameResolutionPass (typeBinaryResult, AssignOp) для
    /// isTrustedType-целей; условия типа читаются из него как `m_typeDecl->m_trust`. Не владеющая
    /// ссылка в AST модуля - переживает таблицу символов. nullptr - нет/нетрастовый/не-AssignOp.
    const AstNodeBase* m_typeDecl = nullptr;
    /// Оператор сравнения типов (<~/~~/~~~): статический результат свёртки. `has_value()` —
    /// проверка выполнена (true/false); `nullopt` — не вычислимо (семантика уже сообщила ошибку,
    /// узел заменяется на ErrorExpr в analyzeNodeTail). ЕДИНСТВЕННОЕ поле оператора сравнения типов.
    std::optional<bool> m_typeCheckConst;
    /// Решённый СЕМАНТИКОЙ признак контролируемой арифметики для -foverflow-check:
    /// `nullopt` — узел вне класса (сравнение/логика/битовые/swap/append, `isOverflowCheckableOp`
    /// == false); `true` — знаковая машинная арифметика (+,-,* и +=,-=,*=) над знаковыми
    /// машинными целыми, проверяемая на переполнение; `false` — арифметика контролируемого класса,
    /// но проверка не ставится (unsigned wrap, BigInteger/Rational, не-целые операнды).
    /// Кодоген признак только читает и комбинирует с поведенческим флагом -foverflow-check.
    std::optional<bool> m_overflowCheck;

    /// Выбранная СЕМАНТИКОЙ перегрузка пользовательского оператора (`==`, `[]`, ...), если
    /// набор сигнатур неоднозначен по C++-разрешению (более одной). Кодоген эмитит explicit-форму
    /// с приведением операндов к объявленным типам параметров. INVALID — обычный вывод.
    TypeId resolvedOperatorSignature{INVALID_TYPE_ID};
    /// Форма выбранного оператора: member (неявный *this у LHS) или свободная функция.
    bool resolvedOperatorIsMember{false};
    /// Модуль и trust-namespace объявления СВОБОДНОГО пользовательского оператора:
    /// operator-функция не имеет идентификатора, поэтому изолируется namespace'ом модуля
    /// (`<enc>::<ns>::operator…`), а вызов эмитится квалифицированно.
    /// Member-форма — `kInvalid`/пусто (оператор уже внутри класса в namespace модуля).
    ModuleIndex resolvedOperatorModule = ModuleIndex::invalid();
    std::string_view resolvedOperatorNamespace;
    /// Материализованные СЕМАНТИКОЙ цели конверсий операндов для `emitOperatorDispatch`:
    /// member - один элемент (правый операнд), free - два (левый, правый). Совпадает с формой,
    /// которую читает кодоген; пусто - оператор не перегружен/не выбирался.
    std::vector<TypeId> argCasts;
};

/// Является ли kind «блочным» узлом (имеет собственный обход тела с отступами:
/// ScopeBlock / sequence / ModuleDecl). Используется в транспиляторе для решения,
/// выставлять ли отступ перед дочерним оператором (блочные узлы делают это сами).
/// Генерируется из PARSER_TOKEN_KINDS по node_type: ScopeBlock/ModuleNode, либо
/// kind==sequence (node_type Sequence; Attr - НЕ блочный, исключён явно).
[[nodiscard]] inline bool is_block_kind(ParserToken::Kind k) noexcept {
    switch (k) {
#define IS_BLOCK_KIND_CASE(name, node_type, category)                                            \
    case ParserToken::Kind::name:                                                                \
        return std::is_same_v<node_type, ScopeBlock> || std::is_same_v<node_type, ModuleNode> || \
               (std::is_same_v<node_type, Sequence> && ParserToken::Kind::name == ParserToken::Kind::sequence);
        PARSER_TOKEN_KINDS(IS_BLOCK_KIND_CASE)
#undef IS_BLOCK_KIND_CASE
    case ParserToken::Kind::END:
        return false;
    }
}

/// Является ли kind «типизируемым бинарным выражением» (результат типа выводится
/// пост-порядково анализатором). Единый источник набора Binary-kinds, участвующих в
/// типизации: MathOp | BitwiseOp | CompareOp | LogicalOp | NameDecl | AssignOp.
/// Используется в ExprFacts::exprType и NameResolutionPass::typeExpr.
[[nodiscard]] constexpr bool is_binary_expr_kind(ParserToken::Kind k) noexcept {
    return k == ParserToken::Kind::MathOp || k == ParserToken::Kind::BitwiseOp || k == ParserToken::Kind::CompareOp || k == ParserToken::Kind::LogicalOp ||
           k == ParserToken::Kind::NameDecl || k == ParserToken::Kind::AssignOp || k == ParserToken::Kind::AppendStmt || k == ParserToken::Kind::IncDecOp;
}

/// Является ли kind «statement-выражением» (в позиции оператора требует завершающей `;`).
/// Единый источник для lowering (оборачивание в SemicolonStmt) и capture-прохода `$^`.
[[nodiscard]] constexpr bool isExprStatement(ParserToken::Kind k) noexcept {
    return k == ParserToken::Kind::NameDecl || k == ParserToken::Kind::AssignOp || k == ParserToken::Kind::AppendStmt || k == ParserToken::Kind::MathOp ||
           k == ParserToken::Kind::IncDecOp || k == ParserToken::Kind::BitwiseOp || k == ParserToken::Kind::CompareOp || k == ParserToken::Kind::LogicalOp ||
           k == ParserToken::Kind::MemberAccess || k == ParserToken::Kind::ArrayAccess || k == ParserToken::Kind::CallExpr ||
           k == ParserToken::Kind::IntLiteral || k == ParserToken::Kind::FloatLiteral || k == ParserToken::Kind::StrChar || k == ParserToken::Kind::StrWide ||
           k == ParserToken::Kind::RationalLiteral;
}

/// Является ли kind литералом (константным значением). Единый источник набора литеральных
/// kinds: IntLiteral | FloatLiteral | StrChar | StrWide | RationalLiteral. Используется
/// типизацией литералов (type_inference.hpp), exprType/dictElementType (семантика) и
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
    /// Слоты узла (единственный источник — ast/slots/node_slots_x.hpp).
    AST_SLOT_METHOD(AstNodeAttr, SLOTS_CallExpr)
    CallExpr() = default;

    /// CallExpr текста не хранит (нет WithText в базе): callee-имя - в m_callee.
    CallExpr(ParserToken::Kind k, TermPtr term)
    : AstNodeAttr(k, term) {}

    /// Uniform term-constructor for the generated factory (kind = CallExpr).
    /// При ctx != nullptr строит детей: m_callee=IdentName(term), m_args=convertChildren(term).
    /// Объявлен здесь, определён в ast_expr.cpp.
    CallExpr(ParserToken::Kind k, TermPtr term, Context* ctx);

    /// Manual-конструктор с callee (без TermPtr; range() вернёт invalid range).
    CallExpr(ParserToken::Kind k, AstNodePtr callee)
    : AstNodeAttr(k)
    , m_callee(std::move(callee)) {}

    [[nodiscard]] std::string dump(size_t indent = 0) const override;

    AstNodePtr m_callee;                           ///< Вызываемое выражение
    std::optional<std::vector<AstNodePtr>> m_args; ///< Аргументы вызова

    /// Аргументы в ПОРЯДКЕ ПАРАМЕТРОВ после named/default binding (семантика). Не владеет:
    /// указывает на узлы `m_args`/`ArgNode::m_value` либо на default-выражения объявления.
    /// Кодоген использует этот список вместо `m_args`, если он задан.
    std::optional<std::vector<const AstNodeBase*>> boundArgs;

    /// Разрешённый тип результата конструктора record-шаблона (`Box(...)` → инстаннциация
    /// `Box<Int32>`): устанавливает СЕМАНТИКА по типу-цели контекста (объявление/присваивание/
    /// возврат/аргумент). INVALID_TYPE_ID - не конструктор шаблона/тип-цель неизвестен.
    TypeId resultType{INVALID_TYPE_ID};

    /// Type-state результата ВЫЗОВА (identity + const/inferred). Материализует семантика;
    /// авторитет для `exprState`. Не путать с `resultType` (тип-цель конструктора шаблона).
    QualType resultState{};

    /// Выбранная СЕМАНТИКОЙ сигнатура вызова (интернированный FunctionTypeData) — ЭФФЕКТИВНАЯ:
    /// типовые параметры подставлены, типовой пак развёрнут (единая семантика для generic и
    /// non-generic вызовов). Авторитетна для арности/типизации/static_cast. Имя C++-символа
    /// определяется ОБЪЯВЛЕННОЙ сигнатурой (resolvedCalleeSuffix), а не этим полем.
    /// INVALID_TYPE_ID - вызов не разрешён (ошибка уже диагностирована).
    TypeId resolvedSignature{INVALID_TYPE_ID};

    /// C++-суффикс выбранной перегрузки (тот же, что FuncDecl::m_cppSuffix у её объявления):
    /// считается от ОБЪЯВЛЕННОЙ сигнатуры (с `T`/паком), а не от resolvedSignature.
    /// Кодоген эмитит вызов `c_name<suffix>(args)` - однозначно, БЕЗ опоры на C++-разрешение
    /// (литералы/конверсии в C++ и в TrustLang могут дать разные перегрузки). Пусто - нативный
    /// вызов/оператор (имя фиксировано).
    std::string resolvedCalleeSuffix;

    /// Материализованные СЕМАНТИКОЙ цели неявных конверсий аргументов (в порядке `boundArgs`
    /// либо `m_args`, если binding не применялся). INVALID_TYPE_ID - приведения нет (Any/INVALID/
    /// вариадик без объявленного типа). Кодоген печатает приведение СТРОГО по этому списку и не
    /// пересчитывает его из `resolvedSignature`.
    std::vector<TypeId> argCasts;

    /// Нативное C++-имя с НЕСКОЛЬКИМИ сигнатурами: кодоген обязан привести аргументы к выбранной
    /// анализатором сигнатуре (иначе C++ выберет другую перегрузку). Для одиночной сигнатуры false -
    /// приведений нет (выбирать не из чего).
    bool resolvedNativeOverload = false;

    /// Выведенные типовые аргументы generic-вызова (в порядке объявления template-параметров).
    /// Пусто - вызов не generic. Кодоген Trust-функций эмитит явные `c_f<...>(...)`;
    /// native-import остаётся на C++ deduction (явные аргументы не эмитятся).
    std::vector<TypeId> resolvedTemplateArgs;

    /// Выбранная перегрузка пользовательского оператора `()` (member). Заполняется ТОЛЬКО при
    /// наборе >1; кодоген эмитит `(obj)(static_cast<...>(args...))`. INVALID — обычный вывод.
    TypeId resolvedOperatorSignature{INVALID_TYPE_ID};
};

/// ArgNode - ЕДИНЫЙ узел позиции списка аргументов: параметр функции, элемент словаря/enum/variant,
/// аргумент вызова. kind = ArgNode.
/// text() - имя (простая строка, не IdentName); "" - безымянный (позиционный аргумент/значение).
/// Голое значение `42` - это ArgNode{name="", value=42}. Явный тип `name:Type` - в m_type;
/// значение (после '=') - в m_value; разрешённый тип значения - в resultType (ставит семантика).
class ArgNode : public WithText<AstNodeAttr> {
  public:
    /// Слоты узла (единственный источник — ast/slots/node_slots_x.hpp).
    AST_SLOT_METHOD(AstNodeAttr, SLOTS_ArgNode)
    ArgNode() = default;

    /// Терм-конструктор: имя параметра читается из Term (у ARGUMENT-терма - из m_left);
    /// тип/значение задаются отдельно. Объявлен здесь, определён в ast_base.cpp.
    ArgNode(TermPtr term, AstNodePtr type = nullptr, AstNodePtr value = nullptr, AttrPool* pool = nullptr);

    /// Uniform term-constructor for the generated factory (kind = ArgNode).
    /// При ctx != nullptr строит m_type из term. Объявлен здесь, определён в ast_base.cpp.
    ArgNode(ParserToken::Kind /*k*/, TermPtr term, Context* ctx = nullptr);

    /// Manual-конструктор: имя задано явно, без TermPtr; тип/значение задаются отдельно.
    ArgNode(std::string name, AstNodePtr type = nullptr, AstNodePtr value = nullptr, AttrPool* pool = nullptr)
    : WithText<AstNodeAttr>(ParserToken::Kind::ArgNode)
    , m_type(std::move(type))
    , m_value(std::move(value)) {
        const std::string_view raw = name;
        EXPECT((pool != nullptr || raw.empty() || raw.back() != '^' || raw == "$^") &&
               "manual ArgNode name with '^' requires AttrPool to apply attr::ReadOnly");
        assignNameFromRaw(*this, *this, ParserToken::Kind::ArgNode, raw, pool);
    }

    [[nodiscard]] std::string dump(size_t indent = 0) const override;

    AstNodePtr m_type{nullptr};  ///< Явный тип (name:Type; nullptr - нет)
    AstNodePtr m_value{nullptr}; ///< Значение (после '='; nullptr - безнарный)
    QualType resultType{};       ///< Разрешённый тип значения (ставит семантика)
    /// Объявленный узел, к которому разрешён захват лямбды (владелец внешней переменной).
    /// Невладеющая ссылка; nullptr для параметров/аргументов вызова/неразрешённых имён.
    const AstNodeBase* resolvedDecl = nullptr;
    /// Типовой пакет `<T...>` (template_param) или pack-параметр `xs: T...` (arg).
    /// `m_type` при этом указывает на паттерн T; pack-expansion в типах кодируется
    /// структурным TypeId PackExpansion(T) (types/type_system.hpp).
    bool isTypePack{false};
};

/// Sequence - узел с телом (последовательностью дочерних токенов) - ВЕРХНИЙ уровень файла
/// (kind = sequence | Attr). Области имён здесь НЕТ: текст у Sequence отсутствует; именованные
/// области (`Block` = ScopeBlock, ModuleNode) несут имя через собственный `WithText<Sequence>`.
class Sequence : public AstNodeAttr {
  public:
    /// Слоты узла (единственный источник — ast/slots/node_slots_x.hpp).
    AST_SLOT_METHOD(AstNodeAttr, SLOTS_Sequence)
    Sequence() = default;

    /// Терм-конструктор: при `ctx != nullptr` сам строит m_body через TermToAstConverter.
    /// Объявлен здесь, определён в ast_base.cpp.
    Sequence(ParserToken::Kind k, TermPtr term, Context* ctx = nullptr);

    /// Kind-only (контейнер без имени, manual/synthetic).
    explicit Sequence(ParserToken::Kind k)
    : AstNodeAttr(k) {}

    [[nodiscard]] std::string dump(size_t indent = 0) const override;

    /// Утилита для дампа содержимого списка узлов (переиспользуется в ScopeBlock, ModuleNode)
    static void dumpBody(std::string& result, const std::vector<AstNodePtr>& body, size_t indent, size_t child_indent);

    [[nodiscard]] Sequence* as_sequence() noexcept override { return this; }
    [[nodiscard]] const Sequence* as_sequence() const noexcept override { return this; }

    std::vector<AstNodePtr> m_body; ///< Тело/содержимое узла
};

/// RefTakeExpr - узел оператора `*` (разыменование/доступ к данным). kind = RefTakeExpr.
/// Двойная роль по позиции:
///   - сигл ТИПА `x : *Int32` (unique_ptr) - грамматика `COLON STAR NAME` → TAKE; операнд - pointee-тип;
///   - ВЫРАЖЕНИЕ `*ref` - разыменование ссылочного операнда: прямой доступ к данным (semantic
///     std::reference_wrapper / std::unique_ptr::operator*), для shared/weak - через lock().
/// Наследует Sequence: m_body = [pointee-тип] в позиции типа или [операнд] в выражении.
/// Вид ссылки операнда (m_opRefKind) ставит семантика (typeExpr) для кодогенерации: транспилятору
/// локальные символы недоступны (скоуп-стек сброшен), поэтому он не может сам различить
/// `*c_x.lock()` (shared/weak) от `*c_u` (unique/ptr).
class RefTakeExpr : public Sequence {
  public:
    using Sequence::Sequence;

    /// Вид ссылки операнда `*ref` в выражении (для кодогенерации): kShared/kWeak → `*(op.lock())`,
    /// kUnique/kPtr → прямой доступ. Для сигла ТИПА - не используется (INVALID-эквивалент).
    RefType m_opRefKind = RefType::kValue;

    /// Lowering доступа `*ref`, РЕШЁННЫЙ АНАЛИЗАТОРОМ (кодоген — буквальный перевод, без обращения
    /// к таблице символов): вид владельца определяет форму доступа.
    enum class TakeLowering : std::uint8_t {
        Direct,       ///< ptr: `trust::checked_deref(op)`
        StaticUnique, ///< unique без deleter'а (StaticUnique, inline): `(op.get())`
        UniqueDirect, ///< unique с deleter'ом (trust::Unique<T,D>): `trust::checked_deref(op.get())`
        SharedLocked, ///< shared/weak: `*(op.lock())` / `*(op.lock_const())`
        UserOperator, ///< record/native/native-template: `(*op)` (объявленный operator*)
    };
    TakeLowering m_lowering = TakeLowering::Direct;

    /// Выбранная перегрузка пользовательского `*` (набор >1): кодоген эмитит explicit member/free
    /// вызов с приведением операнда. INVALID — обычный вывод `(*op)`.
    TypeId resolvedOperatorSignature{INVALID_TYPE_ID};
    bool resolvedOperatorIsMember{false};
    /// Модуль/trust-namespace объявления СВОБОДНОГО `operator*`: квалифицированный вызов в
    /// namespace модуля (см. Binary::resolvedOperatorModule). Member-форма — invalid/пусто.
    ModuleIndex resolvedOperatorModule = ModuleIndex::invalid();
    std::string_view resolvedOperatorNamespace;

    /// Type-state результата `*ref` (тип pointee); материализует семантика.
    QualType m_resultType{};
};

/// RefMakeExpr - узел ссылочного оператора `&` (сигл в позиции ТИПА `x : &Int32`/`&?Int32` ИЛИ
/// address-of в выражении `& shared_var`). kind = RefMakeExpr.
/// Наследует Sequence: m_body = [pointee-тип] в позиции типа (грамматика `COLON ptr NAME`,
/// `ptr` привязывает тип справа) или [операнд] в выражении. Вид сигла - `m_sigilKind`.
/// В выражении результат - слабая ссылка (weak) из shared-переменной; тип результата резолвит
/// семантика (typeExpr) и сохраняет в m_resultType для кодогенерации (локальные символы в
/// транспиляторе недоступны - скоуп-стек сброшен к глобальному).
/// `&` у монопольного `unique` ЗАПРЕЩЁН (нарушает монополию: допустимы только move/swap).
class RefMakeExpr : public Sequence {
  public:
    using Sequence::Sequence;

    /// Вид ссылки из СИМВОЛЬНОГО маркера аннотации типа (`&&`/`&*`/`&?`).
    /// `RefType::kValue` - маркер нативного вида (`&`) или вид не задан (тот же приём, что
    /// `RefTakeExpr::m_opRefKind`). Заполняет конвертер Term->AST.
    RefType m_sigilKind{RefType::kValue};

    /// Тип результата `& expr` (weak-ссылка из shared-переменной), вычисленный семантикой.
    /// Для позиции типа (сигл в аннотации) - INVALID (там тип выводит resolveTypeRef по pointee).
    QualType m_resultType{};
};

/// DictLiteral - литерал словаря/набора элементов (и типизированная конструкция/кортеж).
/// kind = DictLiteral (механический, ставит term_to_ast) | Tuple (уточняется анализатором).
/// Наследует Sequence: m_body = элементы, нормализованные к Binary(AssignOp)
/// (left=имя или пусто, right=значение), см. term_to_ast (visit_DICT).
///
/// Поле m_type - аннотация типа литерала/конструкции (`(args):Type` / `:Type(args)`),
/// резолвится анализатором через TypeSystem. По резолвленному типу анализатор решает
/// класс узла: если тип - Tuple (type_category::Tuple) - kind меняется на Tuple, а тип
/// выражения - на интернированный структурный кортеж (getOrCreateTupleType). Для любого
/// другого типа это типизированная конструкция/каст (решение в кодогенерации). m_type==nullptr
/// - обычный словарь (trust::Dict). Никаких строковых дискриминаторов ("Tuple") нет.
class DictLiteralNode : public Sequence {
  public:
    /// Слоты узла (единственный источник — ast/slots/node_slots_x.hpp).
    AST_SLOT_METHOD(Sequence, SLOTS_DictLiteralNode)
    using Sequence::Sequence;

    /// Аннотация типа литерала/конструкции (см. комментарий класса). nullptr - нет аннотации.
    AstNodePtr m_type;

    /// true - префиксная форма `:Type(args)` (type-call/конструкция; голые аргументы - значения).
    /// false - постфиксная форма `(args):Type` (типизированный литерал). Различает объявление
    /// enum/variant (постфикс) от type-call (префикс) в analyzeTypeDecl/analyzeDictLiteral.
    bool prefix = false;

    /// Для kind==ArrayInit (литерал `[...]`): интернированный структурный массив `Array<Elem>`
    /// (тип выражения), вычисленный семантикой (analyzeArrayInit). Для DictLiteral/Tuple - INVALID.
    QualType arrayType{};

    /// Для kind==ArrayInit/DictLiteral, когда цель/аннотация - тензор: интернированный
    /// структурный `Tensor<Elem, shape>`; кодогенерация эмитит `trust::Tensor<Elem>`.
    QualType tensorType{};

    /// Для конструкции массива `:Array^(...):Elem` - аннотация типа ЭЛЕМЕНТА (хвостовая `:Elem`).
    /// В литерале `[1,2,3,]:Elem` элементная аннотация лежит в m_type (visit_TENSOR). Здесь -
    /// префиксная форма `:Type(...):Elem`, где m_type занят контейнером (`:Array`).
    AstNodePtr arrayElementAnnotation;

    /// Для одиночной типизированной конструкции/каста (`:T(x)` / `(x):T`): тип ОПЕРАНДА,
    /// вычисленный семантикой. Кодогенерация по нему выбирает runtime-конверсию:
    /// Any (`std::any`/`TypedValue`) -> `trust::any_to<T>`, иначе `trust::checked_cast<T>`.
    /// INVALID - не одиночная конструкция либо тип операнда неизвестен.
    QualType castSourceType{};

    /// Type-state ВСЕГО выражения литерала/конструкции (identity + const/inferred).
    /// Материализует семантика; авторитет для `exprState` (для ArrayInit/Tensor
    /// конкретный контейнерный тип берётся из `arrayType`/`tensorType`).
    QualType resultState{};
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
    /// Слоты узла (единственный источник — ast/slots/node_slots_x.hpp).
    AST_SLOT_METHOD(Sequence, SLOTS_RangeExpr)
    using Sequence::Sequence;

    /// Manual-конструктор для тестов/codegen: 2 операнда (start, stop), без шага.
    RangeExpr(ParserToken::Kind k, AstNodePtr start, AstNodePtr stop)
    : Sequence(k) {
        m_body.push_back(std::move(start));
        m_body.push_back(std::move(stop));
    }

    /// Manual-конструктор: 3 операнда (start, stop, step).
    RangeExpr(ParserToken::Kind k, AstNodePtr start, AstNodePtr stop, AstNodePtr step)
    : Sequence(k) {
        m_body.push_back(std::move(start));
        m_body.push_back(std::move(stop));
        m_body.push_back(std::move(step));
    }

    /// Тип элемента диапазона (join start/stop/step), INVALID_TYPE_ID - не выведен.
    QualType elementType{};

    /// Type-state ВСЕГО выражения диапазона (`Range<Elem>`); материализует семантика.
    QualType resultState{};

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

/// ScopeBlock - Block с меткой/именем области (ЕДИНСТВЕННЫЙ носитель имени среди контейнеров,
/// вместе с именованным модульным блоком ModuleNode).
/// kind = ScopeBlock.
/// Наследует Sequence (+m_body) и WithText (имя области).
/// Имя блока хранится в text():
///   ""       - блок без метки { ... } (анонимный)
///   "_"      - скрытая область реализации (имена из нее не эспортируются) _ { ... }
///   "name"   - именованная область name { ... }
///   "::" - глобальная область имен :: { ... }
///   "::ns::name" - глобальная именованная область имен ::ns::name { ... }
///   "ns::name::" - именованная область имен ns::name:: { ... }
/// Поле m_blockCounter - глобально уникальный идентификатор блока,
/// автоматически присваиваемый из Context::nextBlockCounter().
class ScopeBlock : public WithText<Sequence> {
  public:
    ScopeBlock() = default;

    /// Терм-конструктор: метка блока (имя области) читается из Term.
    /// При `ctx != nullptr` сам строит m_body (делегирует Sequence-конструктору).
    /// Объявлен здесь, определён в ast_base.cpp.
    ScopeBlock(TermPtr term, Context* ctx = nullptr, int blockCounter = 0);

    /// Uniform term-constructor for the generated factory (kind всегда ScopeBlock).
    ScopeBlock(ParserToken::Kind /*k*/, TermPtr term, Context* ctx = nullptr)
    : ScopeBlock(term, ctx) {}

    /// Manual-конструктор: метка задана явно, без TermPtr.
    ScopeBlock(std::string name, int blockCounter = 0)
    : WithText<Sequence>(ParserToken::Kind::ScopeBlock, std::string_view(name))
    , m_blockCounter(blockCounter) {}

    /// Блок без пользовательской метки (анонимный `{ ... }`). Парсер хранит text() = "{"
    /// (терм LBRACE), а не пустую строку (см. документированный инвариант «пустой text =
    /// анонимный»), поэтому анонимным считается пустой text или ровно "{". Именованные
    /// области ("name::", "::ns::name") и скрытая "_" - не анонимные.
    [[nodiscard]] bool is_anonymous() const noexcept { return text().empty() || text() == "{"; }
    /// `__::`/`__` — private: только текущий файл.
    [[nodiscard]] bool is_private() const noexcept { return text() == "__" || text() == "__::"; }
    /// `_::`/`_` — protected: файлы той же группы/пакета.
    [[nodiscard]] bool is_protected() const noexcept { return text() == "_" || text() == "_::"; }
    /// Любая непубличная область (`_`/`__`); для проверок, где конкретный уровень не важен.
    [[nodiscard]] bool is_hidden() const noexcept { return is_private() || is_protected(); }
    /// Блок интерфейса файла (`\path { ... };`): container уровня модуля. Объявления внутри
    /// регистрируются в корневом скоупе и экспортируются (как `::`), но namespace-сегментом не
    /// являются. Флаг ставит загрузчик после разбора заголовка.
    [[nodiscard]] bool isModuleInterface() const noexcept { return m_isModuleInterface; }
    void setModuleInterface() noexcept { m_isModuleInterface = true; }
    [[nodiscard]] std::string_view name() const noexcept { return text(); }
    [[nodiscard]] int blockCounter() const noexcept { return m_blockCounter; }

    [[nodiscard]] std::string dump(size_t indent = 0) const override;

  private:
    int m_blockCounter{0};           ///< Глобально уникальный идентификатор блока
    bool m_isModuleInterface{false}; ///< Блок интерфейса файла (см. isModuleInterface)
};

/// ModuleNode - узел AST, представляющий загруженный модуль.
/// Проекция сайта импорта на одну файловую часть модуля: отфильтрованный масками экспорт,
/// экспортированные namespace-алиасы и несовпавшие маски. Один объект на пару `(модуль, маски)`,
/// разделяется всеми сайтами с теми же масками (владелец — кэш загрузчика).
struct SiteProjection {
    std::shared_ptr<const std::vector<AstNodePtr>> exports;
    std::shared_ptr<const std::vector<ModuleNamespaceAlias>> namespaceAliases;
    /// Маски, не совпавшие ни с одним публичным именем части (диагностика `-Wunmatched-mask`).
    std::vector<std::string> unmatchedMasks;
    /// Отдельно — protected-набор (`_::`): его имена связываются только импортёром той же
    /// группы/пакета, поэтому непопадание считается по ДОСТУПНЫМ частям.
    std::vector<std::string> protectedUnmatchedMasks;
    bool hasProtectedExports = false;
};

/// Именованная модульная область: наследует Sequence (m_body) и WithText (module id).
/// Хранит индекс записи в ModuleRegistry.
/// Имя модуля доступно через text()/importSpelling().
/// kind = ModuleDecl.
class ModuleNode : public WithText<Sequence> {
  public:
    /// Одна файловая часть сайта импорта: индекс модуля + РАЗДЕЛЯЕМОЕ тело (владелец — реестр;
    /// `shared_ptr` даёт lifetime-независимость AST от `Pipeline`) + проекция сайта.
    struct ModulePart {
        ModuleIndex index{};
        /// Тело модуля: тот же вектор узлов, что у реестра (копий списка нет). Элементы может
        /// заменять ТОЛЬКО семантика (маркеры раскрытия; тело анализируется один раз на модуль).
        std::shared_ptr<std::vector<AstNodePtr>> body;
        /// Всё, что видно с этого сайта в этой части (владелец — кэш загрузчика).
        std::shared_ptr<const SiteProjection> projection;
    };

    ModuleNode() = default;

    /// Терм-конструктор: имя модуля читается из Term. `isImport` — сайт импорта
    /// (`\module(...)`), а не корневой модуль главного файла.
    ModuleNode(std::vector<ModuleIndex> moduleIndices, TermPtr term, bool isImport = false)
    : WithText<Sequence>(ParserToken::Kind::ModuleDecl, term)
    , m_moduleIndices(std::move(moduleIndices))
    , m_isImport(isImport) {}

    /// Manual-конструктор: имя задано явно, без TermPtr (range() вернёт invalid range).
    ModuleNode(std::vector<ModuleIndex> moduleIndices, std::string name)
    : WithText<Sequence>(ParserToken::Kind::ModuleDecl, std::string_view(name))
    , m_moduleIndices(std::move(moduleIndices)) {}

    /// Удобные конструкторы для одиночного модуля.
    ModuleNode(ModuleIndex moduleIndex, TermPtr term, bool isImport = false)
    : ModuleNode(std::vector<ModuleIndex>{moduleIndex}, std::move(term), isImport) {}

    ModuleNode(ModuleIndex moduleIndex, std::string name)
    : ModuleNode(std::vector<ModuleIndex>{moduleIndex}, std::move(name)) {}

    [[nodiscard]] const std::vector<ModuleIndex>& moduleIndices() const noexcept { return m_moduleIndices; }
    /// Первый индекс (для одиночного импорта/корневого модуля).
    [[nodiscard]] ModuleIndex moduleIndex() const {
        EXPECT(!m_moduleIndices.empty() && "ModuleNode has no module indices");
        return m_moduleIndices.front();
    }
    /// Spelling сайта импорта (`\dir\mod`): домен ИМЕНИ, а не идентичности (`moduleIndex`)
    /// и не пути ФС (`ModuleLoader::modulePath`).
    [[nodiscard]] std::string_view importSpelling() const noexcept { return text(); }

    /// Файловые части сайта: индекс + диапазон тела + экспорт/алиасы. Для одиночного
    /// импорта одна часть, для группового — несколько.
    [[nodiscard]] std::vector<ModulePart>& parts() noexcept { return m_parts; }
    [[nodiscard]] const std::vector<ModulePart>& parts() const noexcept { return m_parts; }

    /// Сайт импорта (`\module(mod, masks)`), а не корневой модуль главного файла.
    /// Устанавливается в терм-конструкторе (visit_MODULE). Для корня - false.
    [[nodiscard]] bool isImport() const noexcept { return m_isImport; }

    /// Сайт импорта внутри БЛОКА ИНТЕРФЕЙСА файла: его (отфильтрованный масками) набор имён
    /// входит в экспорт-набор ЭТОГО файла — реэкспорт. Ставит загрузчик
    /// (`installFileInterface`); владелец имён не меняется (`node->module`).
    [[nodiscard]] bool isReExport() const noexcept { return m_isReExport; }
    void setReExport() noexcept { m_isReExport = true; }

    /// Разобранные маски сайта: `utils::ImportMasks` - единственный парсер, поэтому
    /// строка разбирается ОДИН раз (при установке), а потребители читают готовый набор.
    /// Пустой набор = импортировать все экспорты.
    [[nodiscard]] const utils::ImportMasks& importMaskSet() const noexcept { return m_importMaskSet; }
    void setImportMasks(std::string masks) { m_importMaskSet = utils::ImportMasks::parse(std::move(masks)); }

    [[nodiscard]] std::string dump(size_t indent = 0) const override;

  private:
    std::vector<ModuleIndex> m_moduleIndices; ///< Индексы записей в ModuleRegistry (1 для обычного импорта)
    std::vector<ModulePart> m_parts;          ///< Файловые части сайта (разделяемое тело/экспорт/алиасы)
    bool m_isImport{false};                   ///< true - сайт импорта `\module(...)`, а не корень
    bool m_isReExport{false};                 ///< сайт импорта внутри блока интерфейса (реэкспорт)
    utils::ImportMasks m_importMaskSet;       ///< Разобранные маски (единственный парсер)
};

/// ТИПИЗИРОВАННЫЕ классы для kinds группы AstNodeAttr/Sequence.
/// Каждый kind получает детерминированный класс (one kind → one class). Все - наследники
/// Sequence, чтобы нести дочерние узлы (поля/тело) в унаследованном m_body.
/// Правило конструкторов: терм-конструктор (Kind, TermPtr) + manual (Kind, text) без TermPtr.

} // namespace trust
