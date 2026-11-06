# MEMORY.md

> scope: include/playground
> role: persistent-memory
> last_reviewed: 2026-11-13
> review_period: 30

## Architecture

`trust-playground` — единый бинарник с двумя режимами (`app/main.cpp`): `--playground` — балансировщик
`PlaygroundServer` (вычислений не выполняет) и worker-режим (исполнение кода).
Публичного API у компонента НЕТ (tool-компонент): все заголовки — детали реализации.

| каталог | MEMORY | ответственность |
|---|---|---|
| `playground/app/` | — (в коде) | точка входа бинарника (`main.cpp`): выбор режима, конфиг |
| `playground/server/` | `server/MEMORY.md` | `PlaygroundServer`, worker, эндпоинты, утилиты, встроенные HTML-страницы |
| `playground/http/` | — (инварианты в коде) | HTTP-ядро без сокетов, конфигурация |
| `playground/limits/` | `limits/MEMORY.md` | rate-limit, кеш результатов, `/stats`-сессии, PoW, алерты |
| `playground/deploy/` | — (инварианты в скриптах) | установка и сервисные файлы (systemd/nginx/install) — только ресурсы, TU нет |

## Facts and invariants

## Decisions

## Relations
