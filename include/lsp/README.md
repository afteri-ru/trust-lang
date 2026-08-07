# LSP — Language Server Protocol

## Назначение

LSP-сервер на stdin/stdout для интеграции с IDE (VSCode). Обеспечивает автодополнение, go-to-definition, hover-диагностику, форматирование и двухоконную навигацию между trust- и C++-кодом.

## Особенности реализации

- **In-process транспиляция** — преобразование Trust → C++ выполняется внутри LSP-сервера через Context (без внешнего компилятора), с построением source-map в памяти.
- **Двухоконная навигация** — source-map позволяет переключаться между исходным `.src` и сгенерированным `.cppt` кодом; go-to-definition и hover работают в обе стороны.
- **Кеширование** — результат транспиляции и source-map кешируются по хешу содержимого, с инвалидацией при изменениях и auto-recovery при cache-miss.
- **Единый источник имён для автодополнения** — без лексических проходов по тексту: пользовательский код из таблицы анализатора `SymbolIndex`, встроенные типы/методы/функции/macros из глобального `BuiltinCatalog` (shared ядро `TypeRegistry::builtinCore()` + predef/DSL-макросы).
- **Диагностика + quickfix** — публикация всех severity (`publishDiagnostics`) и `textDocument/codeAction` (kind `quickfix` по `fixits`); конверсии Severity→LSP/range — в общем `diag/protocol.hpp`.
- **Transport** — Content-Length / JSON-RPC 2.0 поверх stdin/stdout, без внешних зависимостей от LSP-клиента.
- **Playground-вывод** (`--json` / `--html`) — godbolt-стиль двухоконный playground (Trust | сгенерированный C++): JSON-контракт `{source, cpp, trustToCpp, cppToTrust}` для live-пере-транспиляции и самодостаточный HTML-фрагмент с встроенными Monaco-редакторами, Monarch-подсветкой Trust и JS-навигацией. Подробнее — `lsp/HTML.md`.