#pragma once

#include "ast/token.hpp"
#include "ast/attr.hpp"
#include "location/location.hpp"
#include "utils/error.hpp"
#include <cstddef>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace trust {

// Forward declaration: TermPtr is defined in syntax/term_types.h (via syntax/term.h).
class Term;
using TermPtr = std::shared_ptr<Term>;

// Forward declaration (full definition in diag/context.hpp). Нужен для uniform
// терм-конструктора (kind, term, Context* ctx) генерируемой фабрики.
class Context;

// Forward declarations
class AttrPool;
class AstNodeAttr;
class Sequence;
struct LowerCtx;

/// Может ли узел данного kind нести признак иммутабельности (суффикс '^' в тексте имени)?
/// kind-вариант прежнего `canHaveImmutableQualifier(term, kind)` (там использовался только kind).
[[nodiscard]] inline bool canHaveImmutableQualifier(ParserToken::Kind kind) noexcept {
    switch (kind) {
    case ParserToken::Kind::Ident:       // имена: arg^, @name^, $name^, value^
    case ParserToken::Kind::TypeName:    // :Type^
    case ParserToken::Kind::ArgNode:     // параметры: arg^, $1^
    case ParserToken::Kind::VarDecl:     // x^
    case ParserToken::Kind::NameDecl:    // x^ := ...
    case ParserToken::Kind::FuncDecl:    // func^
    case ParserToken::Kind::StructDecl:  // class^
    case ParserToken::Kind::RefTakeExpr: // *^ - take с иммутабельностью
    case ParserToken::Kind::RefMakeExpr: // &^ - ptr с иммутабельностью
    case ParserToken::Kind::CallExpr:    // method^() - const-вызов: '^' на имени callee (ReadOnly на вызове)
        return true;
    default:
        return false;
    }
}

/// Нормализация текста имени из сырого Term при построении терм-конструктором.
/// Повторяет прежнюю нормализацию в makeNode:
///   - TypeName: срезается ведущее ':' (\":Int8\" → \"Int8\");
///   - '^' срезается ВСЕГДА (для kinds с иммутабельностью - квалификатор; для прочих -
///     ошибка синтеза, диагностируется в makeNode). Признак '^' (attr::ReadOnly) применяется
///     отдельно (convertAttrsToNode по сырому term-тексту).
[[nodiscard]] inline std::string normalizeTermText(ParserToken::Kind kind, std::string_view raw) {
    std::string s(raw);
    if (kind == ParserToken::Kind::TypeName && !s.empty() && s[0] == ':') {
        s.erase(0, 1);
    }
    if (!s.empty() && s.back() == '^') {
        s.pop_back();
    }
    return s;
}

/// AstNodeBase - базовый класс для всех узлов AST.
/// Хранит только kind и указатель на исходный Term (m_term), из которого узел построен.
/// Текст (text()) и диапазон (range()) читаются из m_term; узел без m_term - ошибка логики.
class AstNodeBase {
  public:
    ParserToken::Kind kind() const noexcept { return m_kind; }

    /// Редкое, но осознанное исключение из инварианта «kind фиксируется при создании»:
    /// используется, когда терм-слой строит механически НЕоднозначный узел, а его класс
    /// определяется ПОЗЖЕ в анализаторе по типу из реестра (напр. литерал-конструкция
    /// `:Type(...)`/`(...):Type` становится кортежем Tuple только после резолва типа).
    /// Ожидается один переход из «механического» kind в уточнённый (DictLiteral -> Tuple).
    void setKind(ParserToken::Kind k) noexcept { m_kind = k; }

    /// Текст узла. Требует m_term (иначе EXPECT/FAULT).
    [[nodiscard]] virtual std::string_view text() const;

    /// Диапазон в исходном файле. Для узла с m_term возвращает его range; для узла БЕЗ m_term
    /// (ручной/test-only, синтетический) - НЕВАЛИДНЫЙ range ({}), без EXPECT: родительские узлы
    /// вычисляют свой охват на лету по ранжам детей (см. Binary/ControlFlowStmt/MatchStmt/
    /// VarDecl/FuncDecl::range), поэтому ручные дети обязаны мягко сообщать отсутствие
    /// source-range. Виртуальный: синтетические узлы (LabelRef/SemicolonStmt/JumpStmt-lowering)
    /// переопределяют его, возвращая явно заданный range вместо чтения из m_term.
    [[nodiscard]] virtual MapperRange range() const;

    /// Исходный Term, из которого построен узел (может быть nullptr для manual-узлов).
    [[nodiscard]] const TermPtr& term() const noexcept { return m_term; }

    [[nodiscard]] bool empty() const noexcept { return m_kind == ParserToken::Kind::END; }

