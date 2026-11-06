# builddriver - драйвер сборки и запуска

## Назначение

Слой, который соединяет артефакты компиляции с хостовым инструментарием: после того как `Pipeline`
произвёл `.cppt`/`.hpp` и модульные единицы, `builddriver` собирает `build-каталог`, вызывает
`toolchain` (Makefile/compile/link) и запускает результат (`--run`).

## Особенности реализации

- **Единственная ответственность — оркестрация.** `compileProgram` не содержит компиляционной
  семантики: артефакты приходят из `Pipeline::runCppToFile`/`generateModuleOutputs`, а сборка/линковка
  делегируется `toolchain` (`writeBuildFiles`/`compileAndLink`).
- **Кеш `--run`.** `tryRunCached` проверяет валидность ELF-секции `.debug_trust_hash`
  (`toolchain::runCacheValid`) и запускает исполняемый файл без перекомпиляции. Запись кеша формирует
  кодоген (шапка `.cppt`), формат — `utils/build_record.hpp`.
- **Разделение режимов.** `--emit-build-dir` генерирует build-каталог без сборки; `-fsingle-file`
  встраивает main-обёртку в `.cppt` главного модуля (`buildEntryMainSource`).

## Границы

- Зависит от `pipeline`, `toolchain`, `driver`, `package`, `session`, `utils`, `location`.
- НЕ является частью `pipeline`: конвейер компиляции от `toolchain` не зависит (E20/R34).
