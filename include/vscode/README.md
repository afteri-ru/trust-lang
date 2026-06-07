# VSCode Extension

## Назначение

Расширение для Visual Studio Code, обеспечивающее интеграцию языка TrustLang: подсветка синтаксиса `.src` файлов, запуск отладки через DAP (trust-dap), и сборка проекта.

## Особенности реализации

- **Тип файла** — регистрирует `.src` как trust-lang исходный код с syntax highlighting.
- **Debug adapter** — запуск trust-dap как DAP-сервера с параметрами из настроек расширения.
- **Build task** — pre-launch транспиляция (trust → C++) и компиляция (C++ → ELF) с отображением прогресса.
- **Контекстное меню** — команда "Trust: Open Generated C++ File" для двухоконной навигации.
- **Breakpoints** — поддержка SetBreakpoints (F9) с трансляцией позиций через source-map.