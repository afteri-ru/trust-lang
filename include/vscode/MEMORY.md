# MEMORY.md

> scope: include/vscode
> role: persistent-memory
> last_reviewed: 2026-08-26
> review_period: 30
> max_size: 6454

## Architecture

VS Code extension ↔ trust-dap (DAP) / trust-lsp (LSP, через `vscode-languageclient`).
Debug adapter: `TrustDebugAdapterDescriptorFactory → dap-adapter.js → DebugAdapterExecutable
(trust-dap [--project-dir])`. Пути к DAP/LSP серверам — из настроек `trust.dapPath`/`trust.lspPath`.
Сборка при F5 — в `resolveDebugConfiguration` (transpile → compile → trust-dap); preLaunchTask —
`TrustBuildTask` (тип `'trust-build'`).

## Facts and invariants

- **Изоляция сгенерированного C++ от clangd (language id `trusted-cpp`):** `.cppt`/`.hppt`
  отображаются на ОТДЕЛЬНЫЙ id `trusted-cpp`, НЕ на `cpp` — `cpp` параллельно обслуживает clangd,
  который падает (краши) на сгенерированном C++. `documentSelector` trust-lsp: `{trust, trusted-cpp}`;
  тип определяется по расширению, поэтому навигация `.src↔.cppt` работает независимо от названия id.
- **Настройки:** `trust.shebangMode` (`ignore|shebang-only|env-after-shebang|env-before-shebang`,
  default `env-after-shebang`) → `--shebang-mode`; `trust.lspArgs` (массив) — дополнительные env-опции
  анализа; общие опции (`--solver-mode`, `--keywords`, `-fsolver-loop-unroll`) trust-lsp принимает
  напрямую (разбираются общим `applyAnalysisArgs`). Dev-флаги — в блоке «Trust Lang: Developer»
  (`trust.dev.*`): `traceDAP`, `traceLSP` (→`--trace`), `highlightRanges` (по умолчанию сброшен;
  middleware `provideDocumentLinks` возвращает `[]`; hover/definition не затрагиваются).
- **Fixits сериализуются в `diagnostic.data`** (зарезервированное LSP поле), а НЕ в кастомное
  верхнеуровневое поле: `vscode-languageclient` сохраняет у диагностики только стандартные поля +
  `data`, поэтому кастомное поле отбрасывалось при `codeAction` и quickfix не появлялся.
- **Цветовая тема "Trust Language"** — самодостаточная (`trust-color-theme.json` генерируется на
  сборке `packager/merge_theme.py` из базы + переопределений; НЕ зависит от `extends` — он в ряде
  окружений не подхватывается). `configurationDefaults.textMateRules` — fallback и могут не
  применяться в части окружений. При добавлении scope нужно синхронизировать ОБА списка
  (textMateRules И тему).
- **Ловушка грамматики (терминатор макроса):** терминатор — `@@@@` (4 `@`), поэтому `end` ДОЛЖЕН
  быть `@@@@` (не `@@`/`@@@`) — иначе scope открывается и не закрывается, проглатывая остальной файл.
- **Упаковка VSIX двумя путями** (CMake `package_vsix.cmake` в `_build/dist/` и
  `packager/package-extension.sh` в `include/vscode/extensions/trust-lang/`). ОБА обязаны включать
  `syntaxes/trust.tmLanguage.json` (грамматику; на неё ссылается package.json) и прод-зависимости
  `node_modules/vscode-languageclient`. Для `vsce package` НЕ использовать `--no-dependencies` и НЕ
  исключать `node_modules/**` в `.vscodeignore`. Версия VSIX (имя файла и встроенная) —
  `TRUST_VERSION_FULL` = `VERSION-<git_hash>` (из `cmake/version.cmake`), чтобы в Extensions view
  отображалась версия с хешем сборки. `include/vscode/CMakeLists.txt` НЕ должен перечитывать
  VERSION/`git rev-parse` сам — брать готовые `TRUST_VERSION`/`TRUST_VERSION_FULL` из родительского scope.
