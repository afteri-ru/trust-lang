# Архитектура VS Code Extension ↔ trust-dap / trust-lsp

## Общая схема

```
VS Code Extension (extension.js)
    │
    ├── TrustDebugAdapterDescriptorFactory → dap-adapter.js → DebugAdapterExecutable(trust-dap [--project-dir <dir>])
    ├── LanguageClient → vscode-languageclient/node → trust-lsp (LSP server via stdin/stdout)
    ├── trust.openCppFile → customRequest('stackTrace') → trust-dap
    ├── resetDapPath / resetLspPath → config.update(...)
    ├── resolveDebugConfiguration → build pipeline (transpile + compile) → debugConfiguration
    └── provideDebugConfigurations → шаблон для launch.json
```

## LSP — vscode-languageclient

LSP-клиент реализован через пакет `vscode-languageclient`. Автоматически управляет жизненным циклом LSP-сервера, регистрирует providers на основе возможностей сервера, отслеживает открытие/закрытие документов, парсит JSON-RPC 2.0, буферизует и диспатчит сообщения.

Путь к LSP серверу берется из настройки `trust.lspPath`. Если путь не задан или файл не существует — показывается `showErrorMessage`.

## Build pipeline (resolveDebugConfiguration)

VSCode extension выполняет сборку при запуске отладки (F5) через `withProgress`:

1. **Транспиляция** — вызов trust-lang компилятора
2. **Компиляция C++** — вызов C++ компилятора
3. **Запуск trust-dap** — DAP сервер с аргументом `--project-dir`

Пути к файлам передаются через DAP-запрос `launch`. Временные файлы создаются в каталоге из настройки `trust.tempDir` (по умолчанию `.trust`).

## DAP-адаптер (dap-adapter.js)

Путь к DAP серверу берется из настройки `trust.dapPath`. Если путь не задан — выбрасывается ошибка.

CLI-аргументы trust-dap: только `--project-dir`. Параметры конфигурации отладчика (`sourceFile`, `cppFile`, `targetFile`, `gdbPath`) передаются через DAP-запрос `launch`.

## Build Task Provider (TrustBuildTask — preLaunchTask)

Зарегистрирован task provider с типом `'trust-build'`. Предоставляет три задачи:

1. **Trust: Transpile .src** — запускает trust-lang компилятор
2. **Trust: Compile .cppt** — запускает C++ компилятор
3. **Trust: Build all** — последовательно транспиляция + компиляция

Параметры берутся из настроек `trust.*`. Задачи используют `vscode.CustomExecution` с псевдотерминалом.

## Тесты

Тесты находятся в `test/vscode/extension/src/`. Включают `extension.test.js` (команды, DAP, LSP, TrustBuildTask) и `extension-utils.test.js` (утилиты). Используют mock VS Code API и vscode-languageclient.

## Выводы

1. Расширение следует стандартному DAP — все взаимодействие через стандартные DAP-команды.
2. Сборка выполняется в `resolveDebugConfiguration`.
3. CLI-аргументы trust-dap: только `--project-dir`, все пути через DAP launch.
4. LSP — через `vscode-languageclient`, автоматическая регистрация провайдеров.
5. preLaunchTask — через TrustBuildTask (тип `'trust-build'`).
