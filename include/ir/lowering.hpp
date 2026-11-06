#pragma once

// Lowering — ПРОХОД HIR над клоном AST: превращает «сахар» источника в структурную форму,
// которую читает C++-бэкенд (SemicolonStmt, goto/метки, временные анализатора).
// AST остаётся деревом разбора и НЕ содержит lowering-логики: все операции живут в `hir`.
// Понижение запускает `Pipeline` в HIR-фазе (`hir::buildHir` → `lowerBody`); семантика его
// не выполняет.

#include "ast/ast_nodes.hpp"
#include "ast/clone.hpp"
#include "ast/token.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace trust {

class Context;
class TypeSystem;

/// Префикс Locker-темпа `with`-захвата (`__cap_N`): тип trust::Locker не является языковым,
/// поэтому транспилятор эмитит его через `auto`. Единый источник для lowering и эмиттера.
inline constexpr std::string_view kLockerTempPrefix = "__cap_";

/// Контекст прохода lowering.
struct LowerCtx {
    bool inFunction = false; ///< внутри тела функции (метки именованных блоков допустимы только в функциях)
    std::string funcName;    ///< имя текущей функции (для «break по имени функции» → return)
    /// Цели break/continue именованных блоков: единое правило с CFG-builder
    /// (pending continue потребляет первый цикл в теле блока).
    JumpTargetsT<std::string> jumpTargets;
    /// Контекст анализа (AttrPool для пометки синтезированных временных readonly/const).
    /// Устанавливает `Pipeline::buildLoweredHir` (через `hir::buildHir`). nullptr - нет контекста.
    Context* ctx = nullptr;
    /// WRITER-ссылка реестра типов (E1): lowering интернирует реф-типы временных. Передаётся явно
    /// владельцем (`Pipeline`), а не берётся из `Context` (мутабельный реестр фасад не отдаёт).
    TypeSystem* types = nullptr;
    /// Счётчики синтезируемых временных (уникальные имена): while-else / деструктуризация.
    /// Scrutinee-match создаёт СЕМАНТИКА (AnalysisState::matchTempCounter), а не lowering.
    int whileElseCounter = 0;
    int destructureCounter = 0;
    /// Флаг `_wN` оператора `with` (для else-ветки): создаёт lowering (инвариант «временные создаёт
    /// анализатор»), транспилятор только читает/эмитит через emitSyntheticVar.
    int withFailCounter = 0;
    /// Счётчик Locker-темпа `__cap_N` захвата ссылки в `with` (`lock = *ref`): создаёт lowering
    /// (инвариант «временные создаёт анализатор»), транспилятор эмитит как `auto __cap_N = ref.lock();`.
    int withCapCounter = 0;
};

/// Понижение одного узла: рекурсия по детям согласно Kind.
void lowerNode(AstNodePtr& node, LowerCtx& ctx);
/// Понижение списка операторов: вставка SemicolonStmt и continue-метки перед первым циклом.
void lowerBody(std::vector<AstNodePtr>& body, LowerCtx& ctx);
/// Понижение тела узла (цикл/ветка): если блок - lowerBody(m_body) без named-block меток
/// (тело цикла/ветки не именованный блок), иначе - одиночный оператор как body из 1 элемента.
void lowerBodyNode(AstNodePtr& bodyNode, LowerCtx& ctx);

} // namespace trust
