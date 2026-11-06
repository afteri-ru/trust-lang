#pragma once

#include "ast/token_base.hpp"
#include "ast/slots/slot_engine.hpp"
#include "ast/binary_op.hpp"
#include "ast/token.hpp"
#include "syntax/check_area.hpp"
#include "ast/ident_name.hpp"
#include "ast/token_type.hpp"
#include "diag/severity.hpp"
#include "ast/trust_prop.hpp"
#include "ast/nodes/z3_term.hpp"
#include "types/type_id.hpp"
#include "utils/error.hpp"
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>
#include "ast/nodes/expr_nodes.hpp"

namespace trust {

struct ConvertServices;

/// JumpStmt - инструкция перехода (return / throw).
/// kind = ReturnStmt | ThrowStmt.
/// Синтаксис:
///   ++ [value] ++ ;     - return
///   -- [value] -- ;     - throw
///   label :: ++ ;       - return с меткой
/// Диапазон (range()) берётся из исходного Term; собственного текста узел не хранит.
class JumpStmt : public AstNodeAttr {
  public:
    AST_SLOT_METHOD(AstNodeAttr, SLOTS_JumpStmt)
    JumpStmt() = default;

    /// range() читается из term (конструктор требует валидный Term); текста узел не хранит.
    /// Роль (return/break/continue/throw) и метка определяются по TermID/тексту;
    /// при `ctx != nullptr` строит m_value из детей. Объявлен здесь, определён в ast_stmt.cpp.
    JumpStmt(ParserToken::Kind k, TermPtr term, ConvertServices* ctx = nullptr);

    explicit JumpStmt(ParserToken::Kind k)
    : AstNodeAttr(k) {}

    /// range: из term. Для синтетических jump-узлов (без Term, созданы ручным конструктором
    /// по kind) - невалиден (не маппятся), т.к. исходного trust-текста нет.

    [[nodiscard]] std::string dump(size_t indent = 0) const override;

    AstNodePtr m_label{}; ///< Optional label (IdentName) before ::
    AstNodePtr m_value{}; ///< Optional return/throw value expression (nullptr = void)

    /// Определение функции, в которой находится этот return (для ReturnStmt). Заполняется
    /// семантическим анализатором (NameResolutionPass) для КАЖДОГО return: узел самодостаточен -
    /// транспилятор знает функцию (пост-условия, имя функции = возвращаемое значение) без
    /// отслеживания текущего контекста. nullptr для не-return jump'ов и ручных/тестовых узлов.
    const FuncDecl* m_funcDecl = nullptr;

    /// Синтезированная СЕМАНТИКОЙ временная const-переменная `__trust_res_N := <m_value>;`
    /// (hoist возвращаемого значения для пост-условий: выражение вычисляется один раз, имя
    /// функции связывается со значением). Создаётся только для ИМЕНОВАННОГО return (m_label) из
    /// функции с пост-условиями. Семантика выводит её тип в VarDecl::inferredType
    /// (exprType(*m_value)); транспилятор эмитит её как обычный VarDecl и читает имя.
    /// Инвариант: временные создаёт анализатор, транспилятор их не синтезирует. nullptr - не синтезирована.
    AstNodePtr m_tempDecl{};
};

/// ControlFlowStmt - общий базовый класс для операторов управления потоком
/// (if / while / do-while). Концентрирует общие поля m_cond (условие),
/// m_body (тело) и m_else (ветка else, nullptr если нет).
/// Производные добавляют специфичные поля (IfStmt::m_elseifs) и переопределяют dump().
class ControlFlowStmt : public AstNodeAttr {
  public:
    AST_SLOT_METHOD(AstNodeAttr, SLOTS_ControlFlowStmt)
    ControlFlowStmt() = default;

    ControlFlowStmt(ParserToken::Kind k, TermPtr term)
    : AstNodeAttr(k, term) {}

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
    AST_SLOT_METHOD(ControlFlowStmt, SLOTS_IfStmt)
    IfStmt() = default;

    IfStmt(ParserToken::Kind k, TermPtr term)
    : ControlFlowStmt(k, term) {}

