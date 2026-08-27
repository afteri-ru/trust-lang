---
title: Сборка из исходников
tags: [getting-started]
description: >
    Инструкция по сборке компилятора TrustLang из исходного кода
weight: 30
---

## Конфигурация и сборка

```sh
cmake -B _build && cmake --build _build
```

Сборка создаёт исполняемые файлы `trust`, `trust-lsp`, `trust-dap`, библиотеки
времени выполнения `trust-runtime.so` и `trust-runtime.a`, а также (опционально)
`.vsix`-пакет для VSCode.

## Тестирование

Тесты регистрируются в **CTest** и запускаются через:

```sh
ctest --test-dir _build --output-on-failure
```

Структура тестов:

- **unit/** - C++ модульные тесты (`test/unit/`), единый исполняемый файл
  `unit_tests`, зарегистрированный как один CTest-тест. При падении
  `ctest --output-on-failure` выводит полный отчёт GTest с именами падающих тестов.
- **lit/** - LIT-интеграционные тесты (`test/lit/`), регистрируются как CTest-тест
  `lit_tests` (запуск через CLI `trust` + FileCheck).
- **vscode/** - тесты VSCode-расширения (`test/vscode/`), регистрируются как
  `vscode_unit`, `vscode_dap` и `vscode_lsp` CTest-тесты.

`make run_tests` (псевдоним `cmake --build _build --target run_tests`) собирает
тестовые бинарники (unit_tests, trust, trust-dap, trust-lsp) и затем запускает тот
же CTest-набор.

## Опции сборки

- `WITH_SOLVER=ON` - включить линковку с Z3 (SMT-решатель) для режима
  `--solver-mode=calculate`. По умолчанию `OFF` - доступна только генерация текста
  SMT-LIB 2 без выполнения.

## Ссылки

- [Установка](install/)
- [Разработка](../contribute/)
