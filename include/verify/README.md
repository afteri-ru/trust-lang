# Verify: слой доказательств

- `obligation.hpp` — модель обязательства (`ObligationKind`, `Obligation`, `ProofStatus`, `ProofReport`).
- `proof_backend.hpp` — контракт сменного доказательного бэкенда + `makeProofReport`.
- `certificate.hpp` — детерминированный JSON `*.trustcert`.
- `verify.hpp` / `diag.hpp` — гейт `--verify=all` (владелец флага — `verify`).

Правило: обязательство создаётся там, где есть факты (`semantic`/`ir`), но модель, отчёт и
сертификат принадлежат `verify`; решатель — реализация `ProofBackend`.