    /// Терм-конструктор: при `ctx != nullptr` сам строит детей (m_cond/m_body/m_elseifs/m_else)
    /// через TermToAstConverter (единая раскладка из parser.y). Объявлен здесь, определён в ast_stmt.cpp.
    IfStmt(ParserToken::Kind k, TermPtr term, ConvertServices* ctx);

    [[nodiscard]] std::string dump(size_t indent = 0) const override;

    std::vector<std::pair<AstNodePtr, AstNodePtr>> m_elseifs; ///< (условие, тело) для else-if
};

/// WhileStmt - цикл while с опциональным else.
/// kind = WhileStmt.
class WhileStmt : public ControlFlowStmt {
  public:
    AST_SLOT_METHOD(ControlFlowStmt, SLOTS_WhileStmt)
    WhileStmt() = default;

    WhileStmt(ParserToken::Kind k, TermPtr term)
    : ControlFlowStmt(k, term) {}

    /// Терм-конструктор: при `ctx != nullptr` сам строит детей (m_cond/m_body/m_else)
    /// через TermToAstConverter. Объявлен здесь, определён в ast_stmt.cpp.
    WhileStmt(ParserToken::Kind k, TermPtr term, ConvertServices* ctx);

    [[nodiscard]] std::string dump(size_t indent = 0) const override;

    /// Синтезированный lowering флаг while-else `bool _weN := 0;` (в C++ нет while...else; else
    /// эмулируется флагом «вошёл ли цикл хотя бы раз»). Создаёт lowering (инвариант «временные —
    /// уровень анализатора»), транспилятор эмитит его и читает имя. nullptr - нет else/не синтезирован.
    AstNodePtr m_elseFlag{};

    /// Инварианты цикла (`TrustContract` с PropertyKind::Invariant), ПРИВЯЗАННЫЕ lowering'ом.
    /// Невладеющие ссылки: узлы-контракты продолжают жить в последовательности операторов (их
    /// эмитит кодоген); поле фиксирует ассоциацию «инвариант ↔ цикл» для потребителей HIR.
    /// Несколько инвариантов — конъюнкция.
    std::vector<const AstNodeBase*> m_invariants;
};

/// DoWhileStmt - цикл do-while.
/// kind = DoWhileStmt.
class DoWhileStmt : public ControlFlowStmt {
  public:
    AST_SLOT_METHOD(ControlFlowStmt, SLOTS_DoWhileStmt)
    DoWhileStmt() = default;

    DoWhileStmt(ParserToken::Kind k, TermPtr term)
    : ControlFlowStmt(k, term) {}

    /// Терм-конструктор: при `ctx != nullptr` сам строит детей (m_body/m_cond)
    /// через TermToAstConverter (do-while: m_left=cond, m_sequence=[body]).
    /// Объявлен здесь, определён в ast_stmt.cpp.
    DoWhileStmt(ParserToken::Kind k, TermPtr term, ConvertServices* ctx);

    [[nodiscard]] std::string dump(size_t indent = 0) const override;

