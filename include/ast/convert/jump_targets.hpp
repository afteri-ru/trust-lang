#pragma once

// include/ast/convert/jump_targets.hpp
// Единое правило целей break/continue для lowering и CFG-builder.
//
// Контракт:
//   * Именованный блок делает своё имя pending-целью для continue; первый цикл
//     (while/do-while) в его теле потребляет эту цель.
//   * `break <имя_функции>` эквивалентен void-return.
//   * Формат имён goto-меток единый: `<блок>_break` / `<блок>_continue`.
//   * Target - представление цели: индекс блока CFG (`size_t`) либо имя goto-метки
//     (`std::string`) для lowering. Само правило от представления не зависит.

#include "ast/token.hpp"
#include "utils/error.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace trust {

template <class Target>
class JumpTargetsT {
  public:
    /// Именованный блок в стеке: цель break и (после привязки) цель continue.
    struct NamedBlock {
        std::string name;
        Target breakTarget;
        std::optional<Target> continueTarget;
    };

    /// Точка восстановления стека перед входом в именованный блок.
    struct SavePoint {
        std::optional<std::string> pending;
        std::size_t depth = 0;
    };

    /// Цикл этого kind потребляет pending continue именованного блока.
    [[nodiscard]] static bool consumesPendingContinue(ParserToken::Kind kind) noexcept {
        return kind == ParserToken::Kind::WhileStmt || kind == ParserToken::Kind::DoWhileStmt;
    }

    /// `break <имя_функции>` == void-return.
    [[nodiscard]] static bool isFunctionBreak(std::string_view label, std::string_view funcName) noexcept { return !funcName.empty() && label == funcName; }

    /// Единый формат goto-меток lowering (суффиксы — единственный источник и для предикатов).
    inline static constexpr std::string_view kBreakSuffix = "_break";
    inline static constexpr std::string_view kContinueSuffix = "_continue";

    [[nodiscard]] static std::string breakLabel(std::string_view blockName) { return std::string(blockName) + std::string(kBreakSuffix); }
    [[nodiscard]] static std::string continueLabel(std::string_view blockName) { return std::string(blockName) + std::string(kContinueSuffix); }

    /// Вид цели по имени метки: предикаты над тем же суффиксом, что и генераторы имён.
    [[nodiscard]] static bool isBreakLabel(std::string_view label) noexcept { return label.ends_with(kBreakSuffix); }
    [[nodiscard]] static bool isContinueLabel(std::string_view label) noexcept { return label.ends_with(kContinueSuffix); }

    /// Открывает именованный блок; pending-целью становится его имя.
    SavePoint enterNamedBlock(std::string name, Target breakTarget) {
        SavePoint saved{.pending = m_pending, .depth = m_blocks.size()};
        m_blocks.push_back(NamedBlock{.name = std::move(name), .breakTarget = std::move(breakTarget), .continueTarget = std::nullopt});
        m_pending = m_blocks.back().name;
        return saved;
    }

    /// Закрывает именованный блок и восстанавливает предыдущую pending-цель.
    void leaveNamedBlock(const SavePoint& saved) {
        EXPECT(saved.depth <= m_blocks.size() && "jump scope save point does not match the label stack");
        m_blocks.resize(saved.depth);
        m_pending = saved.pending;
    }

    /// Привязывает pending continue к цели цикла. false - pending отсутствует.
    bool bindContinue(Target target) {
        if (!m_pending.has_value()) {
            return false;
        }
        const std::string name = *m_pending;
        m_pending.reset();
        for (auto it = m_blocks.rbegin(); it != m_blocks.rend(); ++it) {
            if (it->name == name) {
                it->continueTarget = std::move(target);
                return true;
            }
        }
        FAULT("pending continue '{}' has no named block", name);
    }

    /// Забирает pending continue (lowering вставляет метку). nullopt - pending отсутствует.
    [[nodiscard]] std::optional<std::string> takePendingContinue() {
        if (!m_pending.has_value()) {
            return std::nullopt;
        }
        std::string name = std::move(*m_pending);
        m_pending.reset();
        return name;
    }

    [[nodiscard]] bool hasNamedBlock(std::string_view name) const {
        for (auto it = m_blocks.rbegin(); it != m_blocks.rend(); ++it) {
            if (it->name == name) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] std::optional<Target> breakTarget(std::string_view name) const {
        for (auto it = m_blocks.rbegin(); it != m_blocks.rend(); ++it) {
            if (it->name == name) {
                return it->breakTarget;
            }
        }
        return std::nullopt;
    }

    /// Цель continue именованного блока; nullopt - блок не найден или цикл ещё не привязан.
    [[nodiscard]] std::optional<Target> continueTarget(std::string_view name) const {
        for (auto it = m_blocks.rbegin(); it != m_blocks.rend(); ++it) {
            if (it->name == name) {
                return it->continueTarget;
            }
        }
        return std::nullopt;
    }

  private:
    std::vector<NamedBlock> m_blocks;
    std::optional<std::string> m_pending;
};

} // namespace trust
