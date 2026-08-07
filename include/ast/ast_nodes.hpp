// include/ast/ast_nodes.hpp
// Специализированные AST-узлы: Binary, CallExpr, ScopeBlock, ParamDecl, Sequence, VarDecl.
// Наследуют напрямую AstNodeBase, без универсального AstNode.
//
// Иерархия:
//   AstNodeBase
//     ├── Binary      (kind=TypeDecl|NameDecl|AssignOp|MathOp|BitwiseOp|CompareOp|LogicalOp|MemberAccess|ArrayAccess,
//     │                +m_left, m_right)
//     ├── CallExpr    (kind=CallExpr, +m_callee, m_args)
//     ├── ParamDecl   (kind=ParamDecl, +m_type, m_default; text()=имя параметра)
//     ├── Sequence    (kind=sequence|Attr, +m_body; text()=метка/текст)
//     │     ├── ScopeBlock  (kind=ScopeBlock, +m_name)
//     │     └── ModuleNode  (kind=ModuleDecl, +m_moduleIndex)
//     ├── JumpStmt   (kind=ReturnStmt|ThrowStmt, +m_label, m_value)
//     ├── Decl       (kind any)
//     │     ├── VarDecl     (kind=VarDecl, +m_initializer, m_is_mutable)
//     │     └── FuncDecl    (kind=FuncDecl, +m_params, m_body)
//     └── ...
//
// Все поля, ранее использовавшие SyntaxToken, заменены на AstNodePtr.
// Списки узлов — std::vector<AstNodePtr>.
//
// Конструкторы с TermPtr — основной путь создания (из терминов синтаксического дерева).
// Конструкторы без Term (test-only) существуют для unit-тестов; text() у классов
// с m_text работает, range() для узла без Term вызывает EXPECT.

#pragma once

