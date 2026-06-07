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

## Language Configuration (`language-configuration.json`)

Конфигурация языка Trust для VS Code определяет:

- **Комментарии**: однострочные (`#`), блочные (`/* */`)
- **Скобки**: `()`, `[]`, `{}`, `<>` — используются для авто-отступов
- **Авто-закрытие пар**: `()`, `[]`, `{}`, `<>`, `""`, `''`, `` `` ``
- **Окружающие пары**: `()`, `[]`, `{}`, `""`, `''`, `` `` ``

## Подсветка синтаксиса (`trust.tmLanguage.json`)

TextMate-грамматика включает следующие категории:

- **Комментарии**: блочные (`/* */`), строчные (`#`), doc-комментарии (`/** */`, `///`, `##`)
- **Строки**: двойные кавычки (`""`), одинарные (`''`), интерполируемые (`` `` ``), raw (`r"..."`, `r'...'`)
- **Ключевые слова**: макросы (`@...`), управляющие конструкции, переменные (`$...`), функции (`%...`), типы (`:...`)
- **Операторы**: все операторы языка Trust
- **Числа**: целые, с плавающей точкой, рациональные, комплексные
- **Макросы `@@...@@`**: выделяются scope `markup.other.trust.macros` с серым подчеркиванием
- **Переменные**: `__...__` (системные), `__...` (языковые), `_...` (именованные), обычные

## Цветовая схема для `.src` файлов (`package.json` → `configurationDefaults`)

Все цвета для scope-ов TextMate-грамматики задаются через `configurationDefaults."[trust]".editor.tokenColorCustomizations.textMateRules` в `package.json`. Это гарантирует, что подсветка работает при любой активной теме VS Code, не требуя переключения на тему "Trust Language".

Список scope-ов и их цветов соответствует тёмной схеме (аналог Dark+).

- Ключевые слова (control, macro): `#C586C0`
- Функции (support, macro): `#DCDCAA`
- Переменные: `#9CDCFE` / `#4FC1FF`
- Типы: `#4EC9B0`
- Числа (int, float, rational, complex): `#B5CEA8`
- Строки: `#CE9178`
- Комментарии: `#6A9955`
- Операторы: `#D4D4D4`
- Escape-символы: `#D7BA7D`
- Макросы `@@...@@`: `#808080` с подчёркиванием

## Выводы

1. Расширение следует стандартному DAP — все взаимодействие через стандартные DAP-команды.
2. Сборка выполняется в `resolveDebugConfiguration`.
3. CLI-аргументы trust-dap: только `--project-dir`, все пути через DAP launch.
4. LSP — через `vscode-languageclient`, автоматическая регистрация провайдеров.
5. preLaunchTask — через TrustBuildTask (тип `'trust-build'`).
6. Language configuration определяет `#` как line comment, `/* */` как block comment, поддерживает все виды скобок и кавычек.
7. TextMate-грамматика включает подсветку макросов `@@...@@` с scope `markup.other.trust.macros`.
8. Все цвета для файлов `.src` задаются через `configurationDefaults` и применяются независимо от активной темы VS Code.

- Отдельная цветовая тема "Trust Language" удалена — нет необходимости переключать всю схему ради одного языка.
- При добавлении нового scope достаточно добавить одно правило в `textMateRules` в `package.json`.
