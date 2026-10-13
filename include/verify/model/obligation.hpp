#pragma once

// include/verify/model/obligation.hpp
// Модель обязательства доказательства: ЧТО требуется доказать и с каким статусом.
//
// Правило владения: факты (overflow/stack/borrow/DA, контракты) создаются там, где есть типы и
// CFG (semantic/ir), но их представление как обязательства и отчёт принадлежат `verify`.
// Решатель — сменный `ProofBackend` (SMT-LIB 2 / Z3), он не владеет моделью.

#include "location/location.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace trust {
namespace verify {

/// Вид обязательства. Полный набор — целевой словарь слоя доказательств; источники, которые
/// фактически порождают цели сейчас, помечены в MEMORY компонента.
/// Единый источник: X-макрос генерирует и enum, и стабильный строковый ид (входит в сертификат).
#define TRUST_OBLIGATION_KIND_IDS(O)                     \
    /* знаковое целочисленное переполнение */            \
    O(Overflow, "overflow")                              \
    /* деление/остаток на ноль (и INT_MIN / -1) */       \
    O(DivZero, "divzero")                                \
    /* выход за границы */                               \
    O(Bounds, "bounds")                                  \
    /* заимствование/владение */                         \
    O(Borrow, "borrow")                                  \
    /* контракт функции (pre/post/assert/type-assert) */ \
    O(Contract, "contract")                              \
    /* инвариант цикла (вход/сохранение) */              \
    O(Invariant, "invariant")                            \
    /* переполнение стека */                             \
    O(Stack, "stack")

enum class ObligationKind {
#define TRUST_OBLIGATION_KIND_ENUM(name, id) name,
    TRUST_OBLIGATION_KIND_IDS(TRUST_OBLIGATION_KIND_ENUM)
#undef TRUST_OBLIGATION_KIND_ENUM
};

/// Строковый идентификатор вида (стабильный: входит в сертификат).
/// Недопустимое значение enum — внутренняя ошибка (FAULT).
[[nodiscard]] inline std::string_view obligationKindId(ObligationKind kind) {
    switch (kind) {
#define TRUST_OBLIGATION_KIND_CASE(name, id) \
    case ObligationKind::name:               \
        return id;
        TRUST_OBLIGATION_KIND_IDS(TRUST_OBLIGATION_KIND_CASE)
#undef TRUST_OBLIGATION_KIND_CASE
    }
    FAULT("unknown ObligationKind value {}", static_cast<int>(kind));
}

/// Обязательство: вид, сайт источника, допущения/цель (человекочитаемо) и причина envelope.
struct Obligation {
    ObligationKind kind = ObligationKind::Contract;
    MapperRange site;
    std::vector<std::string> assumptions; ///< допущения (аксиомы контрактов и т.п.)
    std::string goal;                     ///< формулировка цели (для отчёта)
    /// Непусто — обязательство ВНЕ области проверки: VC пропускается, а не доказывается.
    std::optional<std::string> envelope;
};

/// Статус обязательства.
/// Единый источник: X-макрос генерирует и enum, и стабильный строковый ид (входит в сертификат).
#define TRUST_PROOF_STATUS_IDS(O)                        \
    /* нарушение невозможно (для SMT-бэкенда — unsat) */ \
    O(Proved, "proved")                                  \
    /* найдено нарушение (sat; контрпример в отчёте) */  \
    O(Failed, "failed")                                  \
    /* решатель не дал ответа/недоступен (stub) */       \
    O(Unknown, "unknown")                                \
    /* вне области проверки (не проверялось) */          \
    O(Envelope, "envelope")

enum class ProofStatus {
#define TRUST_PROOF_STATUS_ENUM(name, id) name,
    TRUST_PROOF_STATUS_IDS(TRUST_PROOF_STATUS_ENUM)
#undef TRUST_PROOF_STATUS_ENUM
};

/// Строковый идентификатор статуса (стабильный: входит в сертификат).
/// Недопустимое значение enum — внутренняя ошибка (FAULT).
[[nodiscard]] inline std::string_view proofStatusId(ProofStatus status) {
    switch (status) {
#define TRUST_PROOF_STATUS_CASE(name, id) \
    case ProofStatus::name:               \
        return id;
        TRUST_PROOF_STATUS_IDS(TRUST_PROOF_STATUS_CASE)
#undef TRUST_PROOF_STATUS_CASE
    }
    FAULT("unknown ProofStatus value {}", static_cast<int>(status));
}

/// Результат проверки одного обязательства.
struct ProofResult {
    std::size_t index = 0; ///< индекс в списке обязательств
    ProofStatus status = ProofStatus::Unknown;
    std::vector<std::pair<std::string, std::string>> model; ///< контрпример (имена/значения) при Failed
};

/// Отчёт о прогоне: статус на каждое обязательство + имя/версия решателя.
struct ProofReport {
    std::string tool;    ///< имя доказательного бэкенда ("smt" / "stub")
    std::string version; ///< версия решателя (например "z3" или "none")
    std::vector<ProofResult> results;

    /// true — все обязательства ДОКАЗАНЫ (Envelope/Unknown тоже считаются непроверенными).
    [[nodiscard]] bool allProved() const noexcept;
};

} // namespace verify
} // namespace trust
