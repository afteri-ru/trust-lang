# MEMORY.md

> scope: include/verify/model (+ src/verify)
> role: persistent-memory
> last_reviewed: 2026-11-10
> review_period: 30

## Architecture

Модель доказательств: `obligation.hpp` (`ObligationKind`, `Obligation`, `ProofStatus`, `ProofResult`,
`ProofReport`) и `diag.hpp` (список флагов `--verify`). Контракт бэкенда (`proof_backend.hpp`),
сертификат (`certificate.hpp`) и режим гейта (`verify.hpp`) остаются в корне компонента.

## Facts and invariants

- **⚠ Envelope ≠ доказано:** обязательство вне области проверки получает `ProofStatus::Envelope`, а
  `makeProofReport` падает (EXPECT), если бэкенд объявил его иначе; гейт `--verify=all` отклоняет
  Envelope и Unknown так же, как Failed.
- **⚠ Порядок результатов = порядок обязательств:** `check()` возвращает ровно по одному статусу на
  обязательство (несовпадение размеров — EXPECT, не «всё доказано»).

## Decisions

## Relations

- `verify/proof_backend.hpp` (контракт), `solver` (SMT-бэкенд), `pipeline` (стадия `Verify`).
