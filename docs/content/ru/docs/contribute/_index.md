---
title: Разработка
tags: [development, getting-started]
linkTitle: Разработка
description: >
    Как внести вклад: структура проекта, сборка, тесты, стиль кода
weight: 10
---

## О проекте

TrustLang разрабатывается открыто с помощью ИИ-агентов. Исходный код находится в
репозитории [afteri-ru/trust-lang](https://github.com/afteri-ru/trust-lang). Актуальное
расписание версий - в [ROADMAP](https://github.com/afteri-ru/trust-lang/blob/main/ROADMAP.md),
история изменений - в [CHANGELOG](https://github.com/afteri-ru/trust-lang/blob/main/CHANGELOG.md).

## Структура репозитория

| Каталог | Назначение |
|---|---|
| `include/<component>/` | Заголовки C++ (`.hpp`/`.h`), по компоненту на каталог |
| `src/<component>/` | Реализация (`.cpp`) |
| `test/unit/` | Модульные тесты (GTest) |
| `test/lit/` | Интеграционные LIT-тесты (`.src` + FileCheck) |
| `test/vscode/` | Тесты VSCode-расширения |
| `examples/` | Примеры программ |
| `docs/content/` | Документация (исходники на русском) |

Компоненты: `syntax` (лексер/парсер), `ast`, `semantic`, `types`, `diag`,
`transpiler`, `pipeline`, `solver`, `lsp`, `debug` (DAP), `module_loader`, `vscode`.

## Сборка и тесты

```sh
cmake -B _build && cmake --build _build
ctest --test-dir _build --output-on-failure   # все тесты
make run_tests
```

См. [Сборка из исходников](../getting-started/build/).

## Стиль кода и инструменты

- Код должен соответствовать [CODESTYLE](https://github.com/afteri-ru/trust-lang/blob/main/CODESTYLE.md):
  именование, форматирование (`.clang-format`), запрещённые и обязательные паттерны.
- Статический анализ - через `.clang-tidy`.
- Тесты - обязательны для любых изменений; они адаптируются под изменения кода,
  а не наоборот.
- Тесты запускаются с таймаутом, чтобы не допускать бесконечных циклов.

## Правила для ИИ-агентов

Инструкции для агентов - в [AGENTS.md](https://github.com/afteri-ru/trust-lang/blob/main/AGENTS.md),
персистентная память и описание архитектуры - в `MEMORY.md` (корневой и
пер-компонентные). Логи задач ведутся в `.tasklog/`.
