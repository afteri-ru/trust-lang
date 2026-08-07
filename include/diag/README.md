# Diagnostic & Options

## Назначение

Единая точка входа для диагностики, управления опциями компиляции и source-маппинга. Фасад Context объединяет DiagnosticEngine (вывод ошибок/предупреждений с форматированием), Options (именованные опции через X-макросы, CLI-парсинг, стековый push/pop) и Source Manager (хранение исходных файлов, конвертация offset ↔ line:column с LRU-кешем).

## Особенности реализации

- **Диагностика** — форматированный вывод ошибок/предупреждений с визуальным подчёркиванием диапазонов, подсчёт количества ошибок.
- **Source-маппинг** — конвертация позиций между trust- и C++-файлами, msgpack-сериализация source-map, нормализация путей.
- **Опции** — система именованных опций, определяемых через X-макросы, поддержка push/pop для временных изменений. Severity-диагностики задаются в `OPTIONS_LIST(M)` (формат `M(Name, "cli-name", Severity)`), управляются CLI `-W<имя>[=status]` (status: ignore/warning/error/fatal). В `OPTIONS_LIST` определены, в частности: `M(Embed, "embed", Warning)` — предупреждение за сам факт использования C++-вставки `{% ... %}` (независимо от имён внутри); `M(NoSigil, "sigil", Warning)` — предупреждение о нормализации простого имени без сигила в локальную `$x`. **Булевы feature-флаги** (не severity) задаются в `OPTIONS_FLAGS(M)` (формат `M(Name, "cli-name")`), управляются `Options::register_flag`/`is_enabled`/`set_enabled`. Пример: `M(Comments, "comments")` — подавление комментариев в C++-выводе; CLI `-Wno-comments` включает подавление (применяется в `trust.cpp` через `Options::parse_argv`).
- **Изолированность** — используется всеми компонентами (lexer, parser, transpiler, debug, lsp), не зависит от них.
- **`diag/protocol.hpp`** — общие конверсии в протокольные координаты (LSP/DAP), header-only: `severityToLsp(Severity)` (LSP DiagnosticSeverity 1..4) и `mapperRangeToProtocol(SourceMapWriter&, MapperRange)` → 0-based `ProtocolRange`. Используется LSP (`publishDiagnostics`/`codeAction`); будущий DAP — без дублирования.