    /// Инварианты цикла, привязанные lowering'ом (см. WhileStmt::m_invariants): невладеющие
    /// ссылки на `TrustContract`-узлы, остающиеся в последовательности операторов.
    std::vector<const AstNodeBase*> m_invariants;
};

/// Оператор сопоставления `match` (текст MATCHING-терма). Enum-opcode: потребители различают
/// вид по значению; исходный текст остаётся только в Term-фазе.
/// Единый источник глифов операторов: X-макрос (ValueDefault — дефолт без оператора, глиф "==", вне таблицы).
#define TRUST_MATCH_OP_GLYPHS(O) \
    O(Value, "==>")              \
    O(ValueExact, "===>")        \
    O(TypeIsA, "~>")             \
    O(TypeDuck, "~~>")           \
    O(TypeStrict, "~~~>")

enum class MatchOp : uint8_t {
    ValueDefault, ///< manual-узел без оператора: сравнение по значению (==)
#define TRUST_MATCH_OP_ENUM(name, glyph) name,
    TRUST_MATCH_OP_GLYPHS(TRUST_MATCH_OP_ENUM)
#undef TRUST_MATCH_OP_ENUM
};

[[nodiscard]] inline MatchOp matchOpFromSpelling(std::string_view spelling) {
#define TRUST_MATCH_OP_FROM(name, glyph) \
    if (spelling == glyph) {             \
        return MatchOp::name;            \
    }
    TRUST_MATCH_OP_GLYPHS(TRUST_MATCH_OP_FROM)
#undef TRUST_MATCH_OP_FROM
    FAULT("matchOpFromSpelling: unknown match operator '{}'", spelling);
}

/// TypeIsA..TypeStrict - непрерывный диапазон type-matching операторов.
[[nodiscard]] constexpr bool isTypeMatchOp(MatchOp op) noexcept {
    return op >= MatchOp::TypeIsA && op <= MatchOp::TypeStrict;
}

/// Trust-глиф оператора match (для диагностик); ValueDefault -> "==".
/// Недопустимое значение enum — внутренняя ошибка (FAULT).
[[nodiscard]] inline std::string_view matchOpSpelling(MatchOp op) {
    switch (op) {
    case MatchOp::ValueDefault:
        return "==";
#define TRUST_MATCH_OP_SPELLING(name, glyph) \
    case MatchOp::name:                      \
        return glyph;
        TRUST_MATCH_OP_GLYPHS(TRUST_MATCH_OP_SPELLING)
#undef TRUST_MATCH_OP_SPELLING
    }
    FAULT("unknown MatchOp value {}", static_cast<int>(op));
}

/// MatchStmt - оператор сопоставления (match/switch).
/// kind = MatchingStmt.
/// Раскладка из Term (parser.y): m_left=значение, m_right=match_body (BLOCK);
/// m_right->m_sequence = [item1, item2, ..., elseItem]. Каждый item: m_left=шаблоны
/// (m_sequence = список паттернов), m_right=тело; else: m_left = ELLIPSIS.
class MatchStmt : public AstNodeAttr {
  public:
    AST_SLOT_METHOD(AstNodeAttr, SLOTS_MatchStmt)
    struct MatchCase {
        std::vector<AstNodePtr> patterns; ///< Шаблоны ветки (объединяются через ||)
        AstNodePtr body{};                ///< Тело ветки
    };

    MatchStmt() = default;

    MatchStmt(ParserToken::Kind k, TermPtr term);

    /// Терм-конструктор: при `ctx != nullptr` сам строит детей (m_value/m_cases/m_default)
    /// через TermToAstConverter (раскладка MATCHING-терма из parser.y).
    /// Объявлен здесь, определён в ast_stmt.cpp.
    MatchStmt(ParserToken::Kind k, TermPtr term, ConvertServices* ctx);

    [[nodiscard]] std::string dump(size_t indent = 0) const override;

    AstNodePtr m_value{}; ///< Выражение для сопоставления (после анализа — ссылка на временную _matchN)
    /// Синтезированная СЕМАНТИКОЙ временная const-переменная `_matchN := <m_value>;` (scrutinee
    /// вычисляется один раз). Семантика выводит её тип в VarDecl::inferredType (exprType(*m_value));
    /// транспилятор эмитит её как обычный VarDecl и читает тип для выбора switch/enum/if. Инвариант:
    /// временные создаёт анализатор, транспилятор их не синтезирует. nullptr - не синтезирована.
    AstNodePtr m_tempDecl{};
    std::vector<MatchCase> m_cases; ///< Ветки (порядок важен)
    AstNodePtr m_default{};         ///< Тело else (nullptr если нет)

    /// Оператор сопоставления (enum-opcode из текста MATCHING-терма). Терм-конструктор
    /// заполняет через matchOpFromSpelling; manual-конструкторы оставляют ValueDefault (==).
    MatchOp m_op{MatchOp::ValueDefault};

