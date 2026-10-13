// src/ast/nodes/ast_stmt.cpp
// Операторы AST: JumpStmt, ControlFlowStmt/IfStmt/WhileStmt/DoWhileStmt, MatchStmt,
// WithStmt, try/catch, синтетические узлы lowering и ErrorExpr, LabelRef, SemicolonStmt.
#include "ast/ast_nodes.hpp"
#include "ast/nodes/ast_helpers.hpp"
#include "ast/term_to_ast.hpp"
#include "ast/token_type.hpp"
#include "syntax/term/term.h"
#include "utils/error.hpp"
#include <optional>
#include <string>
#include <vector>
namespace trust {

JumpStmt::JumpStmt(ParserToken::Kind k, TermPtr term, Context* ctx)
: AstNodeAttr(k, term) {
    EXPECT(term && "JumpStmt term-constructor requires a source Term");

    // Дети нужны для роли (break vs return) и значения.
    std::vector<AstNodePtr> body;
    if (ctx) {
        convertChildren(*ctx, term, body);
    }

    const auto id = term->getTermID();
    // Роль задаётся терминалом-маркером (после унификации передач управления):
    //   INT_PLUS (|+|)      → return
    //   INT_MINUS (|-|)     → throw
    //   INT_BREAK (|>|)     → break
    //   INT_CONTINUE (|<|)  → continue
    if (id == trust::TermID::INT_PLUS || id == trust::TermID::INT_MINUS || id == trust::TermID::INT_BREAK || id == trust::TermID::INT_CONTINUE) {
        if (id == trust::TermID::INT_MINUS) {
            m_kind = ParserToken::Kind::ThrowStmt;
        } else if (id == trust::TermID::INT_BREAK) {
            m_kind = ParserToken::Kind::BreakStmt;
        } else if (id == trust::TermID::INT_CONTINUE) {
            m_kind = ParserToken::Kind::ContinueStmt;
        } else { // INT_PLUS
            m_kind = ParserToken::Kind::ReturnStmt;
        }

        // Метка/namespace берётся из текста терма; собственного текста у JumpStmt нет.
        std::string_view ns = term->getText();
        const char* op = jumpMarkerGlyph(id);
        if (!ns.empty() && ns != op) {
            auto labelTerm = Term::Create(trust::TermID::NAME, std::string(ns));
            // Метка — ИДЕНТИФИКАТОР: класс обязан соответствовать kind→классу из PARSER_TOKEN_KINDS
            // (Ident → IdentName). Иначе `is<IdentName>()`/`as<IdentName>()` дают UB на объекте
            // AstNodeAttr (нарушение инварианта «kind→класс — канонический реестр»).
            m_label = std::make_shared<IdentName>(std::move(labelTerm));
        }
    }

    if (!body.empty() && m_kind != ParserToken::Kind::BreakStmt && m_kind != ParserToken::Kind::ContinueStmt) {
        // void return `++ _ ++`: значение `_` - служебный символ, m_value = nullptr.
        if (m_kind == ParserToken::Kind::ReturnStmt && body[0] && body[0]->kind() == ParserToken::Kind::Ident && isNoneMarker(body[0].get())) {
            m_value = nullptr;
        } else {
            m_value = std::move(body[0]);
        }
    }
}

IfStmt::IfStmt(ParserToken::Kind k, TermPtr term, Context* ctx)
: ControlFlowStmt(k, term) {
    EXPECT(term && "IfStmt term-constructor requires a source Term");
    if (!ctx) {
        return;
    }
    // Единая раскладка: m_left=условие, m_right=else,
    // m_sequence=[тело then, branch2, branch3, ...] (branch_i = терм cond_i→body_i).
    if (term->m_left) {
        m_cond = convertChild(*ctx, term->m_left);
    }
    if (!term->m_sequence.empty() && term->m_sequence[0]) {
        m_body = convertChild(*ctx, term->m_sequence[0]);
    }
    for (size_t i = 1; i < term->m_sequence.size(); ++i) {
        const auto& br = term->m_sequence[i];
        if (!br) {
            continue;
        }
        AstNodePtr c = br->m_left ? convertChild(*ctx, br->m_left) : nullptr;
        AstNodePtr b = br->m_right ? convertChild(*ctx, br->m_right) : nullptr;
        m_elseifs.emplace_back(std::move(c), std::move(b));
    }
    if (term->m_right) {
        m_else = convertChild(*ctx, term->m_right);
    }
    std::vector<AstNodePtr> all{m_cond, m_body};
    for (const auto& [cond, body] : m_elseifs) {
        all.push_back(cond);
        all.push_back(body);
    }
    all.push_back(m_else);
    if (auto r = rangeOfNodes(all)) {
        setRange(*r);
    }
}

WhileStmt::WhileStmt(ParserToken::Kind k, TermPtr term, Context* ctx)
: ControlFlowStmt(k, term) {
    EXPECT(term && "WhileStmt term-constructor requires a source Term");
    if (!ctx) {
        return;
    }
    // m_left=cond, m_sequence=[body], m_right=else.
    if (term->m_left) {
        m_cond = convertChild(*ctx, term->m_left);
    }
    if (!term->m_sequence.empty() && term->m_sequence[0]) {
        m_body = convertChild(*ctx, term->m_sequence[0]);
    }
    if (term->m_right) {
        m_else = convertChild(*ctx, term->m_right);
    }
    if (auto r = rangeOfNodes({m_cond, m_body, m_else})) {
        setRange(*r);
    }
}

DoWhileStmt::DoWhileStmt(ParserToken::Kind k, TermPtr term, Context* ctx)
: ControlFlowStmt(k, term) {
    EXPECT(term && "DoWhileStmt term-constructor requires a source Term");
    if (!ctx) {
        return;
    }
    // do-while: m_left=cond, m_sequence=[body].
    if (!term->m_sequence.empty() && term->m_sequence[0]) {
        m_body = convertChild(*ctx, term->m_sequence[0]);
    }
    if (term->m_left) {
        m_cond = convertChild(*ctx, term->m_left);
    }
    if (auto r = rangeOfNodes({m_cond, m_body})) {
        setRange(*r);
    }
}

WithStmt::WithStmt(ParserToken::Kind k, TermPtr term, Context* ctx)
: AstNodeAttr(k, term) {
    EXPECT(term && "WithStmt term-constructor requires a source Term");
    if (!ctx) {
        return;
    }
    // Раскладка WITH-терма (parser.y `with`):
    //   m_args = биндинги (name=value, ARGUMENT-термы)
    //   m_right = тело (BLOCK), m_sequence[0] = else (BLOCK).
    // Форма `WITH lval` (блокировка ссылки: m_right - не BLOCK) вне скоупа этой итерации (RAII
    // захват значений) - диагностируем «not implemented».
    if (term->m_right && term->m_right->getTermID() != trust::TermID::BLOCK) {
        ctx->diag().report(Severity::Error, term->m_mapperRange, "reference capture in 'with' is not implemented yet");
        return;
    }
    if (term->m_right) {
        m_body = convertChild(*ctx, term->m_right);
    }
    if (!term->m_sequence.empty() && term->m_sequence[0]) {
        // Ветка else из body_else: FOLLOW-терм `[ELLIPSIS] --> body` (COMMA [ ... ] --> body).
        // Конвертация целиком дала бы IfStmt(Ellipsis, body) - извлекаем тело (m_right) напрямую.
        const auto& elseTerm = term->m_sequence[0];
        if (elseTerm->getTermID() == trust::TermID::FOLLOW && elseTerm->m_left && elseTerm->m_left->getTermID() == trust::TermID::ELLIPSIS &&
            elseTerm->m_right) {
            m_else = convertChild(*ctx, elseTerm->m_right);
        } else {
            m_else = convertChild(*ctx, elseTerm);
        }
    }
    // Биндинги → ЕДИНЫЙ список пар (lock, binding) в m_locks:
    //   * `lock`   = RefTakeExpr (источник захвата, из `*ref`/`*^ref`) или nullptr (обычное значение);
    //   * `binding`= VarDecl (имя из m_left, тип/инициализатор) или nullptr (голый `*ref` без имени).
    // Locker-темп и авторазыменование создаёт ПОЗЖЕ LOWERING (инвариант «временные - анализатор»).
    if (term->m_args) {
        for (const auto& entry : *term->m_args) {
            const auto& argTerm = entry.second;
            if (!argTerm) {
                continue;
            }
            AstNodePtr lock;    // RefTakeExpr (источник захвата) или nullptr
            AstNodePtr binding; // VarDecl (значение) или nullptr
            if (argTerm->getTermID() == trust::TermID::TAKE) {
                // Голый `*ref` / `*^ref` (без имени): лок без биндинга - временный (снимается сразу).
                lock = convertChild(*ctx, argTerm);
            } else {
                // ARGUMENT-терм `name=value` (м.б. `name = *ref` - захват со значением).
                AstNodePtr type;
                if (argTerm->m_type) {
                    type = convertChild(*ctx, argTerm->m_type);
                } else if (argTerm->m_right && argTerm->m_right->m_type) {
                    type = convertChild(*ctx, argTerm->m_right->m_type);
                }
                AstNodePtr value = argTerm->m_right ? convertChild(*ctx, argTerm->m_right) : nullptr;
                if (value && value->kind() == ParserToken::Kind::RefTakeExpr) {
                    // Захват ссылки со значением: lock = источник захвата, binding = переменная,
                    // удерживающая значение до конца тела. Инициализатор binding - отдельная копия
                    // RefTakeExpr (для типизации семантикой); lock - источник для Locker-темпа.
                    lock = std::move(value);
                    AstNodePtr bindingInit = argTerm->m_right ? convertChild(*ctx, argTerm->m_right) : nullptr;
                    binding = std::make_shared<VarDecl>(argTerm, std::move(type), std::move(bindingInit));
                } else {
                    // Обычный value-биндинг (без захвата).
                    binding = std::make_shared<VarDecl>(argTerm, std::move(type), std::move(value));
                }
                if (binding) {
                    static_cast<VarDecl&>(*binding).m_inWith = true;
                }
            }
            m_locks.push_back({std::move(lock), std::move(binding)});
        }
    }
    std::vector<AstNodePtr> all;
    for (const auto& [lock, binding] : m_locks) {
        if (lock) {
            all.push_back(lock);
        }
        if (binding) {
            all.push_back(binding);
        }
    }
    all.push_back(m_body);
    all.push_back(m_else);
    if (auto r = rangeOfNodes(all)) {
        setRange(*r);
    }
}

MatchStmt::MatchStmt(ParserToken::Kind k, TermPtr term)
: AstNodeAttr(k, term) {
    EXPECT(term && "MatchStmt term-constructor requires a source Term");
    // Оператор сопоставления (enum из текста MATCHING-терма) - валидируется всегда.
    m_op = matchOpFromSpelling(term->getText());
}

MatchStmt::MatchStmt(ParserToken::Kind k, TermPtr term, Context* ctx)
: MatchStmt(k, term) {
    if (!ctx) {
        return;
    }
    // Раскладка MATCHING-терма: m_left=значение, m_right=match_body (BLOCK);
    // m_right->m_sequence = [item1, ..., elseItem]; item: m_left=шаблоны (m_sequence=list),
    // m_right=тело; else: m_left=ELLIPSIS.
    if (term->m_left) {
        m_value = convertChild(*ctx, term->m_left);
    }
    if (term->m_right) {
        for (const auto& item : term->m_right->m_sequence) {
            if (!item) {
                continue;
            }
            if (item->m_left && item->m_left->getTermID() == trust::TermID::ELLIPSIS) {
                // else: [...] --> body
                m_default = item->m_right ? convertChild(*ctx, item->m_right) : nullptr;
            } else {
                MatchCase c;
                if (item->m_left) {
                    // matches: первый шаблон - сам терм, остальные - в его m_sequence
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
    std::vector<AstNodePtr> all{m_value};
    for (const auto& c : m_cases) {
        all.insert(all.end(), c.patterns.begin(), c.patterns.end());
        all.push_back(c.body);
    }
    all.push_back(m_default);
    if (auto r = rangeOfNodes(all)) {
        setRange(*r);
    }
}

TryCatchStmt::TryCatchStmt(ParserToken::Kind k, TermPtr term)
: Sequence(k, term) {
    EXPECT(term && "TryCatchStmt term-constructor requires a source Term");
    switch (term->getTermID()) {
    case trust::TermID::BLOCK_PLUS:
        m_tryKind = TryKind::Plus;
        break;
    case trust::TermID::BLOCK_MINUS:
        m_tryKind = TryKind::Minus;
        break;
    default:
        m_tryKind = TryKind::Any;
        break;
    }
}

TryCatchStmt::TryCatchStmt(ParserToken::Kind k, TermPtr term, Context* ctx)
: TryCatchStmt(k, term) {
    if (!ctx) {
        return;
    }
    // BLOCK_TRY-терм: m_sequence[0] = тело try; m_sequence[1..] = ветки catch
    // (терм-«след» FOLLOW: m_left = catch_arg, m_right = тело); m_right = else_body.
    // Тело конвертируем как ОДИН ребёнок; блок-тело расплющивается (его m_body вливается в try).
    if (!term->m_sequence.empty() && term->m_sequence[0]) {
        auto bodyNode = convertChild(*ctx, term->m_sequence[0]);
        if (bodyNode) {
            if (const auto* seq = bodyNode->as_sequence()) {
                m_body = seq->m_body;
            } else {
                m_body.push_back(std::move(bodyNode));
            }
        }
    }
    for (size_t i = 1; i < term->m_sequence.size(); ++i) {
        const auto& br = term->m_sequence[i];
        if (!br) {
            continue;
        }
        auto cb = std::make_shared<CatchBlock>(ParserToken::Kind::CatchBlock);
        const auto& arg = br->m_left;
        if (arg) {
            if (arg->getTermID() == trust::TermID::ELLIPSIS) {
                cb->m_binding = nullptr; // catch(...) → IntAny (любые не перечисленные)
            } else if (arg->getTermID() == trust::TermID::NONE) {
                cb->m_binding = nullptr; // catch(_) / catch(none) → catch-any (IntAny); «_» — лексема NONE, распознана парсером
            } else if (arg->m_type) {
                // catch(e:Type) → единый узел VarDecl(e, Type): имя+тип «в одном блоке».
                cb->m_binding = std::make_shared<VarDecl>(std::string(arg->getText()), convertChild(*ctx, arg->m_type));
            } else {
                cb->m_binding = convertChild(*ctx, arg); // catch(:Type) → IdentType(T)
            }
        }
        if (br->m_right) {
            convertChildren(*ctx, br->m_right, cb->m_body);
        }
        m_catches.push_back(std::move(cb));
    }
    if (term->m_right) {
        if (term->m_right->getTermID() == trust::TermID::FOLLOW && term->m_right->m_right) {
            m_else = convertChild(*ctx, term->m_right->m_right);
        } else {
            m_else = convertChild(*ctx, term->m_right);
        }
    }
}

// -- JumpStmt::dump --

std::string JumpStmt::dump(size_t indent) const {
    std::string result = AstNodeAttr::dump(indent);
    if (m_label) {
        result += " label:";
        result += requiredText(*m_label);
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

// -- ControlFlowStmt::dumpControlFlow --
std::string ControlFlowStmt::dumpControlFlow(size_t indent) const {
    std::string result = AstNodeAttr::dump(indent);
    dumpLabeled(result, indent, "cond", m_cond);
    dumpLabeled(result, indent, "body", m_body);
    dumpLabeled(result, indent, "else", m_else);
    return result;
}

// -- IfStmt::dump --

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

// -- WhileStmt::dump --

std::string WhileStmt::dump(size_t indent) const {
    return dumpControlFlow(indent);
}

// -- DoWhileStmt::dump --
std::string DoWhileStmt::dump(size_t indent) const {
    std::string result = AstNodeAttr::dump(indent);
    dumpLabeled(result, indent, "body", m_body);
    dumpLabeled(result, indent, "cond", m_cond);
    return result;
}

// -- MatchStmt::dump --

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

// -- WithStmt::dump --

std::string WithStmt::dump(size_t indent) const {
    std::string result = AstNodeAttr::dump(indent);
    for (size_t i = 0; i < m_locks.size(); ++i) {
        result += "\n" + std::string(indent, ' ') + "capture" + std::to_string(i) + ":";
        if (m_locks[i].first) {
            result += "\n" + std::string(indent + 2, ' ') + "lock: " + m_locks[i].first->dump(indent + 4);
        }
        if (m_locks[i].second) {
            result += "\n" + std::string(indent + 2, ' ') + "binding: " + m_locks[i].second->dump(indent + 4);
        }
    }
    dumpLabeled(result, indent, "body", m_body);
    dumpLabeled(result, indent, "else", m_else);
    return result;
}

// -- LabelRef (kind=GotoStmt|LabelStmt) / SemicolonStmt: dump --
std::string LabelRef::dump(size_t indent) const {
    return detail::dumpQuotedName(kind(), text(), indent);
}

std::string SemicolonStmt::dump(size_t indent) const {
    std::string result = AstNodeAttr::dump(indent);
    dumpLabeled(result, indent, "expr", m_expr);
    return result;
}

std::string LastResultCapture::dump(size_t indent) const {
    std::string result = AstNodeAttr::dump(indent);
    dumpLabeled(result, indent, "temp", m_temp);
    dumpLabeled(result, indent, "source", m_source);
    dumpLabeled(result, indent, "sink", m_sink);
    return result;
}

std::string ErrorExpr::dump(size_t indent) const {
    std::string result = AstNodeAttr::dump(indent);
    dumpLabeled(result, indent, "error", m_original); // владеемое ошибочное под-дерево (не удаляется)
    return result;
}

} // namespace trust
