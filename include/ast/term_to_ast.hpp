#pragma once

// term_to_ast.hpp - TermID-visitor конвертации Term -> AstNode.
//
// Единственный вход конвертации - класс TermToAstConverter (никаких свободных функций).
// Диспетчеризация - по TermID через dispatchTerm (X-макрос TERMS); класс узла для
// generic-узлов берётся из PARSER_TOKEN_KINDS (node_type_for_kind_t<Kind>).
// «Типовые» visit-методы генерируются из x-macro автоматически (TermVisitorDefault);
// здесь переопределяются только спец-термы (MODULE, CREATE_TYPE, CREATE_NAME, ARGUMENT),
// которые выполняют класс-селекцию узла. Раскладку детей строят сами терм-конструкторы
// узлов (категорийные ast_*.cpp, тот же ast_lib) через этот класс.

#include "ast/term_visitor.hpp"

#include <memory>
#include <vector>

namespace trust {

class Term;
using TermPtr = std::shared_ptr<Term>;

/// TermID-visitor конвертации Term -> AstNode (вход по TermID из TERMS).
class TermToAstConverter : public TermVisitorDefault {
  public:
    explicit TermToAstConverter(Context& ctx)
    : m_ctx(ctx) {}

    /// Преобразовать один Term в AstNode (рекурсивно). nullptr для null/END.
    AstNodePtr convert(const trust::TermPtr& term);

    /// Преобразовать детей Term (m_sequence, m_args, m_left, m_right) и добавить в out.
    void convertSeq(const trust::TermPtr& term, std::vector<AstNodePtr>& out);

    /// Рекурсивно развернуть SEQUENCE-термы (синтаксические контейнеры операторов/doc-bundle)
    /// в их детей; BLOCK-термы (пользовательские скоупы) и прочие - конвертировать как есть.
    /// Не позволяет вложенным SEQUENCE-термам стать лишними ScopeBlock-слоями.
    void flattenInto(const trust::TermPtr& term, std::vector<AstNodePtr>& out);

    /// Вход конвертации: вектор корневых узлов.
    static std::vector<AstNodePtr> termToAst(const trust::TermPtr& term, Context& ctx);

  private:
    // -- Спец-термы (остальные visit_<NAME> генерируются из x-macro) --
    AstNodePtr visit_MODULE(const trust::TermPtr& term, Context& ctx) override;
    AstNodePtr visit_CREATE_NAME(const trust::TermPtr& term, Context&) override;
    /// `a, b = ... source;` - деструктуризация-присваивание (многоимённый LHS + `=`): построить
    /// DestructureDecl(m_isAssign=true); одиночный `a = expr` - обычный AssignOp (generic).
    AstNodePtr visit_ASSIGN(const trust::TermPtr& term, Context& ctx) override;
    AstNodePtr visit_ARGUMENT(const trust::TermPtr& term, Context&) override;
    /// `:Type(...)`/`(...):Type` - единый узел DictLiteralNode с аннотацией типа (m_type);
    /// класс (кортеж/каст/конструктор) определяет анализатор по типу из реестра. `:Type` без
    /// аргументов - обычный тип (TypeName), делегирует в default.
    AstNodePtr visit_TYPE(const trust::TermPtr& term, Context& ctx) override;
    /// `"fmt"(args)` / `'fmt'(args)` - строка как формат-строка (правило `string: strtype call`):
    /// с аргументами → CallExpr(callee=Literal StrWide|StrChar), без - обычный литерал.
    AstNodePtr visit_STRWIDE(const trust::TermPtr& term, Context& ctx) override;
    AstNodePtr visit_STRCHAR(const trust::TermPtr& term, Context& ctx) override;
    /// `(...)` - литерал словаря → Sequence(DictLiteral). Элементы строятся из канонических пар
    /// (name, term) грамматики `args` (argName из parser.y) и нормализуются к ЕДИНОЙ форме
    /// Binary(AssignOp): left=Ident-метка (или пустой), right=значение.
    AstNodePtr visit_DICT(const trust::TermPtr& term, Context&) override;
    /// `[1,2,3,]` / `[1,2,3,]:Int32` - литерал массива → DictLiteralNode(kind=ArrayInit).
    /// Аннотация `]:Type` сохраняется в m_type; элементы - из канонических пар (как visit_DICT).
    AstNodePtr visit_TENSOR(const trust::TermPtr& term, Context& ctx) override;
    /// `start..stop[..step]` - литерал диапазона → RangeExpr. Помимо generic-построения (m_body из
    /// m_args), переносит явные аннотации типа операндов (`start:Type`, `stop:Type`) из m_type
    /// терма-операнда в RangeExpr::operandTypes (для учёта в analyzeRangeExpr, напр. `0..100:Rational`).
    AstNodePtr visit_RANGE(const trust::TermPtr& term, Context& ctx) override;

    Context& m_ctx;
};

/// Сконвертировать один Term в AstNode (рекурсивно) через свежий конвертер.
/// Устраняет повторяющийся паттерн `TermToAstConverter conv{ctx}; return conv.convert(t);`
/// в терм-конструкторах узлов (категорийные ast_*.cpp). nullptr для null/END.
AstNodePtr convertChild(Context& ctx, const trust::TermPtr& term);

/// Сконвертировать детей Term (m_sequence, m_args, m_left, m_right) и добавить в out.
/// Устраняет повторяющийся паттерн `TermToAstConverter conv{ctx}; conv.convertSeq(...)`.
void convertChildren(Context& ctx, const trust::TermPtr& term, std::vector<AstNodePtr>& out);

/// Сконвертировать тело модуля в out: SEQUENCE-терм (контейнер тела модуля) разворачивается
/// в своих детей (ScopeBlock-обёртка НЕ создаётся - модуль сам является глобальным скоупом),
/// а пользовательские `{ ... }` (BLOCK-термы) сохраняются как ScopeBlock-узлы. Иначе блок
/// верхнего уровня неотличим от контейнера тела модуля и его граница теряется.
void convertModuleBody(Context& ctx, const trust::TermPtr& term, std::vector<AstNodePtr>& out);

} // namespace trust
