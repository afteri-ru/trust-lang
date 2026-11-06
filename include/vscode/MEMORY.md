# MEMORY.md

> scope: include/vscode
> role: persistent-memory
> last_reviewed: 2026-11-10
> review_period: 30

## Architecture

VS Code extension: trust-dap (DAP) и trust-lsp (LSP через vscode-languageclient). Пути к серверам —
`trust.dapPath`/`trust.lspPath`. Сборка при F5 — в `resolveDebugConfiguration` (preLaunchTask типа
`'trust-build'`, с `withProgress`: transpile → compile → trust-dap). Исходники расширения —
`extensions/trust-lang/` (package.json, `src/`, темы), грамматика — `syntaxes/`, упаковщик — `packager/`.

## Facts and invariants

- **⚠ trap: изоляция от clangd через language id `trusted-cpp`.** `.cppt`/`.hppt` отображаются на
  ОТДЕЛЬНЫЙ id, НЕ `cpp` (clangd параллельно падает на сгенерированном C++). `documentSelector`
  trust-lsp — `{trust, trusted-cpp}`; навигация `.src↔.cppt` — по расширению, не по id.
- **⚠ trap: fixits кладутся в `diagnostic.data`** (зарезервированное LSP-поле), НЕ в кастомное
  верхнеуровневое: vscode-languageclient сохраняет только стандартные поля + data, иначе quickfix не
  появляется.
- **⚠ trap: терминатор макроса — `@@@@` (4 `@`)** в грамматике, поэтому `end` ОБЯЗАН быть `@@@@`;
  иначе scope не закрывается и проглатывает остаток файла.
- **⚠ trap: цветовая тема «Trust Language» самодостаточна** (генерируется `packager/merge_theme.py`,
  без `extends`); `configurationDefaults.textMateRules` — fallback. Новый scope синхронизировать в
  ОБОИХ списках (textMateRules И тема).
- **VSIX собирается CMake-целью `vsix-trust-lang`** (`package_vsix.cmake`): она обязана включать
  `syntaxes/trust.tmLanguage.json` и `node_modules/vscode-languageclient` (для vsce НЕ использовать
  `--no-dependencies`/не исключать node_modules). Версия = эффективная `TRUST_VERSION` (dev несёт
  git-хеш); `include/vscode/CMakeLists.txt` не читает VERSION сам — берёт `TRUST_VERSION`/
  `TRUST_VERSION_SHORT` из родительского scope.
- **VSIX — дефолтный артефакт только в release:** `TRUST_BUILD_VSIX` default ON в Release, OFF в dev
  (принудительный `-DTRUST_BUILD_VSIX=ON` допустим). Сборка (обе цели) требует чистого дерева.
- **Опции анализа:** `trust.shebangMode` (default env-after-shebang) → `--shebang-mode`; `trust.lspArgs`
  (массив) — доп. env-опции; общие опции (`--solver-mode`, `--keywords`, `-fsolver-loop-unroll`) trust-lsp
  принимает напрямую. Dev-флаги `trust.dev.*`: traceDAP/traceLSP/highlightRanges (default сброшен).

## Decisions

## Relations
