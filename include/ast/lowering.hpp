#pragma once

// include/ast/lowering.hpp
// Понижение (lowering) реализовано В КЛАССАХ УЗЛОВ согласно их Kind: каждый узел переопределяет
// virtual AstNodeBase::lower(self, LowerCtx&) и понижает своих детей. Свободные функции
// lowerBody/lowerBodyNode/lowerNode здесь - векторно-ориентированные помощники: оборачивают
// statement-выражения в SemicolonStmt, вставляют continue-метки перед первым циклом именованного
// блока и рекурсивно вызывают node->lower() на каждом ребёнке.
// Анализатор (semantic) только запускает проход: SemanticPassRunner::run() -> lowerBody(root).

#include "ast/ast_nodes.hpp"
#include "ast/token.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace trust {

class Context;

/// Контекст прохода lowering.
struct LowerCtx {
    bool inFunction = false;     ///< внутри тела функции (метки именованных блоков допустимы только в функциях)
    std::string funcName;        ///< имя текущей функции (для «break по имени функции» → return)
    std::string pendingContinue; ///< continue-метка, которую потребляет первый цикл текущего именованного блока
    /// Контекст анализа (AttrPool для пометки синтезированных временных readonly/const). Устанавливает
    /// SemanticPassRunner::run(); lowering создаёт временные как const VarDecl (инвариант «временные
    /// создаёт анализатор»). nullptr - нет контекста (синтез readonly недоступен).
    Context* ctx = nullptr;
    /// Счётчики синтезируемых временных (уникальные имена): while-else / деструктуризация.
    /// Scrutinee-match создаёт СЕМАНТИКА (AnalysisContext::matchTempCounter), а не lowering.
    int whileElseCounter = 0;
    int destructureCounter = 0;
    /// Флаг `_wN` оператора `with` (для else-ветки): создаёт lowering (инвариант «временные создаёт
    /// анализатор»), транспилятор только читает/эмитит через emitSyntheticVar.
    int withFailCounter = 0;
    /// Счётчик Locker-темпа `__cap_N` захвата ссылки в `with` (`lock = *ref`): создаёт lowering
    /// (инвариант «временные создаёт анализатор»), транспилятор эмитит как `auto __cap_N = ref.lock();`.
    int withCapCounter = 0;
    /// Счётчик синтетической временной `__trust_last_N` прохода capture «результат последней операции»
    /// (`$^`): предыдущий оператор-выражение -> `__trust_last_N := <expr>;`, лист `$^` в sink -> ссылка
    /// на временную. Имя уникально в пределах одного анализа (пере-ран LowerCtx на каждый SemanticPassRunner).
    int lastResultCounter = 0;
};

/// Понижение одного узла: рекурсия через virtual node->lower(self, ctx).
void lowerNode(AstNodePtr& node, LowerCtx& ctx);
/// Понижение списка операторов: вставка SemicolonStmt и continue-метки перед первым циклом.
void lowerBody(std::vector<AstNodePtr>& body, LowerCtx& ctx);
/// Понижение тела узла (цикл/ветка): если блок - lowerBody(m_body) без named-block меток
/// (тело цикла/ветки не именованный блок), иначе - одиночный оператор как body из 1 элемента.
void lowerBodyNode(AstNodePtr& bodyNode, LowerCtx& ctx);

/// Capture-проход «результат последней операции» (`$^`, простой случай) — ПРЕ-семантическое
/// структурное переписывание пары соседних сиблингов в обычный trust-код. `$^` = значение последнего
/// оператора, два источника дают значение:
///   1) голое выражение-оператор `E;` (значение теряется): [ __trust_last_N := E; ] + `$^`→Ident(temp);
///   2) декларация `x := E;` (результат = значение `x`, уже объявленной): `$^`→Ident('x'), temp не нужен.
/// Временная (случай 1) объявляется сиблингом в том же скоупе тела (temp→sink -> declare-before-use);
/// тип выводится семантикой из E штатно, значение E выполняется ровно один раз. Рекурсивно обходит все
/// тела-операторов (функции, sequence/ScopeBlock, ветки if/elif/else, while/do-while, match, try/else, with).
/// НЕподдержанные обращения `$^` (нет источника / после составного оператора / после не-значения) НЕ
/// переписываются: проход сообщает точечную ошибку В МОМЕНТ выявления (здесь известен предыдущий
/// сиблинг) и заменяет лист `$^` на ErrorExpr-заглушку-владельца (см. ast_nodes.hpp), чтобы семантика НЕ
/// эмитила повторную диагностику и не «теряла» заменённый узел. void-вызов-источник относится к случаю 1
/// (переписывается в temp), а его «нет значения» ловит семантика по типу (см. VarDecl::m_lastResultTemp).
/// Запускается SemanticPassRunner::run() ДО core.run.
void captureLastResult(std::vector<AstNodePtr>& body, LowerCtx& ctx);

// -- Вспомогательные (используются node-методами lower) --
/// Имя C++-метки из trust-имени блока/label: убирает '::' (и прочие ':').
std::string cleanLabelName(std::string_view name);
/// Имя функции (без '%') для сравнения с label при «break по имени функции».
std::string funcNameOf(const FuncDecl* fd);
/// Истина, если kind - statement-выражение (оборачивается в SemicolonStmt для явной ';').
bool isExprStatement(ParserToken::Kind k) noexcept;
/// Добавляет LabelStmt в конец тела узла-блока (или оборачивает одиночный statement)
/// - для continue-метки do-while (goto переходит к проверке условия в конце тела).
void appendLabel(AstNodePtr& bodyNode, const std::string& label);

} // namespace trust
