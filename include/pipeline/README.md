# Pipeline - Transpiler Pipeline

## Назначение

Драйвер полного конвейера компиляции: чтение исходного кода → лексический анализ → макропроцессинг → синтаксический анализ → семантический анализ → транспиляция в C++ → сборка через Makefile.

## Особенности реализации

- **Полный конвейер** - orchestrates все этапы компиляции: read, lex, mmproc, parse, semantic, transpile, compile & link.
- **PipelineSteps** - битмаска шагов (LoadModule, ParseAST, Semantic, Transpile), определяет какие этапы выполнять. `determineSteps(EmitFlags)` маппит emit-режимы в шаги.
- **runPipeline()** - два перегруженных метода: базовый (без Transpile) и с Transpile. Возвращает `PipelineResult` (preprocessed + опциональный AST). **Stateless** - AST не кешируется в классе.
- **runTranspileAndSave() (private)** - объединяет общий код compile и emit-cpp: runPipeline + saveCppAndEmbedSourceMap.
- **emitOutput()** - единый метод для вывода результата в stdout для Tokens/Macros/AST. Принимает `const PipelineResult&`.
- **Централизованный execute()** - загружает файл один раз, настраивает DSL один раз, обрабатывает LexemesOnly быстрым путём.
- **PipelineResult** - структура результата: `preprocessed` (const SyntaxSeq*) + `astNodes` (std::optional\<SyntaxSeq\>), `isValid()`.
- **Stateless Pipeline** - `m_astNodes` удалён, AST возвращается через `PipelineResult`.
- **Парсинг CLI** - обработка аргументов командной строки (входной/выходной файл, типы сборки: object, static/shared lib, executable).
- **Генерация Makefile** - создаёт `Makefile` и `build.conf` рядом с `.cppt` для повторяемой сборки; параметры компиляции из CMake с возможностью переопределения.
- **Поддержка DSL** - компиляция встроенных макросов по умолчанию или загрузка из пользовательского файла через `--dsl`.
- **Обработка ошибок** - все ошибки проходят через `diag`, с немедленным прерыванием при фатальных.
