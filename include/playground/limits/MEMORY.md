# MEMORY.md

> scope: include/playground/limits (+ src/playground)
> role: persistent-memory
> last_reviewed: 2026-11-05
> review_period: 30
> max_size: 3500

## Architecture

Защитные сервисы playground, каждый со своим мьютексом, композиция — в `PlaygroundServer`:
`RateLimiter` (rate-limit по IP), `ResultCache` (LRU-кеш `/run` по имени примера),
`StatsSessionStore` (админ-сессии `/stats`), `PowGuard` (PoW-челленджи `/run` и `/download`),
`AlertNotifier` (почтовые алерты).

## Facts and invariants

- **⚠ Сервисы не знают друг о друге:** связывает их только `PlaygroundServer`; каждый владеет
  своим состоянием и синхронизацией.
- **⚠ PoW-сложность адаптивна и приходит снаружи:** `PowGuard` не читает состояние очереди —
  сложность вычисляет балансировщик и передаёт параметром.
- **Сессии `/stats` — два ограничителя:** sliding TTL по `lastAccess` плюс жёсткий `maxSec`
  от `created`; истечение удаляет сессию.

## Decisions

## Relations

- `playground/server` (владелец композиции), `playground/http`.
