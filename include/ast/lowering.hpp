#pragma once

// include/ast/lowering.hpp
// Понижение (lowering) реализовано В КЛАССАХ УЗЛОВ согласно их Kind: каждый узел переопределяет
// virtual AstNodeBase::lower(self, LowerCtx&) и понижает своих детей. Свободные функции
// lowerBody/lowerBodyNode/lowerNode здесь — векторно-ориентированные помощники: оборачивают
// statement-выражения в SemicolonStmt, вставляют continue-метки перед первым циклом именованного
// блока и рекурсивно вызывают node->lower() на каждом ребёнке.
// Анализатор (semantic) только запускает проход: SemanticPassRunner::run() -> lowerBody(root).

#include "ast/ast_nodes.hpp"
#include "ast/token.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace trust {

/// Контекст прохода lowering.
struct LowerCtx {
    bool inFunction = false;     ///< внутри тела функции (метки именованных блоков допустимы только в функциях)
    std::string funcName;        ///< имя текущей функции (для «break по имени функции» → return)
    std::string pendingContinue; ///< continue-метка, которую потребляет первый цикл текущего именованного блока
};

/// Понижение одного узла: рекурсия через virtual node->lower(self, ctx).
void lowerNode(AstNodePtr& node, LowerCtx& ctx);
/// Понижение списка операторов: вставка SemicolonStmt и continue-метки перед первым циклом.
void lowerBody(std::vector<AstNodePtr>& body, LowerCtx& ctx);
/// Понижение тела узла (цикл/ветка): если блок — lowerBody(m_body) без named-block меток
/// (тело цикла/ветки не именованный блок), иначе — одиночный оператор как body из 1 элемента.
void lowerBodyNode(AstNodePtr& bodyNode, LowerCtx& ctx);

// ── Вспомогательные (используются node-методами lower) ──
/// Имя C++-метки из trust-имени блока/label: убирает '::' (и прочие ':').
std::string cleanLabelName(std::string_view name);
/// Имя функции (без '%') для сравнения с label при «break по имени функции».
std::string funcNameOf(const FuncDecl* fd);
/// Истина, если kind — statement-выражение (оборачивается в SemicolonStmt для явной ';').
bool isExprStatement(ParserToken::Kind k) noexcept;
/// Добавляет LabelStmt в конец тела узла-блока (или оборачивает одиночный statement)
/// — для continue-метки do-while (goto переходит к проверке условия в конце тела).
void appendLabel(AstNodePtr& bodyNode, const std::string& label);

} // namespace trust