    /// Разрешённое объявление функции-предиката `@[matcher("fn")]` — невладеющая ссылка,
    /// заполняет семантика (`analyzeMatchMatcher`); кодоген читает её вместо повторного
    /// резолва по тексту (R53). nullptr — атрибута нет / имя не разрешено.
    const AstNodeBase* m_matcherDecl = nullptr;
};

/// WithStmt - менеджер контекста `with(name=expr, ...){ body } else { ... };` (RAII).
/// kind = WithStmt.
/// Раскладка из Term (parser.y `with`): m_args = биндинги (каждый ARGUMENT-терм `name=value`),
/// m_right = тело (BLOCK → ScopeBlock); m_sequence[0] = ветка else (BLOCK; nullptr если нет).
/// Семантика: биндинги вычисляются слева направо как ЛОКАЛЬНЫЕ VarDecl (регистрируются в скоупе,
/// проверка shadow/duplicate - стандартный путь analyzeVarDecl); видимы в теле; ветка else НЕ
/// видит биндинги. При исключении в любом инициализаторе выполняется else (объекты уже
/// уничтожены); исключение в теле пробрасывается (else НЕ выполняется). RAII: объекты
/// автоуничтожаются при выходе из блока.
/// m_locks - ЕДИНЫЙ список пар `(lock, binding)`:
///   - `lock`   = RefLockExpr (источник захвата) или nullptr (обычный value-биндинг);
///   - `binding`= VarDecl, удерживающий значение до конца тела, или nullptr.
///   `with(lock = *ref)` → (RefLockExpr, VarDecl lock); `with(_ = *ref)` → (RefLockExpr, VarDecl _);
///   `with(*ref)` → (RefLockExpr, nullptr) - временный лок (снимается сразу);
///   `with(a = 1)` → (nullptr, VarDecl a).
class WithStmt : public AstNodeAttr {
  public:
    AST_SLOT_METHOD(AstNodeAttr, SLOTS_WithStmt)
    WithStmt() = default;

    WithStmt(ParserToken::Kind k, TermPtr term)
    : AstNodeAttr(k, term) {}

    /// Терм-конструктор: при `ctx != nullptr` сам строит детей (m_locks/m_body/m_else)
    /// из WITH-терма (раскладка parser.y). Форма `WITH lval` (блокировка ссылки) вне скоупа -
    /// диагностика «not implemented».
    WithStmt(ParserToken::Kind k, TermPtr term, ConvertServices* ctx);

    [[nodiscard]] std::string dump(size_t indent = 0) const override;

    /// Список пар (lock, binding) - см. комментарий класса.
    std::vector<std::pair<AstNodePtr, AstNodePtr>> m_locks{};
    AstNodePtr m_body{}; ///< Тело блока (ScopeBlock)
    AstNodePtr m_else{}; ///< Ветка else при исключении в инициализаторе (nullptr если нет)
    /// Синтезированный LOWERING флаг `bool _wN := 0;` (в C++ `catch(...){ if(_wN) throw; else }`).
    /// Создаёт lowering (инвариант «временные - уровень анализатора»), транспилятор эмитит его и
    /// читает имя. nullptr - нет else/не синтезирован.
    AstNodePtr m_failFlag{};
};

/// CatchBlock - ветка catch оператора try/catch (Вариант A, нативный C++ catch).
/// kind = CatchBlock. m_body = тело ветки.
/// m_binding - ЕДИНЫЙ узел catch-аргумента (имя+тип «в одном блоке»):
///   VarDecl(e, T) - `catch(e:Type)` (связывает перехваченное значение как локальную e);
///   IdentType(T)  - `catch(:Type)` (тип без связывания имени);
///   nullptr       - `catch(_)`/`catch(...)` (catch-any → IntAny).
/// Анализатор регистрирует имя (если VarDecl) как локальную переменную скоупа ветки;
/// кодогенерация диспетчеризует: VarDecl → `catch (T& e)`, IdentType → `catch (T&)`, nullptr → `catch (trust::IntAny&)`.
class CatchBlock : public Sequence {
  public:
    AST_SLOT_METHOD(Sequence, SLOTS_CatchBlock)
    using Sequence::Sequence;

