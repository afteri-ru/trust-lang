# Архитектура VS Code Extension ↔ trust-dap / trust-lsp

## Общая схема

```
VS Code Extension (extension.js)
    │
    ├── TrustDebugAdapterDescriptorFactory → dap-adapter.js
    │       │
    │       └── DebugAdapterExecutable(trust-dap, args[])
    │
    ├── LanguageClient                    → vscode-languageclient/node
    │       │
    │       └── trust-lsp (LSP server via stdin/stdout)
    │
    ├── trust.openCppFile        → customRequest('stackTrace') → trust-dap
    ├── resetDapPath             → config.update(...)
    ├── resetLspPath             → config.update(...)
    │
    ├── resolveDebugConfiguration → проверяет .src файл
    └── provideDebugConfigurations → шаблон для launch.json
```

### LSP — vscode-languageclient

LSP-клиент реализован через пакет `vscode-languageclient` (класс `LanguageClient`).
Он автоматически:
- Управляет жизненным циклом LSP-сервера (запуск, остановка, перезапуск)
- Регистрирует `providers` (definition, hover, completion и т.д.) на основе
  возможностей, объявленных сервером в ответе `initialize`
- Отслеживает открытие/закрытие документов (textDocument/didOpen, didClose)
- Парсит протокол JSON-RPC 2.0 с Content-Length заголовками
- Буферизует и диспатчит входящие сообщения
- Предоставляет tracing/output channels
- Управляется через `context.subscriptions` — автоматически останавливается при деактивации

**Путь к LSP серверу** берется исключительно из настройки `trust.lspPath`.
Если путь не задан или файл не существует — показывается `showErrorMessage` с явным сообщением,
в `Trust Lang LSP` output channel пишется `[ERROR]`, статус-бар устанавливается в `$(error)`.
Fallback-поиск не используется.

### DAP-адаптер (dap-adapter.js)

**Путь к DAP серверу** берется исключительно из настройки `trust.dapPath`.
Если путь не задан или файл не существует — `createDebugAdapterDescriptor()` выбрасывает
ошибку с явным сообщением, также вызывается `vscode.window.showErrorMessage()`.
Fallback-поиск в PATH не используется.

Вынесен из `extension.js` в отдельный модуль для:
- Возможности unit-тестирования без активации extension
- Чистого разделения ответственности (DAP-логика отдельно от команд расширения)

Аргументы `trust-dap`:
- `--project-dir` — корень проекта
- `--lldb-server` — путь к lldb-server (опционально)

Параметры конфигурации отладчика (`sourceFile`, `cppFile`, `targetFile`, `mapFile`)
передаются в launch-запросе DAP, не через CLI-аргументы.

## Тесты

### Расположение

- `test/vscode/extension/src/extension.test.js` — основной тестовый файл
- `test/vscode/extension/src/extension-utils.test.js` — тесты утилит
- `test/vscode/extension/src/__mocks__/vscode.js` — mock VS Code API
- `test/vscode/extension/src/__mocks__/languageclient.js` — mock vscode-languageclient

### Покрытие

#### extension.test.js

1. **activate(): command registration** — проверка регистрации команд:
   - `trust.openCppFile`
   - reset-команды: `resetDapPath`, `resetLspPath`
   - DAP factory, debug session handlers, configuration provider, LSP client

2. **TrustDebugAdapterDescriptorFactory** — проверка создания DAP-исполняемого файла:
   - Полный конфиг (project-dir, lldb-server)
   - Минимальный конфиг (только project-dir)
   - Проверка отсутствия старых аргументов

3. **resolveDebugConfiguration** — проверка провайдера конфигурации:
   - null при отсутствии .src файла
   - debugConfiguration при наличии .src файла
   - Генерация дефолтного конфига

4. **sendCustomRequest** — проверка DAP customRequest:
   - Успешный запрос
   - Ошибка сессии

5. **trust.openCppFile** — проверка открытия C++ файла:
   - Предупреждение без .src файла
   - Успешный запрос к debug-сессии через stackTrace
   - Fallback при ошибке сессии

6. **DAP session commands** — mock DAP-команд:
   - continue, next, stepIn, stackTrace, variables, disconnect
   - Ошибка при неизвестной команде

7. **LSP Client initialization** — проверка создания LSP-клиента с установленным `lspPath`:
   - Создание status bar элемента
   - Создание output/traceOutputChannel
   - LanguageClient получает корректные serverOptions.command и clientOptions.outputChannel

8. **LSP binary not found** — проверка обработки ошибки запуска LSP (start().catch):
   - `showErrorMessage` с сообщением о неудачном старте
   - `[ERROR]` в output channel
   - Status bar в `$(error)`

9. **LSP client configuration** — проверка регистрации обработчиков:
   - onDidChangeState и onNotification
   - start() вызывается на LanguageClient

10. **Diagnostics: LSP path errors** — проверка диагностики путей LSP:
    - Empty `lspPath` → ошибка "path not configured", LanguageClient не создаётся
    - Non-empty `lspPath` → LanguageClient создаётся, start() вызывается
    - `onDidChangeState` (running → stopped) → `[ERROR]` в output, status bar `$(error)`

11. **Diagnostics: DAP path errors** — проверка диагностики путей DAP:
    - Empty `dapPath` → throw "path not configured"
    - Non-empty `dapPath` → DebugAdapterExecutable создаётся с command='trust-dap'

#### extension-utils.test.js

- resolvePath — подстановка `${workspaceFolder}`, `~`, абсолютные/относительные пути
- resolveDapVariables — подстановка переменных: workspaceFolder, file, fileBasename, fileBasenameNoExtension, fileDirname, fileExtname
- updateStatusBar — состояния Running (Debugging), Idle

### Mock VS Code API

- **workspace.getConfiguration** — возвращает MockTrustConfiguration с настройками по умолчанию
- **debug.DebugAdapterExecutable** — класс, создающий объект { command, args }
- **debug.registerDebugAdapterDescriptorFactory** — регистрирует фабрику
- **debug.registerDebugConfigurationProvider** — регистрирует провайдер
- **MockDebugSession** — имитирует debug-сессию с customRequest для команд:
  - `continue`, `next`, `stepIn`, `stepOut`, `pause`, `disconnect` → `{ body: {} }`
  - `variables` → `{ body: { variables: [] } }`
  - `stackTrace` → `{ body: { stackFrames: [{ id: 0, name: 'main', source: { path: '/tmp/test.cpp' }, line: 42 }] } }`
- **MockTextEditor** — имитирует редактор с .src файлом
- **MockStatusBarItem** — имитирует status bar
- **MockOutputChannel** — захватывает вывод

## Выводы

1. **Расширение следует стандартному DAP** — все взаимодействие через стандартные DAP-команды (stackTrace, continue, next, stepIn, etc.)
2. **Сборка вынесена** — компиляция Trust → C++ и C++ → ELF выполняется отдельно, до запуска отладки
3. **Покрытие тестов** — 55 тестов (45 extension + 10 extension-utils)
4. **Взаимодействие с trust-dap** через `DebugAdapterExecutable` (запуск процесса) и DAP-протокол (JSON-RPC через stdin/stdout или TCP)
5. **LSP** — через `vscode-languageclient`, автоматическая регистрация провайдеров и управление сервером