    /// If this node is an AstNodeAttr, returns itself; otherwise nullptr.
    [[nodiscard]] virtual AstNodeAttr* as_attr() noexcept { return nullptr; }
    [[nodiscard]] virtual const AstNodeAttr* as_attr() const noexcept { return nullptr; }

    /// If this node is a Sequence (or derived - ScopeBlock/ModuleNode), returns itself;
    /// otherwise nullptr. Позволяет обходчикам получать m_body без kind/static_cast.
    [[nodiscard]] virtual Sequence* as_sequence() noexcept { return nullptr; }
    [[nodiscard]] virtual const Sequence* as_sequence() const noexcept { return nullptr; }

    /// Единый источник истины «kind → дети»: заполняет out указателями на дочерние
    /// слоты (позволяет заменять узлы при обходе, напр. раскрытие ContextMacro).
    /// Для листов - пусто. Не мутирует дерево. Определён в ast_nodes.cpp.
    void collectChildren(std::vector<AstNodePtr*>& out);

    /// Все дочерние узлы AST (обобщённый обход) - const-обёртка над collectChildren,
    /// возвращает копии узлов. Для листов - пустой вектор.
    /// Определён в ast_nodes.cpp (нужны полные типы узлов).
    [[nodiscard]] std::vector<AstNodePtr> children() const;

    /// Dump token contents for debugging
    [[nodiscard]] virtual std::string dump(size_t indent = 0) const;

    /// Понижение узла согласно его Kind: каждый класс переопределяет и рекурсивно понижает
    /// своих детей (см. ast/lowering.hpp). По умолчанию - no-op (лист без детей).
    /// self - shared_ptr на этот узел (нужен для подмены, напр. break→goto).
    virtual void lower(AstNodePtr& self, LowerCtx& ctx);

    /// Документирующий комментарий (`///`, `##`, `/**`, т.ч. хвостовой `///<`/`##<`),
    /// привязанный к объявлению. Заполняется TermToAstConverter::convert из term->m_docs
    /// (см. include/syntax/term.h); для LSP hover/док (SymbolCollectorHook::finalize).
    /// Пуст, если у объявления нет документирующего комментария.
    std::string documentation;

    AstNodeBase() = default;

    /// Kind is set at construction and (normally) never changes (like Clang's StmtClass).
    /// Единственное исключение - `setKind`, используемое анализатором для уточнения
    /// механически созданного kind (DictLiteral -> Tuple) по типу из реестра.
    virtual ~AstNodeBase() = default;

    /// Construct with kind and the source Term this node is derived from.
    // Проверка m_term выполняется только в производных классах, где text()/range()
    // реально читаются из Term (Binary, JumpStmt, CallExpr). Классы с собственным
    // m_text (HasText-семейство: IdentName, Literal, Sequence и т.д.) не требуют Term
    // для text().
    AstNodeBase(ParserToken::Kind k, TermPtr term)
    : m_kind(k)
    , m_term(std::move(term)) {}

  protected:
    /// Construct with kind only (no source Term; text()/range() will EXPECT).
    explicit AstNodeBase(ParserToken::Kind k)
    : m_kind(k) {}

    ParserToken::Kind m_kind{ParserToken::Kind::END};

    /// Исходный синтаксический узел, из которого построен этот AST-узел.
    TermPtr m_term;
};

/// Истина, если kind - объявление (для привязки документирующего комментария к узлу).
inline bool isDeclKindForDocs(ParserToken::Kind k) {
    return k == ParserToken::Kind::VarDecl || k == ParserToken::Kind::FuncDecl || k == ParserToken::Kind::TypeDecl || k == ParserToken::Kind::ArgNode;
}

/// AstNodeAttr - расширение AstNodeBase с поддержкой атрибутов.
class AstNodeAttr : public AstNodeBase {
  public:
    /// Read-only access to attributes
    const std::vector<AttrId>& attrs() const noexcept { return m_attrs; }

    AstNodeAttr() = default;

    virtual ~AstNodeAttr() = default;

    /// Construct with kind and the source Term.
    AstNodeAttr(ParserToken::Kind k, TermPtr term)
    : AstNodeBase(k, std::move(term)) {}

    /// Uniform term-constructor for the generated factory (kind + source Term).
    /// AstNodeAttr хранит только attrs/docs - детей не строит (ctx игнорируется).
    AstNodeAttr(ParserToken::Kind k, TermPtr term, Context* /*ctx*/)
    : AstNodeBase(k, std::move(term)) {}

    /// Construct with kind only (no source Term).
    explicit AstNodeAttr(ParserToken::Kind k)
    : AstNodeBase(k) {}

    /// Add an attribute ID to this token.
    /// @param manual - set the manual bit (source-level attribute, default true).
    void add_attr(AttrId id, bool manual = true) { m_attrs.push_back(manual ? detail::with_manual(id) : id); }

