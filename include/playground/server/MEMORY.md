# MEMORY.md

> scope: include/playground/server (+ src/playground)
> role: persistent-memory
> last_reviewed: 2026-11-05
> review_period: 30
> max_size: 4000

## Architecture

Ядро `trust-playground`:
- `server.hpp` — `PlaygroundServer`: сеть, accept-цикл, семафоры соединений, маршрутизация
  (`handle` → `handleRun/Poll/Result/Download/Stats*/Challenge`), композиция сервисов;
- `worker.hpp` — worker-режим: long-poll `/poll`, выполнение кода через `trust-lsp --json`, `/result`;
- `util.hpp` — общие утилиты сервера.

Эндпоинты: `POST /run` (постановка задачи в очередь + ожидание результата), `POST /poll` +
`POST /result` (протокол воркера), `POST /download` (ленивая сборка build-архива), `GET /challenge`
(PoW), `GET /health`, `GET|POST /stats*` (админ-статистика, вход/выход).

## Facts and invariants

- **⚠ Режим задаётся флагом бинарника, а не отдельной сборкой:** `--playground` — балансировщик
  (вычислений не выполняет), worker-режим — исполнение кода.
- **Порядок инициализации полей `PlaygroundServer` = порядок объявления в `server.hpp`**
  (`-Wreorder-ctor` в проекте = ошибка).

## Decisions

## Relations

- `playground/http`, `playground/limits`, `lsp` (`trust-lsp --json`).
