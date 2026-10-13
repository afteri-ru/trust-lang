# MEMORY.md

> scope: include/playground
> role: persistent-memory
> last_reviewed: 2026-11-05
> review_period: 30
> max_size: 4100

## Architecture

`trust-playground` — единый бинарник с двумя режимами (`main.cpp`): `--playground` — балансировщик
`PlaygroundServer` (вычислений не выполняет) и worker-режим (исполнение кода).
Публичного API у компонента НЕТ (tool-компонент): все заголовки — детали реализации.

| каталог | MEMORY | ответственность |
|---|---|---|
| `playground/server/` | `server/MEMORY.md` | `PlaygroundServer`, worker, эндпоинты, утилиты |
| `playground/http/` | — (инварианты в коде) | HTTP-ядро без сокетов, конфигурация |
| `playground/limits/` | `limits/MEMORY.md` | rate-limit, кеш результатов, `/stats`-сессии, PoW, алерты |
| `playground/deploy/` | — (инварианты в скриптах) | установка и сервисные файлы (systemd/nginx/install) — только ресурсы, TU нет |

## Facts and invariants

## Decisions

## Relations
