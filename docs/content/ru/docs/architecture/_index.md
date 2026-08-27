---
title: Архитектура
tags: [architecture, compiler]
linkTitle: Архитектура
description: >
    Архитектура компилятора trust и конкретные технические решения
weight: 8
simple_list: true
---

[Doxygen](/doxygen/index.html)

Раздел описывает внутреннюю архитектуру компилятора TrustLang и конкретные
технические решения. Актуальная реализация - **транспилятор `trust`** в код C++ с
последующей сборкой через внешний C++ компилятор (по умолчанию `clang++`,
стандарт `-std=c++23`).

## Общая схема

```
Исходный .src -> Лексический/синтаксический анализ (Flex/Bison) -> Term-дерево
  -> Конвертер Term->AST -> Семантический анализ (NameResolutionPass)
  -> CppTranspiler -> .cppt -> сборка (Makefile + clang++) -> исполняемый файл
```

Основные компоненты: `syntax` (лексер/парсер), `ast`, `semantic`, `types`, `diag`,
`transpiler`, `pipeline`, `solver` (Z3/SMT), `lsp`, `debug` (DAP), `module_loader`,
`vscode`. Подробности этапов компиляции описаны на подчинённых страницах раздела.
