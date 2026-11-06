#pragma once

// CFG поверх HIR: граф базовых блоков ПОВЕРХ понижённой формы (сахар уже развёрнут).
// Форма графа параметризована типом элемента (BasicBlockT<Element>/CfgT<Element>);
// MIR-адаптер задаётся алиасами FlowBlock/ControlFlowGraph.
// Контракт:
//   * Вход — функция из HIR (`hir::buildHir`): `SemicolonStmt`, goto-метки и временные
//     анализатора уже материализованы. Исходная AST (pre-lowering) в CFG не подаётся.
//   * Блоки и элементы ссылаются на узлы HIR (невладеющие указатели). Время жизни CFG -
//     не дольше HIR.
//   * Переходы по меткам (GotoStmt/LabelStmt) разрешаются отложенно: lowering ставит метки
//     forward. ScopeExit для покидаемых скоупов вычисляется по снимкам стека скоупов
//     (выходит всё, что глубже общего префикса с меткой).
//   * Элемент блока - MirInstruction (явный порядок вычисления), а не AST-узел целиком.
//   * Узлы вложенных функций/лямбд (FuncDecl) в внешний CFG не раскрываются: их тела
//     анализируются отдельными CFG; в внешний блок попадают только чтения capture-by-value.
//   * Недостижимые блоки не удаляются (как в Clang CFG): они помечаются unreachable, чтобы
//     flow-анализ мог обработать их отдельно и не давать ложных срабатываний.

#include "ir/mir/mir.hpp"
#include "ast/token.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace trust {

class AstNodeBase;
class AttrPool;
class FuncDecl;

namespace flow {

/// Вид ребра перехода между блоками.
enum class EdgeKind : std::uint8_t {
    Normal,    ///< последовательный переход / ветка switch
    True,      ///< условие истинно
    False,     ///< условие ложно
    Break,     ///< break к цели (loop или именованный блок)
    Continue,  ///< continue к точке продолжения цикла
    Exception, ///< исключительный переход (catch/with-else)
    Return,    ///< return в exit
    Throw,     ///< throw в catch-dispatch или exit
};

/// Терминатор блока.
enum class TerminatorKind : std::uint8_t {
    Fallthrough, ///< нет явного терминатора (последний блок пути)
    Conditional, ///< if/while/do-while: True/False-рёбра
    Switch,      ///< match: несколько Normal-рёбер
    Dispatch,    ///< try/with: Normal + Exception-рёбра
    Return,      ///< return
    Throw,       ///< throw
    Break,       ///< break
    Continue,    ///< continue
    Goto,        ///< goto на метку HIR (named break/continue, continue-метка do-while)
};

/// Ребро CFG.
struct FlowEdge {
    EdgeKind kind = EdgeKind::Normal;
    std::size_t target = 0; ///< индекс целевого блока в ControlFlowGraph::blocks
};

/// Базовый блок, параметризованный типом элемента.
/// Элемент задаёт представление, к которому относится CFG: MIR-адаптер использует `MirInstruction`;
/// форма графа от представления не зависит.
template <class Element>
struct BasicBlockT {
    std::size_t id = 0;               ///< индекс блока (== позиция в blocks)
    std::vector<Element> elements;    ///< элементы блока в порядке выполнения
    std::vector<FlowEdge> successors; ///< исходящие рёбра
    TerminatorKind terminator = TerminatorKind::Fallthrough;
    Element terminatorNode{}; ///< маркер источника терминатора (у MIR-адаптера — MirInstruction с site)
    bool unreachable = false; ///< недостижим из entry
};

/// CFG функции/лямбды. entry/exit - служебные блоки без элементов.
template <class Element>
struct CfgT {
    std::size_t entry = 0;
    std::size_t exit = 0;
    std::vector<BasicBlockT<Element>> blocks;

    [[nodiscard]] const BasicBlockT<Element>& block(std::size_t index) const { return blocks[index]; }
};

/// CFG поверх MIR: элементы — MIR-инструкции с явным порядком; терминатор — MirInstruction-маркер с site.
using FlowBlock = BasicBlockT<MirInstruction>;
using ControlFlowGraph = CfgT<MirInstruction>;

/// MIR функции: HIR-декларация + построенный для неё CFG. Владелец — `MirModule`.
struct MirFunction {
    const FuncDecl* func = nullptr;
    ControlFlowGraph cfg;
};

/// MIR модуля: все функции/лямбды в порядке DFS (внешняя → вложенные).
/// Строится ОДИН раз; flow-анализы читают только этот модуль.
struct MirModule {
    std::vector<MirFunction> functions;
    /// Корни HIR, из которых построен модуль. Хранится КОПИЯ вектора (узлы разделяются через
    /// `shared_ptr`): так MIR не зависит от времени жизни вектора-источника и может переживать
    /// перемещение HIR. Нужны анализам, которые читают AST-факты вне тел функций (например,
    /// атрибуты классов).
    std::vector<AstNodePtr> roots;
};

/// Строит CFG по телу функции/лямбды из HIR (сахар уже развёрнут lowering'ом).
/// attrs нужен для классификации readonly-целей (finalWrite).
[[nodiscard]] ControlFlowGraph buildCfg(const FuncDecl& func, const AttrPool& attrs);

/// Строит MIR-модуль: обходит HIR, собирает все функции/лямбды в порядке DFS и строит
/// CFG для каждой. Единственная точка создания MIR для flow-анализов.
[[nodiscard]] MirModule buildMir(const std::vector<AstNodePtr>& roots, const AttrPool& attrs);

/// Текстовый дамп CFG (диагностика и unit-тесты формы).
[[nodiscard]] std::string dumpCfg(const ControlFlowGraph& cfg);

/// Структурный верификатор MIR — гейт корректности производного представления (аналог
/// `verifyHir` и `llvm::verifyModule`). Проверяет инварианты, которые обязаны выполняться при
/// корректной работе построителя: индексы entry/exit и целей рёбер, `id` блока совпадает с
/// позицией, флаг `unreachable` согласован с достижимостью из entry, exit без преемников,
/// терминатор имеет источник, элементы несут обязательные поля (`place`/`site`/`scope`),
/// контрактные элементы ссылаются на `TrustContract`, `MirOp::Terminator` не встречается в
/// элементах блока, у `Conditional` ровно True+False рёбра. Нарушение — ICE-политика.
[[nodiscard]] std::optional<std::string> verifyFlowModule(const MirModule& mir);

} // namespace flow
} // namespace trust
