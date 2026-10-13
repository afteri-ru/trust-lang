#pragma once

// include/ir/dataflow.hpp
// Обобщённый worklist/fixpoint dataflow поверх ir/cfg.hpp.
//
// Анализ задаётся duck-typed классом `Analysis`:
//   using State = ...;
//   static constexpr Direction direction;
//   State boundary() const;          // вход (forward) / выход (backward)
//   State bottom() const;            // начальное состояние блоков
//   State unreachableState() const;  // состояние недостижимых блоков (DA: top = всё assigned)
//   bool join(State& dst, const State& src) const;  // true, если dst изменился
//   State transferElement(const Element& element, State state) const;   // одно из двух
//   State transferBlock(const BasicBlockT<Element>& block, State state) const; // либо это
// Опционально:
//   State transferTerminator(const BasicBlockT<Element>& block, State state) const;
//   State transferEdge(const BasicBlockT<Element>& from, EdgeKind kind, State state) const;
//
// Framework НЕ навязывает направление итерации элементов: при наличии transferBlock
// анализ сам отвечает за порядок; иначе forward идёт по elements, backward - в обратном
// порядке (сначала terminator). Сходимость обеспечивается конечной высотой решётки анализа.

#include "ir/cfg.hpp"
#include "utils/error.hpp"

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <utility>
#include <vector>

namespace trust::flow {

/// Направление анализа.
enum class Direction : std::uint8_t { Forward, Backward };

template <class Analysis>
struct DataflowResult {
    using State = typename Analysis::State;