    AstNodePtr m_binding{}; ///< catch-аргумент: VarDecl(e,T) | IdentType(T) | nullptr (catch-any)
};

/// Вид оператора перехвата: задаётся TermID блока (`{+ A +}` → Plus, `{- A -}` → Minus,
/// `{* A *}` → Any) и определяет ТИП дефолтного catch при отсутствии явных веток.
/// Текст-маркер `{+`/`{-`/`{*` не хранится (Sequence текст не несёт).
enum class TryKind : std::uint8_t {
    Any,   ///< BLOCK_TRY `{* ... *}` - ловить IntAny
    Plus,  ///< BLOCK_PLUS `{+ ... +}` - ловить IntPlus
    Minus, ///< BLOCK_MINUS `{- ... -}` - ловить IntMinus
};

/// TryCatchStmt - оператор перехвата `try { A } catch(:T){B} catch(...){C} else{D};`.
/// kind = TryCatchStmt. m_body = тело try (последовательность операторов).
/// m_catches - ветки catch (порядок важен; catch(...)/IntAny должна быть последней).
/// m_else - тело else (выполняется, если прерываний НЕ было); nullptr если нет.
/// Текст не хранится: вид перехвата - `TryKind`, имена - в дочерних узлах.
class TryCatchStmt : public Sequence {
  public:
    AST_SLOT_METHOD(Sequence, SLOTS_TryCatchStmt)
    TryCatchStmt() = default;
    /// Терм-конструктор: вид перехвата из TermID блока; дети не строятся.
    TryCatchStmt(ParserToken::Kind k, TermPtr term);
    /// Терм-конструктор: при ctx != nullptr сам строит m_body (тело try), m_catches и m_else
    /// из BLOCK_TRY-терма (m_sequence[0] = тело, m_sequence[1..] = ветки catch, m_right = else).
    TryCatchStmt(ParserToken::Kind k, TermPtr term, ConvertServices* ctx);

    /// Вид перехвата (из TermID блока); по нему кодоген выбирает тип дефолтного catch.
    TryKind m_tryKind{TryKind::Any};

    std::vector<AstNodePtr> m_catches; ///< Ветки catch (каждая - CatchBlock; порядок важен)
    AstNodePtr m_else{};               ///< Тело else (нет прерываний) или nullptr
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
class LabelRef : public WithText<AstNodeAttr> {
  public:
    LabelRef() = default;

    /// Manual-конструктор (синтетический узел): kind (GotoStmt|LabelStmt) + имя метки.
    /// Range НЕ хранится: сопоставлять сгенерированные goto/метку с исходным текстом не нужно.
    /// Имя интернируется в пул (HasText) - единая модель текста, как у прочих имён.
    LabelRef(ParserToken::Kind k, std::string_view name)
    : WithText<AstNodeAttr>(k, name) {}

    /// Синтетический узел без Term и без source-range - range невалиден (не маппится).
    [[nodiscard]] MapperRange range() const noexcept override { return {}; }

    [[nodiscard]] std::string dump(size_t indent = 0) const override;
};

/// SemicolonStmt - выражение в позиции оператора с завершающей точкой с запятой.
/// СИНТЕТИЧЕСКИЙ узел: вставляется анализатором (lowering) вокруг statement-позиции
/// выражений, чтобы решение о финальной ';' было явным в AST, а не выводилось
/// транспилятором из контекста. kind = SemicolonStmt.
class SemicolonStmt : public AstNodeAttr {
  public:
    AST_SLOT_METHOD(AstNodeAttr, SLOTS_SemicolonStmt)
    SemicolonStmt() = default;

    /// Manual-конструктор (синтетический узел): оборачивает выражение-оператор.
    explicit SemicolonStmt(AstNodePtr expr)
    : AstNodeAttr(ParserToken::Kind::SemicolonStmt)
    , m_expr(std::move(expr)) {}

    /// range делегируется обёрнутому выражению (SemicolonStmt не имеет собственного Term).
    [[nodiscard]] MapperRange range() const noexcept override { return m_expr ? m_expr->range() : MapperRange{}; }

    [[nodiscard]] std::string dump(size_t indent = 0) const override;

