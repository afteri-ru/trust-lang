# MEMORY.md

> scope: include/verify (public API)
> role: persistent-memory
> last_reviewed: 2026-11-05
> review_period: 30
> max_size: 6000

## Architecture

Слой доказательств — владелец модели обязательств и отчёта:
- `verify/model/obligation.hpp` — `ObligationKind`, `Obligation{kind, site, assumptions, goal, envelope}`,
  `ProofStatus{Proved, Failed, Unknown, Envelope}`, `ProofResult`, `ProofReport`;
- `verify/proof_backend.hpp` — контракт сменного доказательного бэкенда + `makeProofReport`;
- `verify/certificate.hpp` — детерминированный JSON `*.trustcert` (`composeCertificate`);
- `verify/verify.hpp` — режим гейта `--verify=all` (`verifyModeFromOptions`), `verify/model/diag.hpp` —
  список флагов (владелец флага — `verify`).

Детали реализации — internal-подкаталог `verify/model/` (обязательства, `ProofReport`, флаги);
`proof_backend.hpp`, `certificate.hpp`, `verify.hpp` — публичный API корня.

## Facts and invariants

- **⚠ Envelope ≠ доказано:** обязательство вне области проверки получает `ProofStatus::Envelope`, а
  `makeProofReport` падает (EXPECT), если бэкенд объявил его иначе; гейт `--verify=all` отклоняет
  Envelope и Unknown так же, как Failed.
- **⚠ Порядок результатов = порядок обязательств:** `check()` возвращает ровно по одному статусу на
  обязательство (несовпадение размеров — EXPECT, не «всё доказано»).
- **⚠ Сертификат байт-стабилен:** порядок ключей и записей фиксирован входом; сортировок по
  нестабильным контейнерам нет, внешних JSON-зависимостей нет (собственный writer + экранирование).
- **Зависимости:** `diag`, `location`; НЕ `pipeline`/`semantic`/`solver` — решатель подключается
  реализацией `ProofBackend`.

## Decisions

- Флаг `--verify` принадлежит `verify` (не `semantic`): гейт доказательств — не severity-диагностика
  и не режим кодогена.

## Relations

- `solver` (SMT-бэкенд как `ProofBackend`), `ir` (HIR/MIR — источник фактов), `pipeline` (стадия
  `Verify` и печать сертификата), `diag` (флаги).
