---
title: Языковые серверы (LSP / DAP)
tags: [tooling]
description: >
    trust-lsp и trust-dap - языковой сервер и адаптер отладки для TrustLang
weight: 20
---

В состав поставки входят два сервера:

- **`trust-lsp`** - языковой сервер, реализующий протокол **LSP**
  (Language Server Protocol) для поддержки языка в IDE.
- **`trust-dap`** - адаптер отладки, реализующий протокол **DAP**
  (Debug Adapter Protocol) для отладки программ.

Оба сервера используют единый arity-aware парсер драйвера (таблица `DriverOption`
+ `parseDriverArgs`) с подкомандой `server[=<port>]`.

## Возможности `trust-lsp`

`trust-lsp` поддерживает следующие методы LSP:

- **`textDocument/completion`** - автодополнение (в т.ч. для DSL-макросов и
  предопределённых имён).
- **`textDocument/hover`** - всплывающие подсказки с документацией (например, для
  мнемонических команд `@func`).
- **`textDocument/definition`** - переход к определению.
- **`textDocument/documentLink`** - ссылки на определения макросов в тексте.
- **`textDocument/codeAction`** - предлагаемые действия по коду.
- **`textDocument/publishDiagnostics`** - диагностика на лету (ошибки/предупреждения).
- Синхронизация документа (`didOpen` / `didChange` / `didClose`).

Также поддерживается рендеринг примеров в HTML (для песочницы/playground).

## Запуск

```sh
trust-lsp server
trust-dap server
```

Серверы подключаются расширением VSCode автоматически; вручную их можно запустить
как отдельные процессы, передав номер порта.

## Ссылки

- [Расширение для VSCode](vscode/)
- [Компилятор (CLI)](cli/)