#include "ast/token_base.hpp"
#include "ast/token.hpp"
#include "ast/ident_name.hpp"
#include "utils/error.hpp"
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace trust {

/// Literal — узел AST для константных литералов (IntLiteral, StringLiteral).
/// kind = IntLiteral | StringLiteral.
/// Переопределяет dump() для форматирования: IntLiteral '42', StringLiteral "hello".
class Literal : public AstNodeAttr {
  public:
    Literal() = default;

    /// Конструктор из исходного Term: текст копируется из терма в m_text.
    Literal(ParserToken::Kind k, std::string text, TermPtr term)
    : AstNodeAttr(k, std::move(term))
    , m_text(std::move(text)) {}

    /// Конструктор с текстом без Term (range() будет EXPECT).
    Literal(ParserToken::Kind k, std::string text)
    : AstNodeAttr(k)
    , m_text(std::move(text)) {}

    /// Test-only: узел без Term (range() будет EXPECT).
    Literal(ParserToken::Kind k, std::string text, MapperRange r)
    : Literal(k, std::move(text)) {
        (void)r;
    }

    [[nodiscard]] std::string_view text() const noexcept override { return m_text; }

    [[nodiscard]] std::string dump(size_t indent = 0) const override;

  private:
    std::string m_text; ///< Нормализованный текст литерала
};

/// Binary — бинарная операция, member access или array access.
/// kind = TypeDecl | NameDecl | AssignOp | MathOp | BitwiseOp | CompareOp |
///        LogicalOp | MemberAccess | ArrayAccess.
/// Текст оператора (text()) берётся из исходного Term.
class Binary : public AstNodeAttr {
  public:
    Binary() = default;

    /// text() читается из m_term — конструктор требует валидный Term.
    Binary(ParserToken::Kind k, TermPtr term, AstNodePtr left, AstNodePtr right)
    : AstNodeAttr(k, std::move(term))
    , m_left(std::move(left))
    , m_right(std::move(right)) {}

    Binary(ParserToken::Kind k, TermPtr term)
    : Binary(k, std::move(term), nullptr, nullptr) {}

    /// Test-only: узел без Term (range()/text() будут EXPECT).
    Binary(ParserToken::Kind k, std::string, MapperRange r)
    : AstNodeAttr(k) {
        (void)r;
    }

    explicit Binary(ParserToken::Kind k)
    : AstNodeAttr(k) {}

    [[nodiscard]] std::string dump(size_t indent = 0) const override;

    AstNodePtr m_left;
    AstNodePtr m_right;
};

/// Является ли kind бинарным узлом (имеет m_left/m_right).
[[nodiscard]] inline bool is_binary_kind(ParserToken::Kind k) noexcept {
    switch (k) {
    case ParserToken::Kind::TypeDecl:
    case ParserToken::Kind::NameDecl:
    case ParserToken::Kind::AssignOp:
    case ParserToken::Kind::MathOp:
    case ParserToken::Kind::BitwiseOp:
    case ParserToken::Kind::CompareOp:
    case ParserToken::Kind::LogicalOp:
    case ParserToken::Kind::MemberAccess:
    case ParserToken::Kind::ArrayAccess:
        return true;
    default:
        return false;
    }
}

/// CallExpr — вызов функции: callee(args...)
/// kind = CallExpr.
class CallExpr : public AstNodeAttr {
  public:
    CallExpr() = default;

    /// text() читается из m_term — конструктор требует валидный Term.
    CallExpr(ParserToken::Kind k, TermPtr term, AstNodePtr callee)
    : AstNodeAttr(k, std::move(term))
    , m_callee(std::move(callee)) {}

    /// Convenience constructor with callee = nullptr.
    CallExpr(ParserToken::Kind k, TermPtr term)
    : CallExpr(k, std::move(term), nullptr) {}

    [[nodiscard]] std::string dump(size_t indent = 0) const override;

    AstNodePtr m_callee;                           ///< Вызываемое выражение
    std::optional<std::vector<AstNodePtr>> m_args; ///< Аргументы вызова
};

/// ParamDecl — объявление параметра функции.
/// kind = ParamDecl.
/// text() — имя параметра (простая строка, не IdentName).
class ParamDecl : public AstNodeAttr {
  public:
    ParamDecl() = default;

    /// Конструктор из исходного Term (имя параметра копируется в m_text).
    ParamDecl(std::string name, TermPtr term, AstNodePtr type = nullptr, AstNodePtr defaultVal = nullptr)
    : AstNodeAttr(ParserToken::Kind::ParamDecl, std::move(term))
    , m_type(std::move(type))
    , m_default(std::move(defaultVal))
    , m_text(std::move(name)) {}

    /// Конструктор без Term (test-only; range() будет EXPECT).
    ParamDecl(std::string name, AstNodePtr type = nullptr, AstNodePtr defaultVal = nullptr)
    : AstNodeAttr(ParserToken::Kind::ParamDecl)
    , m_type(std::move(type))
    , m_default(std::move(defaultVal))
    , m_text(std::move(name)) {}

    [[nodiscard]] std::string_view text() const noexcept override { return m_text; }

    [[nodiscard]] std::string dump(size_t indent = 0) const override;

    AstNodePtr m_type;    ///< Тип параметра (может быть nullptr)
    AstNodePtr m_default; ///< Значение по умолчанию (nullptr = нет default)

  private:
    std::string m_text; ///< Имя параметра
};

/// Sequence — узел с телом (последовательностью дочерних токенов).
/// kind = sequence | Attr.
/// text() — метка/текст узла (у ScopeBlock/ModuleNode — имя области/модуля).
class Sequence : public AstNodeAttr {
  public:
    Sequence() = default;

    /// Конструктор из исходного Term (текст копируется в m_text).
    Sequence(ParserToken::Kind k, std::string text, TermPtr term)
    : AstNodeAttr(k, std::move(term))
    , m_text(std::move(text)) {}

    /// Конструктор без Term (range() будет EXPECT).
    Sequence(ParserToken::Kind k, std::string text)
    : AstNodeAttr(k)
    , m_text(std::move(text)) {}

    /// Test-only: узел без Term (range() будет EXPECT).
    Sequence(ParserToken::Kind k, std::string text, MapperRange r)
    : Sequence(k, std::move(text)) {
        (void)r;
    }

    [[nodiscard]] std::string_view text() const noexcept override { return m_text; }

    [[nodiscard]] std::string dump(size_t indent = 0) const override;

    /// Утилита для дампа содержимого списка узлов (переиспользуется в ScopeBlock, ModuleNode)
    static void dumpBody(std::string& result, const std::vector<AstNodePtr>& body, size_t indent, size_t child_indent);

    std::vector<AstNodePtr> m_body; ///< Тело/содержимое узла

  private:
    std::string m_text; ///< Метка/имя области (для ScopeBlock/ModuleNode)
};

/// ScopeBlock — блок с меткой/именем области.
/// kind = ScopeBlock.
/// Наследует Sequence (+m_body от родителя).
/// Имя блока хранится в text().
///   ""       — блок без метки { ... } (анонимный)
///   "_"      — скрытая область реализации (имена из нее не эспортируются) _ { ... }
///   "name"   — именованная область name { ... }
///   "::" — глобальная область имен :: { ... }
///   "::ns::name" — глобальная именованная область имен ::ns::name { ... }
///   "ns::name::" — именованная область имен ns::name:: { ... }
/// Поле m_blockCounter — глобально уникальный идентификатор блока,
/// автоматически присваиваемый из Context::nextBlockCounter().
class ScopeBlock : public Sequence {
  public:
    ScopeBlock() = default;

    ScopeBlock(std::string name, TermPtr term, int blockCounter = 0)
    : Sequence(ParserToken::Kind::ScopeBlock, std::move(name), std::move(term))
    , m_blockCounter(blockCounter) {}

    [[nodiscard]] bool is_anonymous() const noexcept { return text().empty(); }
    [[nodiscard]] bool is_hidden() const noexcept { return text() == "_"; }
    [[nodiscard]] std::string_view name() const noexcept { return text(); }
    [[nodiscard]] int blockCounter() const noexcept { return m_blockCounter; }

    [[nodiscard]] std::string dump(size_t indent = 0) const override;

  private:
    int m_blockCounter{0}; ///< Глобально уникальный идентификатор блока
};

/// ModuleNode — узел AST, представляющий загруженный модуль.
/// Наследует Sequence (через m_body содержит тело модуля из cacheBody).
/// Хранит индекс записи в ModuleRegistry.
/// Имя модуля доступно через text().
/// kind = ModuleDecl.
class ModuleNode : public Sequence {
  public:
    ModuleNode() = default;

    ModuleNode(std::size_t moduleIndex, std::string name, TermPtr term)
    : Sequence(ParserToken::Kind::ModuleDecl, std::move(name), std::move(term))
    , m_moduleIndex(moduleIndex) {}

    /// Test-only: узел без Term (range()/text() будут EXPECT).
    ModuleNode(std::size_t moduleIndex, std::string name, MapperRange r)
    : Sequence(ParserToken::Kind::ModuleDecl, std::move(name))
    , m_moduleIndex(moduleIndex) {
        (void)r;
    }

    [[nodiscard]] std::size_t moduleIndex() const noexcept { return m_moduleIndex; }
    [[nodiscard]] std::string_view moduleId() const noexcept { return text(); }

    [[nodiscard]] std::string dump(size_t indent = 0) const override;

  private:
    std::size_t m_moduleIndex{0}; ///< Индекс записи в ModuleRegistry
};

/// Decl — базовый класс для объявлений (VarDecl, FuncDecl).
/// Наследует IdentName для хранения имени объявления в text().
/// Добавляет m_type — тип объявления (может быть nullptr).
class Decl : public IdentName {
  public:
    Decl() = default;

    Decl(std::string text, TermPtr term)
    : IdentName(std::move(text), std::move(term)) {}

    /// Конструктор без Term (range() будет EXPECT).
    Decl(std::string text, MapperRange r)
    : IdentName(std::move(text)) {
        (void)r;
    }

    [[nodiscard]] std::string dump(size_t indent = 0) const override;

    /// Тип объявления (nullptr если тип не указан или выводится).
    AstNodePtr m_type;
};

/// FuncDecl — объявление функции.
/// kind = FuncDecl.
/// Наследует Decl: имя функции (с % префиксом) в text(), тип в m_type.
/// Поддерживает:
///   %func(params):Type ::= { body }  — полное определение
///   %func(params):Type ::= ... ;     — предварительное объявление (m_body = nullopt)
class FuncDecl : public Decl {
  public:
    FuncDecl() { m_kind = ParserToken::Kind::FuncDecl; }

    FuncDecl(std::string text, TermPtr term)
    : Decl(std::move(text), std::move(term)) {
        m_kind = ParserToken::Kind::FuncDecl;
    }

    /// Конструктор без Term (test-only; range() будет EXPECT).
    FuncDecl(std::string text, MapperRange r)
    : Decl(std::move(text), std::move(r)) {
        m_kind = ParserToken::Kind::FuncDecl;
    }

    [[nodiscard]] std::string dump(size_t indent = 0) const override;

    /// Диапазон тела функции — блока `{ ... }` (m_term->m_right), если доступен.
    /// Используется для зеркалирования раскладки исходника ({ и } на тех же строках).
    /// Возвращает invalid range, если терм/блок недоступны (test-only узлы).
    [[nodiscard]] MapperRange blockRange() const noexcept;

    /// Параметры функции (каждый — ParamDecl)
    std::optional<std::vector<AstNodePtr>> m_params;

    /// Тело функции (опционально — для предварительных объявлений)
    std::optional<std::vector<AstNodePtr>> m_body;
};

/// JumpStmt — инструкция перехода (return / throw).
/// kind = ReturnStmt | ThrowStmt.
/// Синтаксис:
///   ++ [value] ++ ;     — return
///   -- [value] -- ;     — throw
///   label :: ++ ;       — return с меткой
/// Текст оператора (text()) берётся из исходного Term.
class JumpStmt : public AstNodeAttr {
  public:
    JumpStmt() = default;

    /// text() читается из m_term — конструктор требует валидный Term.
    JumpStmt(ParserToken::Kind k, TermPtr term)
    : AstNodeAttr(k, std::move(term)) {}

    /// Test-only: узел без Term (range()/text() будут EXPECT).
    JumpStmt(ParserToken::Kind k, std::string, MapperRange r)
    : AstNodeAttr(k) {
        (void)r;
    }

    explicit JumpStmt(ParserToken::Kind k)
    : AstNodeAttr(k) {}

    [[nodiscard]] std::string dump(size_t indent = 0) const override;

    AstNodePtr m_label{}; ///< Optional label (IdentName) before ::
    AstNodePtr m_value{}; ///< Optional return/throw value expression (nullptr = void)
};

/// VarDecl — объявление переменной.
/// kind = VarDecl.
/// text() — имя переменной (чистое, без префиксов).
/// Поддерживает:
///   x := expr        — m_name="x", m_type=nullptr, m_initializer=expr
///   x:Type := expr   — m_name="x", m_type=IdentType, m_initializer=expr
///   x := expr mut    — m_is_mutable=true (future)
class VarDecl : public Decl {
  public:
    VarDecl() { m_kind = ParserToken::Kind::VarDecl; }

    /// Construct with name, optional type, optional initializer (from source Term).
    VarDecl(std::string name, TermPtr term, AstNodePtr type = nullptr, AstNodePtr initializer = nullptr, bool is_mutable = false)
    : Decl(std::move(name), std::move(term))
    , m_initializer(std::move(initializer))
    , m_is_mutable(is_mutable) {
        m_kind = ParserToken::Kind::VarDecl;
        m_type = std::move(type);
    }

    /// Construct with name, optional type, optional initializer (test-only; range() will EXPECT).
    VarDecl(std::string name, MapperRange r, AstNodePtr type = nullptr, AstNodePtr initializer = nullptr, bool is_mutable = false)
    : Decl(std::move(name), std::move(r))
    , m_initializer(std::move(initializer))
    , m_is_mutable(is_mutable) {
        m_kind = ParserToken::Kind::VarDecl;
        m_type = std::move(type);
    }

    [[nodiscard]] std::string dump(size_t indent = 0) const override;

    /// Диапазон исходного имени переменной (для source-map/имени).
    /// Базовый m_term для `x := ...` — это терм оператора ':=' (CREATE_NAME):
    /// его m_mapperRange указывает на оператор, а реальное имя лежит в m_term->m_left.
    /// Возвращает диапазон m_term->m_left (имени) или невалидный, если имя не из исходника.
    [[nodiscard]] MapperRange nameRange() const noexcept;

    AstNodePtr m_initializer;  ///< Инициализатор (nullptr = нет инициализатора)
    bool m_is_mutable = false; ///< mutable qualifier
};

/// IfStmt — условный оператор.
/// kind = IfStmt.
/// Единая раскладка из Term (parser.y): m_left=условие, m_right=else,
/// m_block=[тело then, branch2, branch3, ...] (branch_i = терм cond_i→body_i).
/// Здесь хранятся сконвертированные AST-дети: m_cond, m_then, m_elseifs, m_else.
class IfStmt : public AstNodeAttr {
  public:
    IfStmt() = default;

    IfStmt(ParserToken::Kind k, TermPtr term)
    : AstNodeAttr(k, std::move(term)) {}

    [[nodiscard]] std::string dump(size_t indent = 0) const override;

    AstNodePtr m_cond{};                                      ///< Условие первой ветки
    AstNodePtr m_then{};                                      ///< Тело then-ветки
    std::vector<std::pair<AstNodePtr, AstNodePtr>> m_elseifs; ///< (условие, тело) для else-if
    AstNodePtr m_else{};                                      ///< Тело else-ветки (nullptr если нет)
};

/// WhileStmt — цикл while с опциональным else.
/// kind = WhileStmt.
class WhileStmt : public AstNodeAttr {
  public:
    WhileStmt() = default;

    WhileStmt(ParserToken::Kind k, TermPtr term)
    : AstNodeAttr(k, std::move(term)) {}

    [[nodiscard]] std::string dump(size_t indent = 0) const override;

    AstNodePtr m_cond{}; ///< Условие
    AstNodePtr m_body{}; ///< Тело цикла
    AstNodePtr m_else{}; ///< Тело else (nullptr если нет)
};

/// DoWhileStmt — цикл do-while.
/// kind = DoWhileStmt.
class DoWhileStmt : public AstNodeAttr {
  public:
    DoWhileStmt() = default;

    DoWhileStmt(ParserToken::Kind k, TermPtr term)
    : AstNodeAttr(k, std::move(term)) {}

    [[nodiscard]] std::string dump(size_t indent = 0) const override;

    AstNodePtr m_body{}; ///< Тело цикла
    AstNodePtr m_cond{}; ///< Условие
};

/// MatchStmt — оператор сопоставления (match/switch).
/// kind = MatchingStmt.
/// Раскладка из Term (parser.y): m_left=значение, m_right=match_body (BLOCK);
/// m_right->m_block = [item1, item2, ..., elseItem]. Каждый item: m_left=шаблоны
/// (m_block = список паттернов), m_right=тело; else: m_left = ELLIPSIS.
class MatchStmt : public AstNodeAttr {
  public:
    struct MatchCase {
        std::vector<AstNodePtr> patterns; ///< Шаблоны ветки (объединяются через ||)
        AstNodePtr body{};                ///< Тело ветки
    };

    MatchStmt() = default;

    MatchStmt(ParserToken::Kind k, TermPtr term)
    : AstNodeAttr(k, std::move(term)) {}

    [[nodiscard]] std::string dump(size_t indent = 0) const override;

    AstNodePtr m_value{};           ///< Выражение для сопоставления
    std::vector<MatchCase> m_cases; ///< Ветки (порядок важен)
    AstNodePtr m_default{};         ///< Тело else (nullptr если нет)
};

} // namespace trust