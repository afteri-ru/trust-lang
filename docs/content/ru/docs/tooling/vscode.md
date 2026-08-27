---
title: Расширение для VSCode
tags: [tooling]
description: >
    Плагин TrustLang для Visual Studio Code: подсветка, LSP, DAP, песочница
weight: 30
---

Расширение TrustLang для Visual Studio Code объединяет поддержку языка в редакторе:

- подсветка синтаксиса `.src`-файлов;
- интеграция с `trust-lsp` - автодополнение, переход к определению, диагностика
  на лету;
- интеграция с `trust-dap` - запуск и отладка программ;
- встроенная [песочница (playground)](/ru/playground/) с живой пере-транспиляцией
  в C++.

## Тесты расширения

Тесты VSCode-расширения (`test/vscode/`) регистрируются как CTest-тесты
`vscode_unit`, `vscode_dap` и `vscode_lsp` и запускаются вместе с общим набором:

```sh
ctest --test-dir _build --output-on-failure
```

## Ссылки

- [Языковые серверы (LSP/DAP)](lsp-dap/)
- [Компилятор (CLI)](cli/)
