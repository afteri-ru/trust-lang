---
title: Установка
tags: [getting-started, modules]
description: >
    Способы получения компилятора TrustLang: дистрибутив или сборка из исходников
weight: 10
---

Компилятор TrustLang распространяется либо как готовый дистрибутивный архив, либо
собирается из исходников. В состав поставки входят:

- компилятор **`trust`**;
- языковые серверы **`trust-lsp`** и **`trust-dap`**;
- библиотека времени выполнения **`trust-runtime`** (динамическая `trust-runtime.so`
  и статическая `trust-runtime.a`);
- файлы `VERSION` и `LICENSE`, а также `manifest.txt` с метаданными сборки.

## Дистрибутивный архив

```sh
cmake --build _build --target package
```

Архив также собирается автоматически как часть стандартной сборки
(`cmake --build _build`), независимо от запуска тестов. Архив (и `.vsix`-пакет,
если он собран) помещается в каталог `_build/dist/`. Имя архива кодирует атрибуты
сборки:

```
trust-lang-<version>-<git-hash>-<os>-<arch>.tar.gz
```

где `<os>` происходит из `CMAKE_SYSTEM_NAME` (например, `linux`), а `<arch>` из
`CMAKE_SYSTEM_PROCESSOR` (например, `x86_64`).

## Запуск готовых бинарников (зависимости для запуска)

Если вы **скачали готовый дистрибутив** (`.tar.gz`-архив или бинарники `trust`,
`trust-lsp`, `trust-dap`, `trust-runtime.so`), для их запуска нужны библиотеки
времени выполнения, которые линкуют собранные бинарники: `trust`/`trust-lsp`/
`trust-dap`/`trust-playground` - `libz3`, `libzstd` и `libz`; `trust-runtime.so` -
`libgmp`. LLVM линкуется статически, поэтому библиотеки LLVM для запуска
**не требуются**.

```sh
sudo apt-get update
sudo apt-get install -y \
    libz3-dev \
    libgmp-dev \
    libzstd-dev \
    zlib1g-dev \
    build-essential
```

## Требования

Тулчейн (clang-22, LLVM, GMP, bison/flex, lit) и конвейер сборки основаны на
POSIX/ELF. Поэтому рекомендуемый способ сборки на Windows - **WSL2**, где окружение
является нативным Linux, а архив собирается как пакет `linux-<arch>`.

Полностью нативная Windows-сборка требует отделения хранения встроенных заголовков
от ELF-секций и замены make-конвейера - это отдельная задача.

## Сборка из исходников (зависимости для сборки, apt-get)

Для **автономной сборки на своём компьютере** нужен полный набор зависимостей.
Ниже - пакеты для Debian/Ubuntu (проверено на Ubuntu 24.04).

### 1. LLVM-тулчейн 22 (clang-22, llvm-22-dev, llvm-22-tools)

Компилятор `trust` использует `clang++-22` и библиотеки LLVM 22, которых нет в
стандартных репозиториях Ubuntu. Установите их из официального репозитория
[apt.llvm.org](https://apt.llvm.org/):

```sh
wget https://apt.llvm.org/llvm.sh
chmod +x llvm.sh
sudo ./llvm.sh 22
```

### 2. Пакеты сборки (apt-get)

```sh
sudo apt-get update
sudo apt-get install -y \
    build-essential \
    cmake \
    pkg-config \
    flex \
    bison \
    libgtest-dev \
    nlohmann-json3-dev \
    libzstd-dev \
    libgmp-dev \
    libz3-dev \
    zlib1g-dev \
    libmsgpack-c-dev \
    python3 \
    python3-pip
```

> Список сборки включает все зависимости запуска. Формальная верификация (Z3)
> включается флагом `-DWITH_SOLVER=ON` (как в выпущенных бинарниках); `zlib1g-dev`
> нужен для линковки.

### 3. LIT (интеграционные тесты)

Инструмент `lit` входит в LLVM (каталог `/usr/lib/llvm-22/bin/lit`), но его нужно
сделать доступным в `PATH` либо установить через pip:

```sh
python3 -m pip install --user lit
```

> LIT-тесты и `ctest` также требуют `FileCheck` (из `llvm-22-tools`).

### 4. Опциональные зависимости

- **LibTorch (тензоры, `-DWITH_TORCH=ON`)**: бинарная библиотека скачивается вручную
  в `contrib/libtorch` (в стандартных репозиториях отсутствует).
- **Node.js/npm (сборка `.vsix`-пакета для VSCode)**: включена по умолчанию, можно
  отключить через `-DTRUST_BUILD_VSIX=OFF`. Пакеты: `nodejs`, `npm`.

## Ссылки

- [Сборка из исходников](build/)
- [Быстрый старт](quickstart/)