    std::deque<State> in;        ///< состояние на входе блока (forward) / вычисленное (backward)
    std::deque<State> out;       ///< состояние на выходе блока (forward) / на входе (backward)
    std::vector<bool> reachable; ///< блок достижим из entry (для backward - из exit)
};

namespace detail {

template <class Element, class Analysis>
typename Analysis::State applyBlockForward(const Analysis& analysis, const BasicBlockT<Element>& block, typename Analysis::State state) {
    if constexpr (requires {
                      { analysis.transferBlock(block, state) } -> std::same_as<typename Analysis::State>;
                  }) {
        return analysis.transferBlock(block, state);
    } else {
        for (const Element& element : block.elements) {
            state = analysis.transferElement(element, state);
        }
        if constexpr (requires {
                          { analysis.transferTerminator(block, state) } -> std::same_as<typename Analysis::State>;
                      }) {
            state = analysis.transferTerminator(block, state);
        }
        return state;
    }
}

template <class Element, class Analysis>
typename Analysis::State applyBlockBackward(const Analysis& analysis, const BasicBlockT<Element>& block, typename Analysis::State state) {
    if constexpr (requires {
                      { analysis.transferBlock(block, state) } -> std::same_as<typename Analysis::State>;
                  }) {
        return analysis.transferBlock(block, state);
    } else {
        if constexpr (requires {
                          { analysis.transferTerminator(block, state) } -> std::same_as<typename Analysis::State>;
                      }) {
            state = analysis.transferTerminator(block, state);
        }
        for (auto it = block.elements.rbegin(); it != block.elements.rend(); ++it) {
            state = analysis.transferElement(*it, state);
        }
        return state;
    }
}

template <class Element, class Analysis>
typename Analysis::State applyEdge(const Analysis& analysis, const BasicBlockT<Element>& from, EdgeKind kind, typename Analysis::State state) {
    if constexpr (requires {
                      { analysis.transferEdge(from, kind, state) } -> std::same_as<typename Analysis::State>;
                  }) {
        return analysis.transferEdge(from, kind, state);
    } else {
        return state;
    }
}

} // namespace detail

/// Выполняет анализ до неподвижной точки. Возвращает in/out для каждого блока.
/// Граф параметризован типом элемента; анализ реализует transferElement(const Element&, State)
/// ЛИБО transferBlock(const BasicBlockT<Element>&, State) (представление задаёт адаптер).
template <class Element, class Analysis>
DataflowResult<Analysis> runDataflow(const CfgT<Element>& cfg, const Analysis& analysis) {
    using State = typename Analysis::State;

    static_assert(
        requires(const Analysis& a, State s, State& dst) {
            { a.boundary() } -> std::same_as<State>;
            { a.bottom() } -> std::same_as<State>;
            { a.unreachableState() } -> std::same_as<State>;
            { a.join(dst, s) } -> std::same_as<bool>;
        }, "Analysis must provide boundary/bottom/unreachableState/join");
    static_assert(
        requires(const Analysis& a, const Element& n, State s) {
            { a.transferElement(n, s) } -> std::same_as<State>;
        } ||
            requires(const Analysis& a, const BasicBlockT<Element>& b, State s) {
                { a.transferBlock(b, s) } -> std::same_as<State>;
            },
        "Analysis must provide transferElement(const Element&, State) or transferBlock(const BasicBlockT<Element>&, State)");

    EXPECT(!cfg.blocks.empty() && "dataflow requires a non-empty CFG");
    EXPECT(cfg.entry < cfg.blocks.size() && cfg.exit < cfg.blocks.size() && "dataflow entry/exit must index an existing block");
    for (const BasicBlockT<Element>& block : cfg.blocks) {
        for (const FlowEdge& edge : block.successors) {
            EXPECT(edge.target < cfg.blocks.size() && "dataflow successor target must index an existing block");
        }
    }

    const std::size_t blockCount = cfg.blocks.size();
    DataflowResult<Analysis> result;
    result.in.assign(blockCount, analysis.bottom());
    result.out.assign(blockCount, analysis.bottom());
    result.reachable.assign(blockCount, false);

    std::vector<bool> queued(blockCount, false);
    std::vector<bool> visited(blockCount, false);
    std::deque<std::size_t> work;
    const auto enqueue = [&](std::size_t block) {
        if (!queued[block]) {
            queued[block] = true;
            work.push_back(block);
        }
    };

    if constexpr (Analysis::direction == Direction::Forward) {
        result.in[cfg.entry] = analysis.boundary();
        enqueue(cfg.entry);
        while (!work.empty()) {
            const std::size_t block = work.front();
            work.pop_front();
            queued[block] = false;
            if (cfg.blocks[block].unreachable) {
                continue;
            }
            visited[block] = true;
            result.reachable[block] = true;
            const State out = detail::applyBlockForward(analysis, cfg.blocks[block], result.in[block]);
            result.out[block] = out;
            for (const FlowEdge& edge : cfg.blocks[block].successors) {
                if (cfg.blocks[edge.target].unreachable) {
                    continue;
                }
                const State candidate = detail::applyEdge(analysis, cfg.blocks[block], edge.kind, out);
                const bool changed = analysis.join(result.in[edge.target], candidate);
                if (changed || !visited[edge.target]) {
                    enqueue(edge.target);
                }
            }
        }
    } else {
        std::vector<std::vector<std::pair<std::size_t, EdgeKind>>> preds(blockCount);
        for (const auto& block : cfg.blocks) {
            for (const FlowEdge& edge : block.successors) {
                preds[edge.target].push_back({block.id, edge.kind});
            }
        }
        result.out[cfg.exit] = analysis.boundary();
        enqueue(cfg.exit);
        while (!work.empty()) {
            const std::size_t block = work.front();
            work.pop_front();
            queued[block] = false;
            if (cfg.blocks[block].unreachable) {
                continue;
            }
            visited[block] = true;
            result.reachable[block] = true;
            const State in = detail::applyBlockBackward(analysis, cfg.blocks[block], result.out[block]);
            result.in[block] = in;
            for (const auto& [pred, kind] : preds[block]) {
                if (cfg.blocks[pred].unreachable) {
                    continue;
                }
                const State candidate = detail::applyEdge(analysis, cfg.blocks[pred], kind, in);
                const bool changed = analysis.join(result.out[pred], candidate);
                if (changed || !visited[pred]) {
                    enqueue(pred);
                }
            }
        }
    }

    for (std::size_t i = 0; i < blockCount; ++i) {
        if (cfg.blocks[i].unreachable) {
            result.in[i] = analysis.unreachableState();
            result.out[i] = analysis.unreachableState();
        }
    }
    return result;
}

} // namespace trust::flow
