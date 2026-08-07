// ast_nodes.cpp — реализации Binary, CallExpr, Scope, Decl, JumpStmt, VarDecl
// + утилита dumpBody() для устранения дублирования обхода std::vector<AstNodePtr>

#include "ast/ast_nodes.hpp"
#include "ast/attr_pool.hpp"
#include "ast/term_to_ast.hpp"
#include "ast/token_type.hpp"
#include "syntax/term.h"
#include <optional>
#include <string_view>

namespace trust {

// ── AstNodeBase::collectChildren — единый источник истины «kind → дети». ──
//    Заполняет out указателями на дочерние слоты (позволяет заменять узлы при обходе).
//    Листья — без слотов. Используется семантикой (обход с подменой узлов) и const-
//    обёрткой children() (см. ниже). Не мутирует дерево — только читает слоты.

void AstNodeBase::collectChildren(std::vector<AstNodePtr*>& out) {
    switch (m_kind) {
    // ── Sequence / блоки / модуль: m_body ──
    case ParserToken::Kind::sequence:
    case ParserToken::Kind::Attr:
    case ParserToken::Kind::ScopeBlock:
    case ParserToken::Kind::ModuleDecl:
    case ParserToken::Kind::ArrayInit:
    case ParserToken::Kind::DictLiteral:
    case ParserToken::Kind::Tuple:
    case ParserToken::Kind::RangeExpr:
    case ParserToken::Kind::RefMakeExpr:
    case ParserToken::Kind::RefTakeExpr:
    case ParserToken::Kind::Ellipsis:
    case ParserToken::Kind::TryCatchStmt:
    case ParserToken::Kind::CatchBlock:
    case ParserToken::Kind::EnumDecl:
    case ParserToken::Kind::EnumMember:
    case ParserToken::Kind::StructDecl:
    case ParserToken::Kind::StructField: {
        auto& s = static_cast<Sequence&>(*this);
        for (auto& c : s.m_body) {
            out.push_back(&c);
        }
        break;
    }
    // ── Binary: m_left/m_right. Для объявлений (TypeDecl/NameDecl) m_left — имя
    //    объявляемого, его НЕ обходим (не резолвим как переменную) — только m_right. ──
    case ParserToken::Kind::AssignOp:
    case ParserToken::Kind::MathOp:
    case ParserToken::Kind::BitwiseOp:
    case ParserToken::Kind::CompareOp:
    case ParserToken::Kind::LogicalOp:
    case ParserToken::Kind::MemberAccess:
    case ParserToken::Kind::ArrayAccess:
    case ParserToken::Kind::AppendStmt: {
        auto& b = static_cast<Binary&>(*this);
        if (b.m_left) {
            out.push_back(&b.m_left);
        }
        if (b.m_right) {
            out.push_back(&b.m_right);
        }
        break;
    }
    case ParserToken::Kind::TypeDecl:
    case ParserToken::Kind::NameDecl: {
        auto& b = static_cast<Binary&>(*this);
        if (b.m_right) {
            out.push_back(&b.m_right);
        }
        break;
    }
    // ── DestructureDecl: обходим только источник; цели слева — объявления, их не резолвим ──
    case ParserToken::Kind::DestructureDecl: {
        auto& d = static_cast<DestructureDecl&>(*this);
        if (d.m_source) {
            out.push_back(&d.m_source);
        }
        break;
    }
    // ── CallExpr: m_callee + m_args ──
    case ParserToken::Kind::CallExpr: {
        auto& c = static_cast<CallExpr&>(*this);
        if (c.m_callee) {
            out.push_back(&c.m_callee);
        }
        if (c.m_args) {
            for (auto& a : *c.m_args) {
                out.push_back(&a);
            }
        }
        break;
    }
    // ── IdentType: dims [...] + params (...) ──
    case ParserToken::Kind::TypeName: {
        auto& t = static_cast<IdentType&>(*this);
        if (t.dims()) {
            auto& dims = const_cast<std::vector<AstNodePtr>&>(*t.dims());
            for (auto& d : dims) {
                out.push_back(&d);
            }
        }
        if (t.params()) {
            auto& params = const_cast<std::vector<AstNodePtr>&>(*t.params());
            for (auto& p : params) {
                out.push_back(&p);
            }
        }
        break;
    }
    // ── JumpStmt (return/throw/break/continue): m_value — выражение. m_label — метка
    //    (не переменная), её НЕ обходим и не резолвим. ──
    case ParserToken::Kind::ReturnStmt:
    case ParserToken::Kind::ThrowStmt:
    case ParserToken::Kind::BreakStmt:
    case ParserToken::Kind::ContinueStmt: {
        auto& j = static_cast<JumpStmt&>(*this);
        if (j.m_value) {
            out.push_back(&j.m_value);
        }
        break;
    }
    // ── VarDecl: m_type + m_initializer ──
    case ParserToken::Kind::VarDecl: {
        auto& v = static_cast<VarDecl&>(*this);
        if (v.m_type) {
            out.push_back(&v.m_type);
        }
        if (v.m_initializer) {
            out.push_back(&v.m_initializer);
        }
        break;
    }
    // ── FuncDecl: m_type + m_params + m_body ──
    case ParserToken::Kind::FuncDecl: {
        auto& f = static_cast<FuncDecl&>(*this);
        if (f.m_type) {
            out.push_back(&f.m_type);
        }
        if (f.m_params) {
            for (auto& p : *f.m_params) {
                out.push_back(&p);
            }
        }
        if (f.m_body) {
            for (auto& b : *f.m_body) {
                out.push_back(&b);
            }
        }
        break;
    }
    // ── ArgNode: m_type + m_value ──
    case ParserToken::Kind::ArgNode: {
        auto& p = static_cast<ArgNode&>(*this);
        if (p.m_type) {
            out.push_back(&p.m_type);
        }
        if (p.m_value) {
            out.push_back(&p.m_value);
        }
        break;
    }
    // ── ControlFlowStmt (if/while/do-while): cond + body + else (+ elseifs у if) ──
    case ParserToken::Kind::IfStmt:
    case ParserToken::Kind::WhileStmt:
    case ParserToken::Kind::DoWhileStmt: {
        auto& cf = static_cast<ControlFlowStmt&>(*this);
        if (cf.m_cond) {
            out.push_back(&cf.m_cond);
        }
        if (cf.m_body) {
            out.push_back(&cf.m_body);
        }
        if (cf.m_else) {
            out.push_back(&cf.m_else);
        }
        if (m_kind == ParserToken::Kind::IfStmt) {
            auto& i = static_cast<IfStmt&>(*this);
            for (auto& [cond, body] : i.m_elseifs) {
                if (cond) {
                    out.push_back(&cond);
                }
                if (body) {
                    out.push_back(&body);
                }
            }
        }
        break;
    }
    // ── SemicolonStmt: m_expr ──
    case ParserToken::Kind::SemicolonStmt: {
        auto& e = static_cast<SemicolonStmt&>(*this);
        if (e.m_expr) {
            out.push_back(&e.m_expr);
        }
        break;
    }
    // ── MatchStmt: m_value + patterns/body веток + m_default ──
    case ParserToken::Kind::MatchingStmt: {
        auto& m = static_cast<MatchStmt&>(*this);
        if (m.m_value) {
            out.push_back(&m.m_value);
        }
        for (auto& c : m.m_cases) {
            for (auto& p : c.patterns) {
                if (p) {
                    out.push_back(&p);
                }
            }
            if (c.body) {
                out.push_back(&c.body);
            }
        }
        if (m.m_default) {
            out.push_back(&m.m_default);
        }
        break;
    }
    default:
        break; // листья: Ident/Literal/LabelRef/ContextMacro/AstNodeAttr-листья
    }
}

// ── AstNodeBase::children — const-обёртка над collectChildren (копии узлов). ──
//    Единый источник истины «kind → дети» — collectChildren; этот метод только
//    копирует слоты в возвращаемый вектор (для const-потребителей, напр. pipeline).

std::vector<AstNodePtr> AstNodeBase::children() const {
    std::vector<AstNodePtr*> slots;
    const_cast<AstNodeBase*>(this)->collectChildren(slots);
    std::vector<AstNodePtr> out;
    out.reserve(slots.size());
    for (auto* p : slots) {
        if (p) {
            out.push_back(*p);
        }
    }
    return out;
}

// ── Единый хелпер иммутабельности ('^' → attr::ReadOnly) — объявлен в token_base.hpp.
//    Используется конвертером Term→AST и IdentName (manual-конструктор).
bool applyReadonlyFromCaret(AstNodeAttr& node, std::string_view raw, AttrPool* pool) {
    if (raw.empty() || raw.back() != '^') {
        return false;
    }
    if (!canHaveImmutableQualifier(node.kind())) {
        return false; // '^' неприменим к этому kind — диагностика выполняется отдельно (в convert)
    }
    EXPECT(pool && "applyReadonlyFromCaret requires AttrPool");
    auto readonly_id = pool->lookup(attr::ReadOnly);
    EXPECT(readonly_id.has_value() && "Predefined attr::ReadOnly not registered!");
    node.add_attr(readonly_id.value());
    return true;
}

// ── HasText: терм-конструктор — текст читается из Term и нормализуется по kind.
//    m_term хранится для range(); m_text — нормализованный текст (text() читает m_text).
HasText::HasText(ParserToken::Kind k, TermPtr term)
: AstNodeAttr(k, std::move(term)) {
    EXPECT(m_term && "HasText term-constructor requires a source Term");
    m_text = normalizeTermText(k, m_term->getText());
}

// ── Sequence / Binary / ScopeBlock / JumpStmt: терм-конструкторы строят детей.
//    При ctx != nullptr дети рекурсивно строятся через TermToAstConverter (convert/convertSeq,
//    объявлен в ast/term_to_ast.hpp, тот же ast_lib — без loader и без цикла).

Sequence::Sequence(ParserToken::Kind k, TermPtr term, Context* ctx)
: HasText(k, std::move(term)) {
    EXPECT(m_term && "Sequence term-constructor requires a source Term");
    if (ctx) {
        convertChildren(*ctx, m_term, m_body);
    }
}

Binary::Binary(ParserToken::Kind k, TermPtr term, Context* ctx)
: AstNodeAttr(k, std::move(term)) {
    EXPECT(m_term && "Binary term-constructor requires a source Term");
    if (ctx) {
        if (m_term->m_left) {
            m_left = convertChild(*ctx, m_term->m_left);
        }
        if (m_term->m_right) {
            m_right = convertChild(*ctx, m_term->m_right);
        }
    }
}

// ── CallExpr: терм-конструктор — callee из имени Term, args из convertChildren. ──
// Единственный владелец раскладки Ident-вызова: класс-селекция (CallExpr vs IdentName)
// остаётся в конвертере, здесь — только постройка полей из m_term.

CallExpr::CallExpr(ParserToken::Kind k, TermPtr term, Context* ctx)
: AstNodeAttr(k, std::move(term)) {
    EXPECT(m_term && "CallExpr term-constructor requires a source Term");
    if (ctx) {
        m_callee = std::make_shared<IdentName>(m_term);
        std::vector<AstNodePtr> args;
        convertChildren(*ctx, m_term, args);
        if (!args.empty()) {
            m_args = std::move(args);
        }
    }
}

ScopeBlock::ScopeBlock(TermPtr term, Context* ctx, int blockCounter)
: Sequence(ParserToken::Kind::ScopeBlock, std::move(term), ctx)
, m_blockCounter(blockCounter) {
}

JumpStmt::JumpStmt(ParserToken::Kind k, TermPtr term, Context* ctx)
: AstNodeAttr(k, std::move(term)) {
    EXPECT(m_term && "JumpStmt term-constructor requires a source Term");

    // Дети нужны для роли (break vs return) и значения.
    std::vector<AstNodePtr> body;
    if (ctx) {
        convertChildren(*ctx, m_term, body);
    }

    const auto id = m_term->getTermID();
    // Роль определяется по TermID и наличию значения:
    //   INT_PLUS  (++) без значения → break; со значением → return
    //   INT_REPEAT (-+ / +-)        → continue
    //   INT_MINUS (--)              → throw
    if (id == trust::TermID::INT_PLUS || id == trust::TermID::INT_MINUS || id == trust::TermID::INT_REPEAT) {
        if (id == trust::TermID::INT_MINUS) {
            m_kind = ParserToken::Kind::ThrowStmt;
        } else if (id == trust::TermID::INT_REPEAT) {
            m_kind = ParserToken::Kind::ContinueStmt;
        } else { // INT_PLUS
            m_kind = body.empty() ? ParserToken::Kind::BreakStmt : ParserToken::Kind::ReturnStmt;
        }

        // Текст узла AST — '++'/'--'/'-+'; namespace (label) из m_text уходит в m_label.
        std::string_view ns = m_term->getText();
        const char* op = (m_kind == ParserToken::Kind::ReturnStmt || m_kind == ParserToken::Kind::BreakStmt) ? "++"
                         : (m_kind == ParserToken::Kind::ThrowStmt)                                          ? "--"
                                                                                                             : "-+";
        if (!ns.empty() && ns != op) {
            auto labelTerm = Term::Create(trust::TermID::NAME, std::string(ns));
            m_label = std::make_shared<AstNodeAttr>(ParserToken::Kind::Ident, std::move(labelTerm));
        }
    }

    if (!body.empty() && m_kind != ParserToken::Kind::BreakStmt && m_kind != ParserToken::Kind::ContinueStmt) {
        // void return `++ _ ++`: значение `_` — служебный символ, m_value = nullptr.
        if (m_kind == ParserToken::Kind::ReturnStmt && body[0] && body[0]->kind() == ParserToken::Kind::Ident && body[0]->text() == "_") {
            m_value = nullptr;
        } else {
            m_value = std::move(body[0]);
        }
    }
}

// ── ModuleNode: терм-конструктор строит m_body из term->m_sequence (loader-free). ──

ModuleNode::ModuleNode(ParserToken::Kind /*k*/, TermPtr term, Context* ctx)
: ModuleNode(0, std::move(term)) {
    // Терм-конструктор вызывается только из visit_MODULE для сайтов импорта `\module(...)`.
    m_isImport = true;
    if (ctx) {
        // Тело модуля разворачивается: контейнер (SEQUENCE-терм) не оборачивается в ScopeBlock,
        // пользовательские { ... } (BLOCK-терм) сохраняются как ScopeBlock-узлы.
        for (const auto& childTerm : m_term->m_sequence) {
            convertModuleBody(*ctx, childTerm, m_body);
        }
    }
}

// ── Control flow / match: терм-конструкторы строят детей при ctx != nullptr.
//    Раскладка из parser.y (см. комментарии в ast_nodes.hpp). Рекурсия детей через
//    TermToAstConverter (объявлен в ast/term_to_ast.hpp, тот же ast_lib).

IfStmt::IfStmt(ParserToken::Kind k, TermPtr term, Context* ctx)
: ControlFlowStmt(k, std::move(term)) {
    EXPECT(m_term && "IfStmt term-constructor requires a source Term");
    if (!ctx) {
        return;
    }
    // Единая раскладка: m_left=условие, m_right=else,
    // m_sequence=[тело then, branch2, branch3, ...] (branch_i = терм cond_i→body_i).
    if (m_term->m_left) {
        m_cond = convertChild(*ctx, m_term->m_left);
    }
    if (!m_term->m_sequence.empty() && m_term->m_sequence[0]) {
        m_body = convertChild(*ctx, m_term->m_sequence[0]);
    }
    for (size_t i = 1; i < m_term->m_sequence.size(); ++i) {
        const auto& br = m_term->m_sequence[i];
        if (!br) {
            continue;
        }
        AstNodePtr c = br->m_left ? convertChild(*ctx, br->m_left) : nullptr;
        AstNodePtr b = br->m_right ? convertChild(*ctx, br->m_right) : nullptr;
        m_elseifs.emplace_back(std::move(c), std::move(b));
    }
    if (m_term->m_right) {
        m_else = convertChild(*ctx, m_term->m_right);
    }
}

WhileStmt::WhileStmt(ParserToken::Kind k, TermPtr term, Context* ctx)
: ControlFlowStmt(k, std::move(term)) {
    EXPECT(m_term && "WhileStmt term-constructor requires a source Term");
    if (!ctx) {
        return;
    }
    // m_left=cond, m_sequence=[body], m_right=else.
    if (m_term->m_left) {
        m_cond = convertChild(*ctx, m_term->m_left);
    }
    if (!m_term->m_sequence.empty() && m_term->m_sequence[0]) {
        m_body = convertChild(*ctx, m_term->m_sequence[0]);
    }
    if (m_term->m_right) {
        m_else = convertChild(*ctx, m_term->m_right);
    }
}

DoWhileStmt::DoWhileStmt(ParserToken::Kind k, TermPtr term, Context* ctx)
: ControlFlowStmt(k, std::move(term)) {
    EXPECT(m_term && "DoWhileStmt term-constructor requires a source Term");
    if (!ctx) {
        return;
    }
    // do-while: m_left=cond, m_sequence=[body].
    if (!m_term->m_sequence.empty() && m_term->m_sequence[0]) {
        m_body = convertChild(*ctx, m_term->m_sequence[0]);
    }
    if (m_term->m_left) {
        m_cond = convertChild(*ctx, m_term->m_left);
    }
}

MatchStmt::MatchStmt(ParserToken::Kind k, TermPtr term, Context* ctx)
: AstNodeAttr(k, std::move(term)) {
    EXPECT(m_term && "MatchStmt term-constructor requires a source Term");
    if (!ctx) {
        return;
    }
    // Раскладка MATCHING-терма: m_left=значение, m_right=match_body (BLOCK);
    // m_right->m_sequence = [item1, ..., elseItem]; item: m_left=шаблоны (m_sequence=list),
    // m_right=тело; else: m_left=ELLIPSIS.
    if (m_term->m_left) {
        m_value = convertChild(*ctx, m_term->m_left);
    }
    if (m_term->m_right) {
        for (const auto& item : m_term->m_right->m_sequence) {
            if (!item) {
                continue;
            }
            if (item->m_left && item->m_left->getTermID() == trust::TermID::ELLIPSIS) {
                // else: [...] --> body
                m_default = item->m_right ? convertChild(*ctx, item->m_right) : nullptr;
            } else {
                MatchCase c;
                if (item->m_left) {
                    // matches: первый шаблон — сам терм, остальные — в его m_sequence
                    c.patterns.push_back(convertChild(*ctx, item->m_left));
                    for (const auto& p : item->m_left->m_sequence) {
                        c.patterns.push_back(convertChild(*ctx, p));
                    }
                }
                c.body = item->m_right ? convertChild(*ctx, item->m_right) : nullptr;
                m_cases.push_back(std::move(c));
            }
        }
    }
}

// ── Имя узла из Term для операторных термов (VarDecl/FuncDecl/ArgNode):
//    у терма оператора (`:=`, `::=`, ARGUMENT) имя лежит в m_left; иначе — в самом терме.
static std::string_view declNameFromTerm(const TermPtr& term) {
    if (term->m_left) {
        return term->m_left->getText();
    }
    return term->getText();
}

// True, если терм — «чистое» многоточие `<name> := ...;` (forward-объявление): ELLIPSIS без
// детей. Для извлечения из коллекции `... dict` (ELLIPSIS c rval в m_right) — false, такой
// терм не является признаком forward-объявления.
static bool isForwardEllipsisTerm(const TermPtr& term) {
    if (!term || term->getTermID() != trust::TermID::ELLIPSIS) {
        return false;
    }
    return !term->m_left && !term->m_right && term->m_sequence.empty() && !term->m_args.has_value();
}

// True, если терм — нативный импорт `<name>(...) := %sym...;` (m_right — native-терм `%sym`).
static bool isNativeImportTerm(const TermPtr& term) {
    return term && term->getTermID() == trust::TermID::NATIVE;
}

// C++-имя нативного импорта: убираем ведущий '%' (`%abs`→"abs", `%std::sqrt`→"std::sqrt").
static std::string nativeImportName(const TermPtr& term) {
    std::string_view t = term ? term->getText() : std::string_view{};
    if (t.size() > 1 && t[0] == '%') {
        t.remove_prefix(1);
    }
    return std::string(t);
}

// ── VarDecl/FuncDecl/ArgNode: терм-конструкторы — имя из m_left (fallback term->getText()).
VarDecl::VarDecl(TermPtr term, AstNodePtr type, AstNodePtr initializer)
: Decl(std::move(term))
, m_initializer(std::move(initializer)) {
    EXPECT(m_term && "VarDecl term-constructor requires a source Term");
    m_kind = ParserToken::Kind::VarDecl;
    m_type = std::move(type);
    m_text = normalizeTermText(ParserToken::Kind::VarDecl, declNameFromTerm(m_term));
}

// ── VarDecl: uniform терм-конструктор (kind = VarDecl). ──
// Единственный владелец раскладки операторного терма `:=` (CREATE_NAME):
//   m_type из m_left->m_type, m_initializer из m_right. Охват [имя, expr] вычисляется
//   в VarDecl::range() на лету — Term не мутируется.
VarDecl::VarDecl(ParserToken::Kind /*k*/, TermPtr term, Context* ctx)
: VarDecl(std::move(term)) {
    if (ctx && m_term) {
        if (m_term->m_left && m_term->m_left->m_type) {
            m_type = convertChild(*ctx, m_term->m_left->m_type);
        }
        // Предварительное (forward) объявление `x:Type := ...;`: m_right — чистое многоточие
        // вместо инициализатора. m_initializer остаётся nullptr (forward declaration).
        if (isForwardEllipsisTerm(m_term->m_right)) {
            return;
        }
        if (m_term->m_right) {
            std::vector<AstNodePtr> body;
            convertChildren(*ctx, m_term, body);
            if (body.size() >= 2 && body[1]) {
                m_initializer = std::move(body[1]);
            } else if (body.size() >= 1 && body[0]) {
                m_initializer = std::move(body[0]);
            }
        }
    }
}

// ── DestructureDecl: `t1, ..., tN := [... ]source;` ──
// Цели слева — список имён (assign_items: цепочка m_left: a→b→c), источник — m_right
// (для `...` — ELLIPSIS с вложенным выражением; для кортежа — само выражение).
DestructureDecl::DestructureDecl(ParserToken::Kind /*k*/, TermPtr term, Context* ctx)
: AstNodeAttr(ParserToken::Kind::DestructureDecl, std::move(term)) {
    EXPECT(m_term && "DestructureDecl term-constructor requires a source Term");
    if (!ctx || !m_term) {
        return;
    }
    m_isAssign = m_term->getTermID() == trust::TermID::ASSIGN;
    m_isSpread = m_term->m_right && m_term->m_right->getTermID() == trust::TermID::ELLIPSIS;
    if (m_term->m_left) {
        for (const TermPtr* t = &m_term->m_left; *t; t = &(*t)->m_left) {
            // Цель — имя переменной из терма lval; IdentName конструируем напрямую (convertChild
            // для lval-терма может дать не тот kind). rest-цель (`rest...`) — lval с суффиксом
            // ELLIPSIS в m_right (грамматика assign_item: `lval ELLIPSIS`). Явная аннотация типа
            // цели (`a:Int32`) хранится в lval->m_type (грамматика `lval_var: name type_item`
            // → m_type = type_item); конвертируем её в узел типа для семантики/кодгена.
            const bool isRest = (*t)->m_right && (*t)->m_right->getTermID() == trust::TermID::ELLIPSIS;
            m_targets.push_back(std::make_shared<IdentName>(std::string((*t)->getText())));
            m_targetIsRest.push_back(isRest);
            m_targetTypeNodes.push_back((*t)->m_type ? convertChild(*ctx, (*t)->m_type) : AstNodePtr{});
        }
    }
    TermPtr src;
    if (m_isSpread) {
        if (m_term->m_right && m_term->m_right->m_right) {
            src = m_term->m_right->m_right;
        }
    } else {
        src = m_term->m_right;
    }
    if (src) {
        m_source = convertChild(*ctx, src);
    }
}

std::string DestructureDecl::dump(size_t indent) const {
    std::string result(indent, ' ');
    result += "DestructureDecl";
    if (m_isAssign) {
        result += " (assign)";
    }
    result += "\n";
    for (size_t i = 0; i < m_targets.size(); ++i) {
        const auto& t = m_targets[i];
        const bool isRest = i < m_targetIsRest.size() && m_targetIsRest[i];
        result += std::string(indent + 2, ' ') + "target: " + (t ? std::string(t->text()) : std::string{}) + (isRest ? "..." : "") + "\n";
    }
    if (m_source) {
        result += m_source->dump(indent + 2);
    }
    return result;
}

MapperRange DestructureDecl::range() const {
    if (!m_term) {
        return {};
    }
    // Полный охват `t1, ..., tN := [... ]source;`: начало — первый lval-терм цепочки имён
    // (m_term->m_left), конец — источник (m_source->range(); для `... source` источник — операнд
    // ELLIPSIS). Базовый m_term (CREATE_NAME `:=`) имеет m_mapperRange на оператор `:=`; без
    // расширения маппинг деструктуризации сужался бы до одного оператора (как VarDecl::range()).
    const bool beginOk = m_term->m_left && !m_term->m_left->m_mapperRange.isInvalid();
    const bool endOk = m_source && !m_source->range().isInvalid();
    if (beginOk && endOk && m_term->m_left->m_mapperRange.begin.fileIdx() == m_source->range().end.fileIdx() &&
        m_term->m_left->m_mapperRange.begin.offset() <= m_source->range().end.offset()) {
        return MapperRange{m_term->m_left->m_mapperRange.begin, m_source->range().end};
    }
    return AstNodeAttr::range();
}

FuncDecl::FuncDecl(TermPtr term)
: Decl(std::move(term)) {
    EXPECT(m_term && "FuncDecl term-constructor requires a source Term");
    m_kind = ParserToken::Kind::FuncDecl;
    m_text = normalizeTermText(ParserToken::Kind::FuncDecl, declNameFromTerm(m_term));
}

// ── FuncDecl: uniform терм-конструктор (kind = FuncDecl). ──
// Единственный владелец раскладки функции. Два layout:
//   1. Функция с сигнатурой в m_left: CREATE_NAME (`:=`) `name(params):Type := {body}`
//      (в т.ч. native — %name(params):Type := {body}); m_type из m_left->m_type,
//      params из m_left->m_args (ARGUMENT: имя в m_left, тип в m_right), body из m_right.
//      Диапазон функции [имя, оператор] (признак функции) вычисляется в FuncDecl::range()
//      на лету — Term не мутируется.
//   2. lambda/iterator (FUNCTION/COROUTINE/ITERATOR): split params/body из convertChildren.
FuncDecl::FuncDecl(ParserToken::Kind /*k*/, TermPtr term, Context* ctx)
: FuncDecl(std::move(term)) {
    if (!ctx || !m_term) {
        return;
    }
    // CREATE_NAME (`:=`) с сигнатурой функции в m_left (m_left->isCall()) — это функция.
    const bool sigInLeft = m_term->getTermID() == trust::TermID::CREATE_NAME && m_term->m_left && m_term->m_left->isCall();
    if (sigInLeft) {
        if (m_term->m_left->m_type) {
            m_type = convertChild(*ctx, m_term->m_left->m_type);
        }
        m_params = std::vector<AstNodePtr>{};
        // Параметры: m_args на сигнатуре (ARGUMENT: имя в m_left, тип в m_right).
        // Раскладка m_type — в ArgNode-конструкторе.
        if (m_term->m_left->m_args) {
            for (const auto& [name, argTerm] : *m_term->m_left->m_args) {
                (void)name;
                if (!argTerm) {
                    continue;
                }
                m_params->push_back(std::make_shared<ArgNode>(ParserToken::Kind::ArgNode, argTerm, ctx));
            }
        }
        // Нативный импорт `<name>(...) := %sym...;` — АЛИАС на нативную функцию: C++-функция
        // НЕ эмитится, вызовы name(...) переписываются в прямой вызов %sym(...). m_body пуст.
        if (isNativeImportTerm(m_term->m_right)) {
            m_isNativeImport = true;
            m_nativeName = nativeImportName(m_term->m_right);
            return;
        }
        // Тело: m_right — block. Предварительное объявление `%func():Type := ...;` —
        // m_right — чистое многоточие вместо тела → m_body = nullopt (forward declaration).
        if (isForwardEllipsisTerm(m_term->m_right)) {
            return;
        }
        if (m_term->m_right) {
            std::vector<AstNodePtr> fnBody;
            convertChildren(*ctx, m_term->m_right, fnBody);
            m_body = std::move(fnBody);
        }
    } else {
        // Lambda/iterator: split params (ArgNode) / body из convertChildren.
        std::vector<AstNodePtr> body;
        convertChildren(*ctx, m_term, body);
        std::vector<AstNodePtr> params;
        std::vector<AstNodePtr> fnBody;
        bool inParams = true;
        for (auto& tok : body) {
            if (inParams && tok && tok->kind() == ParserToken::Kind::ArgNode) {
                params.push_back(std::move(tok));
            } else {
                inParams = false;
                fnBody.push_back(std::move(tok));
            }
        }
        if (!params.empty()) {
            m_params = std::move(params);
        }
        if (!fnBody.empty()) {
            m_body = std::move(fnBody);
        }
    }
}

ArgNode::ArgNode(TermPtr term, AstNodePtr type, AstNodePtr value)
: HasText(ParserToken::Kind::ArgNode, std::move(term))
, m_type(std::move(type))
, m_value(std::move(value)) {
    EXPECT(m_term && "ArgNode term-constructor requires a source Term");
    m_text = normalizeTermText(ParserToken::Kind::ArgNode, declNameFromTerm(m_term));
}

// ── ArgNode: uniform терм-конструктор (kind = ArgNode). ──
// КАНОНИЧЕСКАЯ раскладка аргумента: имя в m_left, явный тип в m_type (страховка `name:Type`
// без значения — m_right->m_type), значение в m_right. Единая для параметров функций, элементов
// словаря/enum/variant и аргументов вызова (терм_to_ast::appendDictElementsFromArgs использует
// manual-конструктор; здесь — uniform путь visit_ARGUMENT / параметры функции).
ArgNode::ArgNode(ParserToken::Kind /*k*/, TermPtr term, Context* ctx)
: ArgNode(std::move(term)) {
    if (ctx && m_term) {
        // Тип: из m_type (канонический слот); страховка — m_right->m_type (`name:Type` без =value).
        if (m_term->m_type) {
            m_type = convertChild(*ctx, m_term->m_type);
        } else if (m_term->m_right && m_term->m_right->m_type) {
            m_type = convertChild(*ctx, m_term->m_right->m_type);
        }
        // Значение: у ARGUMENT m_right — значение (name=value / name:Type=value).
        if (m_term->getTermID() == trust::TermID::ARGUMENT && m_term->m_right) {
            m_value = convertChild(*ctx, m_term->m_right);
        }
    }
}

// ── Helper: добавить строку "label: <child-dump>" с отступом (если child не пуст).
//    Единый формат для dump() узлов с детьми (Binary, ArgNode, VarDecl, If/While/DoWhile, Match).
//    Сохраняет точный прежний вывод: "\n" + indent + "label: " + child->dump(indent+2). ──
static void dumpLabeled(std::string& out, size_t indent, std::string_view label, const AstNodePtr& child) {
    if (!child) {
        return;
    }
    out += "\n";
    out += std::string(indent, ' ');
    out += label;
    out += ": ";
    out += child->dump(indent + 2);
}

// ── Sequence::dumpBody — дамп содержимого std::vector<AstNodePtr> ──

void Sequence::dumpBody(std::string& result, const std::vector<AstNodePtr>& body, size_t indent, size_t child_indent) {
    if (body.empty()) {
        return;
    }
    result += "\n";
    for (size_t i = 0; i < body.size(); i++) {
        result += std::string(indent, ' ');
        if (body[i]) {
            result += body[i]->dump(child_indent);
        }
        if (i + 1 < body.size()) {
            result += "\n";
        }
    }
}

// ── Literal::dump ──

std::string Literal::dump(size_t indent) const {
    std::string result = std::string(indent, ' ');
    result += ParserToken::name(kind());
    if (kind() == ParserToken::Kind::StrChar || kind() == ParserToken::Kind::StrWide) {
        result += " \"";
        result += text();
        result += "\"";
    } else {
        // Numeric and other non-string literals are printed without quotes
        // (matching the convention used by clang -ast-dump).
        result += " ";
        result += text();
    }
    return result;
}

// ── ContextMacro::dump ──

std::string ContextMacro::dump(size_t indent) const {
    std::string result = std::string(indent, ' ');
    result += ParserToken::name(kind());
    if (!text().empty()) {
        result += " '";
        result += text();
        result += "'";
    }
    return result;
}

// ── Binary::dump ──

std::string Binary::dump(size_t indent) const {
    std::string result = AstNodeAttr::dump(indent);
    dumpLabeled(result, indent, "left", m_left);
    dumpLabeled(result, indent, "right", m_right);
    return result;
}

// ── CallExpr::dump ──

std::string CallExpr::dump(size_t indent) const {
    std::string result = AstNodeAttr::dump(indent);
    dumpLabeled(result, indent, "callee", m_callee);
    if (m_args) {
        result += "\n";
        result += std::string(indent, ' ');
        result += "args:";
        for (size_t i = 0; i < m_args->size(); i++) {
            const auto& arg = (*m_args)[i];
            result += "\n";
            if (arg) {
                result += arg->dump(indent + 2);
            } else {
                result += std::string(indent + 2, ' ') + "(null)";
            }
        }
    }
    return result;
}

// ── ArgNode::dump ──

std::string ArgNode::dump(size_t indent) const {
    std::string result = detail::dumpQuotedName(kind(), text(), indent);
    dumpLabeled(result, indent, "type", m_type);
    dumpLabeled(result, indent, "value", m_value);
    return result;
}

// ── Sequence::dump ──

std::string Sequence::dump(size_t indent) const {
    std::string result = AstNodeAttr::dump(indent);
    dumpBody(result, m_body, indent, indent + 2);
    return result;
}

// ── RangeExpr::dump ──

std::string RangeExpr::dump(size_t indent) const {
    std::string result = AstNodeAttr::dump(indent);
    if (elementType != INVALID_TYPE_ID) {
        result += "elem=";
        result += std::to_string(elementType);
        result += ' ';
    }
    dumpBody(result, m_body, indent, indent + 2);
    return result;
}

// ── ScopeBlock::dump ──
std::string ScopeBlock::dump(size_t indent) const {
    std::string result = AstNodeAttr::dump(indent);
    if (!text().empty()) {
        result += text();
    } else if (m_blockCounter > 0) {
        result += std::to_string(m_blockCounter);
    }
    dumpBody(result, m_body, indent, indent + 2);
    return result;
}

// ── ModuleNode::dump ──

std::string ModuleNode::dump(size_t indent) const {
    // Паттерн ScopeBlock::dump: заголовок (kind) + имя, затем дети ОДИН раз.
    // НЕ используем Sequence::dump — он уже печатает m_body (иначе было бы дважды).
    std::string result = detail::dumpQuotedName(kind(), text(), indent);
    dumpBody(result, m_body, indent, indent + 2);
    return result;
}

// ── Decl::dump ──

std::string Decl::dump(size_t indent) const {
    // Имя выводится один раз (ident_name/dump даёт "Kind 'text'"); ранее здесь
    // ошибочно добавлялся второй "'text'" → "Kind 'x' 'x'". Исправлено.
    std::string result = detail::dumpQuotedName(kind(), text(), indent);
    if (m_type) {
        result += "\n";
        result += std::string(indent, ' ');
        result += "type: ";
        result += m_type->dump(indent + 2);
    }
    return result;
}

// ── FuncDecl::dump ──

std::string FuncDecl::dump(size_t indent) const {
    std::string result = Decl::dump(indent);
    if (m_params) {
        result += "\n";
        result += std::string(indent, ' ');
        result += "params:";
        for (size_t i = 0; i < m_params->size(); ++i) {
            result += "\n";
            const auto& param = (*m_params)[i];
            if (param) {
                result += param->dump(indent + 2);
            } else {
                result += std::string(indent + 2, ' ') + "(null)";
            }
        }
    }
    if (m_body) {
        result += "\n";
        result += std::string(indent, ' ');
        result += "body:";
        for (size_t i = 0; i < m_body->size(); ++i) {
            result += "\n";
            const auto& stmt = (*m_body)[i];
            if (stmt) {
                result += stmt->dump(indent + 2);
            } else {
                result += std::string(indent + 2, ' ') + "(null)";
            }
        }
    } else {
        result += " (forward)";
    }
    return result;
}

// ── FuncDecl::signature — строка сигнатуры для контекст-макроса @__FUNCSIG__ ──

std::string FuncDecl::signature(std::string_view namespace_path) const {
    std::string name(text());
    if (!name.empty() && name.front() == '%') {
        name.erase(0, 1);
    }
    std::string sig;
    if (!namespace_path.empty()) {
        sig.assign(namespace_path);
        sig += "::";
    }
    sig += name;
    sig += "(";
    if (m_params) {
        bool first = true;
        for (const auto& p : *m_params) {
            if (!p || p->kind() != ParserToken::Kind::ArgNode) {
                continue;
            }
            const auto& pd = static_cast<const ArgNode&>(*p);
            if (!first) {
                sig += ", ";
            }
            first = false;
            sig += pd.text();
            if (pd.m_type) {
                sig += ":";
                sig += pd.m_type->text();
            }
        }
    }
    sig += ")";
    if (m_type) {
        sig += ":";
        sig += m_type->text();
    }
    return sig;
}

MapperRange FuncDecl::blockRange() const noexcept {
    // Тело функции — блок { ... } лежит в m_term->m_right (терм CREATE_TYPE ::=).
    // Его range используется для определения строк { и }, чтобы сгенерированный код
    // повторял раскладку исходника. Для test-only узлов без терма — invalid range.
    if (m_term && m_term->m_right) {
        return m_term->m_right->m_mapperRange;
    }
    return {};
}

// ── JumpStmt::dump ──

std::string JumpStmt::dump(size_t indent) const {
    std::string result = AstNodeAttr::dump(indent);
    if (m_label) {
        result += " label:";
        result += m_label->text();
    }
    if (m_value) {
        result += "\n";
        result += std::string(indent, ' ');
        result += "value: ";
        result += m_value->dump(indent + 2);
    } else {
        result += " void";
    }
    return result;
}

// ── VarDecl::dump ──

std::string VarDecl::dump(size_t indent) const {
    std::string result = Decl::dump(indent);
    if (m_initializer) {
        dumpLabeled(result, indent, "init", m_initializer);
    } else {
        result += " (forward)";
    }
    return result;
}

// ── VarDecl::nameRange ──
// Диапазон реального имени. Базовый m_term для `x := ...` — терм оператора ':=':
// его m_mapperRange указывает на оператор, а имя лежит в m_term->m_left.
MapperRange VarDecl::nameRange() const noexcept {
    if (m_term && m_term->m_left && !m_term->m_left->m_mapperRange.isInvalid()) {
        return m_term->m_left->m_mapperRange;
    }
    return {};
}

// ── ControlFlowStmt::dumpControlFlow ──
// Единый дамп общих полей cond/body/else (в порядке WhileStmt: cond → body → else).
std::string ControlFlowStmt::dumpControlFlow(size_t indent) const {
    std::string result = AstNodeAttr::dump(indent);
    dumpLabeled(result, indent, "cond", m_cond);
    dumpLabeled(result, indent, "body", m_body);
    dumpLabeled(result, indent, "else", m_else);
    return result;
}

// ── IfStmt::dump ──

std::string IfStmt::dump(size_t indent) const {
    std::string result = AstNodeAttr::dump(indent);
    dumpLabeled(result, indent, "cond", m_cond);
    dumpLabeled(result, indent, "then", m_body);
    for (size_t i = 0; i < m_elseifs.size(); ++i) {
        result += "\n" + std::string(indent, ' ') + "elseif" + std::to_string(i) + ":";
        if (m_elseifs[i].first) {
            result += "\n" + std::string(indent + 2, ' ') + "cond: " + m_elseifs[i].first->dump(indent + 4);
        }
        if (m_elseifs[i].second) {
            result += "\n" + std::string(indent + 2, ' ') + "body: " + m_elseifs[i].second->dump(indent + 4);
        }
    }
    dumpLabeled(result, indent, "else", m_else);
    return result;
}

// ── WhileStmt::dump ──

std::string WhileStmt::dump(size_t indent) const {
    return dumpControlFlow(indent);
}

// ── DoWhileStmt::dump ──
// do-while печатает body → cond (порядок исходника «тело→условие»).

std::string DoWhileStmt::dump(size_t indent) const {
    std::string result = AstNodeAttr::dump(indent);
    dumpLabeled(result, indent, "body", m_body);
    dumpLabeled(result, indent, "cond", m_cond);
    return result;
}

// ── MatchStmt::dump ──

std::string MatchStmt::dump(size_t indent) const {
    std::string result = AstNodeAttr::dump(indent);
    dumpLabeled(result, indent, "value", m_value);
    for (size_t i = 0; i < m_cases.size(); ++i) {
        result += "\n" + std::string(indent, ' ') + "case" + std::to_string(i) + ":";
        for (size_t j = 0; j < m_cases[i].patterns.size(); ++j) {
            result += "\n" + std::string(indent + 2, ' ') + "pat" + std::to_string(j) + ": " + m_cases[i].patterns[j]->dump(indent + 4);
        }
        if (m_cases[i].body) {
            result += "\n" + std::string(indent + 2, ' ') + "body: " + m_cases[i].body->dump(indent + 4);
        }
    }
    dumpLabeled(result, indent, "default", m_default);
    return result;
}

// ── LabelRef (kind=GotoStmt|LabelStmt) / SemicolonStmt: dump ──
// Синтетические узлы (без Term): текст читается из собственного поля. Имя kind выводится
// из ParserToken::name(kind()) — поэтому один dump на оба kinds (GotoStmt/LabelStmt).

std::string LabelRef::dump(size_t indent) const {
    return detail::dumpQuotedName(kind(), m_name, indent);
}

std::string SemicolonStmt::dump(size_t indent) const {
    std::string result = AstNodeAttr::dump(indent);
    dumpLabeled(result, indent, "expr", m_expr);
    return result;
}

// ── Диапазоны узлов: вычисляются на лету по узлам-детям, исходный Term НЕ мутируется. ──
// Ранее конвертер TermToAstConverter расширял term->m_mapperRange (expandTermRangeToChildren /
// expandControlFlowRange и блок в FuncDecl-конструкторе). Теперь полный охват statement'а
// возвращается override'ами range() узлов, Term остаётся неизменным. Охват считается по
// РАНЖАМ УЗЛОВ-ДЕТЕЙ (их range() уже рекурсивно корректен), а не по сырым термам.

// [min-begin, max-end] по ранжам узлов-детей (guard: только дети из того же файла, что и первый).
static std::optional<MapperRange> spanOfNodes(const std::vector<AstNodePtr>& children) {
    bool have = false;
    trust::MapperLocation begin, end;
    auto consider = [&](const MapperRange& r) {
        if (r.isInvalid()) {
            return;
        }
        if (!have) {
            begin = r.begin;
            end = r.end;
            have = true;
            return;
        }
        if (r.begin.fileIdx() != begin.fileIdx()) {
            return; // макро-раскрытие: ребёнок в другом псевдо-файле — пропускаем
        }
        if (r.begin.offset() < begin.offset()) {
            begin = r.begin;
        }
        if (r.end.offset() > end.offset()) {
            end = r.end;
        }
    };
    for (const auto& c : children) {
        if (c) {
            consider(c->range());
        }
    }
    if (have && begin.offset() <= end.offset()) {
        return trust::MapperRange{begin, end};
    }
    return std::nullopt;
}

MapperRange Binary::range() const {
    if (!m_term) {
        return {};
    }
    // [left.begin, right.end] — обе стороны обязательны (операторный терм несёт range только
    // оператора). Совпадает с прежним expandTermRangeToChildren, но охват читается из детей.
    if (m_left && m_right) {
        const MapperRange l = m_left->range();
        const MapperRange r = m_right->range();
        if (!l.isInvalid() && !r.isInvalid() && l.begin.fileIdx() == r.end.fileIdx() && l.begin.offset() <= r.end.offset()) {
            return MapperRange{l.begin, r.end};
        }
    }
    return AstNodeAttr::range();
}

MapperRange ControlFlowStmt::range() const {
    if (!m_term) {
        return {};
    }
    if (auto r = spanOfNodes({m_cond, m_body, m_else})) {
        return *r;
    }
    return AstNodeAttr::range();
}

MapperRange IfStmt::range() const {
    if (!m_term) {
        return {};
    }
    std::vector<AstNodePtr> children;
    children.reserve(2 + m_elseifs.size() * 2 + 1);
    children.push_back(m_cond);
    children.push_back(m_body);
    for (const auto& [cond, body] : m_elseifs) {
        children.push_back(cond);
        children.push_back(body);
    }
    children.push_back(m_else);
    if (auto r = spanOfNodes(children)) {
        return *r;
    }
    return AstNodeAttr::range();
}

MapperRange MatchStmt::range() const {
    if (!m_term) {
        return {};
    }
    std::vector<AstNodePtr> children;
    children.push_back(m_value);
    for (const auto& c : m_cases) {
        for (const auto& p : c.patterns) {
            children.push_back(p);
        }
        children.push_back(c.body);
    }
    children.push_back(m_default);
    if (auto r = spanOfNodes(children)) {
        return *r;
    }
    return AstNodeAttr::range();
}

MapperRange VarDecl::range() const {
    if (!m_term) {
        return {};
    }
    // [имя, инициализатор] — обе стороны обязательны (прежний expandTermRangeToChildren).
    // Имя из nameRange() (m_term->m_left), инициализатор — узел-ребёнок.
    if (m_initializer) {
        const MapperRange n = nameRange();
        const MapperRange init = m_initializer->range();
        if (!n.isInvalid() && !init.isInvalid() && n.begin.fileIdx() == init.end.fileIdx() && n.begin.offset() <= init.end.offset()) {
            return MapperRange{n.begin, init.end};
        }
    }
    return AstNodeAttr::range();
}

MapperRange FuncDecl::range() const {
    if (!m_term) {
        return {};
    }
    // CREATE_NAME (`:=`) с сигнатурой функции в m_left: [имя, оператор] (без тела).
    // Совпадает с прежней мутацией term->m_mapperRange в конструкторе (см. FuncDecl-конструктор).
    if (m_term->getTermID() == trust::TermID::CREATE_NAME && m_term->m_left && m_term->m_left->isCall() && !m_term->m_left->m_mapperRange.isInvalid() &&
        !m_term->m_mapperRange.isInvalid() && m_term->m_left->m_mapperRange.begin.fileIdx() == m_term->m_mapperRange.end.fileIdx() &&
        m_term->m_left->m_mapperRange.begin.offset() <= m_term->m_mapperRange.end.offset()) {
        return MapperRange{m_term->m_left->m_mapperRange.begin, m_term->m_mapperRange.end};
    }
    return AstNodeAttr::range();
}

} // namespace trust