    /// Check if this token has a specific attribute by ID.
    /// Search is performed by the base index, ignoring the bitmask, so
    /// manual/auto and builtin/user variants match the same attribute.
    [[nodiscard]] bool has_attr(AttrId id) const {
        AttrId idx = id & detail::kAttrIndexMask;
        return std::find_if(m_attrs.begin(), m_attrs.end(), [idx](AttrId stored) { return (stored & detail::kAttrIndexMask) == idx; }) != m_attrs.end();
    }

    /// Check if this token has an attribute by name (delegates to has_attr(AttrId)).
    [[nodiscard]] bool has_attr(const AttrPool& pool, std::string_view name) const;

    /// Хранит список строковых аргументов атрибута (`@[link("m")]` → ["m"]).
    /// Атрибут может иметь один или несколько аргументов; аргументы конвертируются
    /// в строки при создании AstNode (convertAttrsToNode). Ключ - индекс атрибута
    /// (id & kAttrIndexMask). Пуст, если атрибут аргументов не несёт.
    void set_attr_args(AttrId id, std::vector<std::string> args);

    /// Возвращает список аргументов атрибута (nullptr, если не задано).
    [[nodiscard]] const std::vector<std::string>* attr_args(AttrId id) const noexcept;

    [[nodiscard]] AstNodeAttr* as_attr() noexcept override { return this; }
    [[nodiscard]] const AstNodeAttr* as_attr() const noexcept override { return this; }

    /// Dump token contents for debugging
    [[nodiscard]] std::string dump(size_t indent = 0) const override;

  protected:
    std::vector<AttrId> m_attrs; ///< Attribute IDs attached to this token
    /// Аргументы атрибутов: индекс атрибута → список строковых значений (см. set_attr_args).
    std::map<AttrId, std::vector<std::string>> m_attrArgs;
};

/// HasText - промежуточный базовый класс для узлов, хранящих текст локально в m_text.
/// Устраняет дублирование поля m_text и override text() между Literal/Sequence/ArgNode
/// и IdentName (и узлами, наследующими их). Текст НЕ читается из m_term (в отличие от
/// Binary/JumpStmt). ЕДИНЫЙ носитель локального текста узла в иерархии AST.
///
/// Две группы конструкторов (правило: никаких гибридов term+данные):
///   - терм-конструктор `HasText(Kind, TermPtr)` - текст читается из m_term и нормализуется
///     (задаётся в .cpp), m_term хранится для range();
///   - manual-конструктор `HasText(Kind, std::string)` - БЕЗ TermPtr (text() работает,
///     range() вернёт invalid range).
class HasText : public AstNodeAttr {
  public:
    HasText() = default;

    /// Терм-конструктор: текст читается из Term и нормализуется по kind (см. normalizeTermText).
    /// Объявлен здесь, определён в ast_nodes.cpp (нужен полный тип Term).
    HasText(ParserToken::Kind k, TermPtr term);

    /// Manual-конструктор: текст задан явно, без TermPtr.
    HasText(ParserToken::Kind k, std::string text)
    : AstNodeAttr(k)
    , m_text(std::move(text)) {}

    [[nodiscard]] std::string_view text() const noexcept override { return m_text; }

    /// Переопределяет локальный текст узла (используется анализатором для нормализации имён,
    /// напр. `x` → `$x`). range()/m_term не меняются; trust-имя source-map берётся из исходника.
    void set_text(std::string_view t) { m_text.assign(t); }

  protected:
    std::string m_text; ///< Локальный текст узла
};

namespace detail {
/// Единый формат заголовка dump для узлов с именем: `Name 'text'` (+ необязательный
/// prefix, например ':' для TypeName). Устраняет повторяющийся паттерн в
/// IdentName/IdentType/Decl/ArgNode/ModuleNode dump().
[[nodiscard]] inline std::string dumpQuotedName(ParserToken::Kind kind, std::string_view text, size_t indent, std::string_view prefix = {}) {
    std::string result(indent, ' ');
    result += ParserToken::name(kind);
    if (!text.empty()) {
        result += " '";
        result += prefix;
        result += text;
        result += "'";
    }
    return result;
}
} // namespace detail

/// Единый хелпер иммутабельности ('^' → attr::ReadOnly). Используется конвертером
/// Term→AST (convertAttrsToNode) и IdentName (stripCaretAndApplyReadonly), чтобы логика
/// «суффикс '^' ⇒ квалификатор ReadOnly» не дублировалась.
/// Сам '^' из текста НЕ срезает (это делает normalizeTermText при построении узла).
/// Возвращает true, если ReadOnly действительно применён (raw имеет '^' И kind допускает
/// квалификатор). Определён в ast_nodes.cpp (нужен полный тип AttrPool).
bool applyReadonlyFromCaret(AstNodeAttr& node, std::string_view raw, AttrPool* pool);

} // namespace trust