    AstNodePtr m_expr{}; ///< Выражение-оператор (эмитится без скобок, затем ';')
};

/// LastResultCapture - СИНТЕТИЧЕСКИЙ узел «результат последней операции» (read-only псевдо-
/// переменная `$^`). Вставляется АНАЛИЗАТОРОМ (семантика), когда в операторе найден лист `$^`:
/// вокруг предыдущего сиблинга (m_source) и оператора-потребителя (m_sink) создаётся этот узел,
/// открывающий собственный скоуп. На кодогенерации в начале узла объявляется неинициализированная
/// временная `m_temp` (синтетический const VarDecl `__trust_last_N`), в m_source перед выходом
/// значение пишется во временную, а в m_sink лист `$^` заменён ссылкой на неё. Временная
/// автоматически удаляется при закрытии скоупа узла. Парсер узел НЕ создаёт (без term,
/// невалидный range). kind = LastResultCapture.
class LastResultCapture : public AstNodeAttr {
  public:
    AST_SLOT_METHOD(AstNodeAttr, SLOTS_LastResultCapture)
    LastResultCapture() { m_kind = ParserToken::Kind::LastResultCapture; }

    /// Manual-конструктор (синтетический): источник (предыдущий сиблинг), потребитель
    /// (оператор с `$^`), временная-носитель захваченного значения.
    LastResultCapture(AstNodePtr source, AstNodePtr sink, AstNodePtr temp)
    : AstNodeAttr(ParserToken::Kind::LastResultCapture)
    , m_source(std::move(source))
    , m_sink(std::move(sink))
    , m_temp(std::move(temp)) {}

    /// range делегируется источнику (нет собственного Term); у синтетического узла может быть невалидным.
    [[nodiscard]] MapperRange range() const noexcept override { return m_source ? m_source->range() : MapperRange{}; }

    [[nodiscard]] std::string dump(size_t indent = 0) const override;

    AstNodePtr m_source{}; ///< Предыдущий сиблинг (источник значения, захватываемого в `$^`).
    AstNodePtr m_sink{};   ///< Оператор-потребитель, где лист `$^` заменён ссылкой на m_temp.
    AstNodePtr m_temp{};   ///< Синтетическая временная (`const VarDecl`, `__trust_last_N`).
};

/// ErrorExpr - УНИВЕРСАЛЬНАЯ error/recovery-заглушка (архитектурное решение для диагностики, НЕ
/// привязано к `$^`). Принцип: пасс, который первым обнаружил ошибку и обладает нужным контекстом,
/// сообщает её СРАЗУ в момент выявления и заменяет ошибочный под-узел на ErrorExpr-владельца, чтобы:
///   - последующие пассы (семантика/транспилятор) НЕ эмитили повторную/каскадную диагностику по
///     содержимому (m_original не входит в children()/collectChildren → игнорируется);
///   - заменённый узел НЕ удалялся, а сохранялся (владение в m_original) для дампов/диагностик.
/// Использовать этот же механизм в других анализаторах при «diagnose-then-replace», вместо того чтобы
/// тащить причину в следующий пасс. kind = ErrorExpr; парсер узел НЕ создаёт.
class ErrorExpr : public AstNodeAttr {
  public:
    AST_SLOT_METHOD(AstNodeAttr, SLOTS_ErrorExpr)
    ErrorExpr() { m_kind = ParserToken::Kind::ErrorExpr; }

    /// Manual-конструктор (синтетический): владелец заменённого (уже-ошибочного) узла.
    explicit ErrorExpr(AstNodePtr original)
    : AstNodeAttr(ParserToken::Kind::ErrorExpr)
    , m_original(std::move(original)) {}

    /// range делегируется заменённому узлу (своего Term у заглушки нет). Текста нет:
    /// чтение недопустимо (EXPECT), потребители разворачивают m_original явно.
    [[nodiscard]] MapperRange range() const noexcept override { return m_original ? m_original->range() : MapperRange{}; }

    [[nodiscard]] std::string dump(size_t indent = 0) const override;

    AstNodePtr m_original{}; ///< Владеемое (заменённое) под-дерево; НЕ входит в children()/collectChildren.
};

} // namespace trust
