#pragma once

// Сменный доказательный бэкенд: получает готовые обязательства и возвращает статусы.
// Модель обязательств принадлежит `verify`; кодирование/исполнение (SMT-LIB 2, Z3, ...) —
// реализация бэкенда (сейчас — `solver`). Порядок результатов совпадает с порядком входа.

#include "verify/model/obligation.hpp"

#include <string_view>
#include <vector>

namespace trust {
namespace verify {

class ProofBackend {
  public:
    ProofBackend() = default;
    virtual ~ProofBackend() = default;
    ProofBackend(const ProofBackend&) = delete;
    ProofBackend& operator=(const ProofBackend&) = delete;

    /// Имя бэкенда (стабильное, входит в сертификат).
    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
    /// Версия решателя (например "z3" или "none").
    [[nodiscard]] virtual std::string_view version() const noexcept = 0;

    /// Проверить обязательства. Возвращает статус на КАЖДОЕ обязательство (в том же порядке).
    /// Обязательство вне envelope получает `ProofStatus::Envelope`, а не "доказано".
    [[nodiscard]] virtual std::vector<ProofResult> check(const std::vector<Obligation>& obligations) = 0;
};

/// Собрать отчёт из обязательств и результатов бэкенда (проверка полноты соответствия).
[[nodiscard]] ProofReport makeProofReport(std::string_view tool, std::string_view version, const std::vector<Obligation>& obligations,
                                          std::vector<ProofResult> results);

} // namespace verify
} // namespace trust
