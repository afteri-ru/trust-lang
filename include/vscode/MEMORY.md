# MEMORY.md

> scope: include/vscode
> role: persistent-memory
> last_reviewed: 2026-09-04
> review_period: 30
> max_size: 7745

## Architecture

VS Code extension ↔ trust-dap (DAP) / trust-lsp (LSP, через vscode-languageclient). Debug adapter:
`TrustDebugAdapterDescriptorFactory → dap-adapter.js → DebugAdapterExecutable (trust-dap [--project-dir])`.
Пути к серверам — `trust.dapPath`/`trust.lspPath`. Сборка при F5 — в resolveDebugConfiguration; preLaunchTask
— `TrustBuildTask` (тип `'trust-build'`).

## Facts and invariants

- **Изоляция от clangd (language id `trusted-cpp`):** `.cppt`/`.hppt` отображаются на ОТДЕЛЬНЫЙ id
  `trusted-cpp`, НЕ `cpp` (cpp параллельно обслуживает clangd, который падает/крашится на сгенерированном
  C++). `documentSelector` trust-lsp: `{trust, trusted-cpp}`; тип по расширению — навигация `.src↔.cppt`
  независима от названия id.
- **Настройки:** `trust.shebangMode` (default env-after-shebang) → `--shebang-mode`; `trust.lspArgs` (массив)
  — доп. env-опции анализа; общие опции (`--solver-mode`, `--keywords`, `-fsolver-loop-unroll`) trust-lsp
  принимает напрямую (общий applyAnalysisArgs). Dev-флаги `trust.dev.*`: traceDAP/traceLSP/highlightRanges
  (default сброшен; middleware provideDocumentLinks → []).
- **Fixits сериализуются в `diagnostic.data`** (зарезервированное LSP поле), НЕ кастомное верхнеуровневое
  поле: vscode-languageclient сохраняет у диагностики только стандартные поля + data, иначе кастомное поле
  отбрасывалось при codeAction и quickfix не появлялся.
- **Цветовая тема «Trust Language» — самодостаточная** (генерируется packager/merge_theme.py; НЕ зависит
  от `extends`); `configurationDefaults.textMateRules` — fallback. При добавлении scope синхронизировать
  ОБА списка (textMateRules И тему).
- **Ловушка грамматики (терминатор макроса):** терминатор — `@@@@` (4 `@`), поэтому `end` ДОЛЖЕН быть
  `@@@@` (не `@@`/`@@@`) — иначе scope открывается и не закрывается, проглатывая остальной файл.
- **Упаковка VSIX двумя путями** (CMake `package_vsix.cmake` и packager/package-extension.sh): ОБА обязаны
  включать `syntaxes/trust.tmLanguage.json` (на неё ссылается package.json) и `node_modules/vscode-languageclient`.
  Для vsce не использовать `--no-dependencies`/не исключать node_modules в .vscodeignore. Версия VSIX в
  CMake-пути (`include/vscode/CMakeLists.txt`) = эффективная `TRUST_VERSION` (режим сборки: release → без
  хеша, dev → с хешем). `include/vscode/CMakeLists.txt` НЕ перечитывает VERSION сам — брать готовые
  `TRUST_VERSION`/`TRUST_VERSION_SHORT` из родительского scope. Отдельная npx-цель `extension_vsix`
  (packager/package-extension.sh) всегда берёт VERSION+git-хеш (полная версия) и не знает о режиме сборки —
  учитывать при изменении модели версии.
- **VSIX — дефолтный артефакт только в release:** `TRUST_BUILD_VSIX` default ON в `CMAKE_BUILD_TYPE=Release`
  и OFF в dev (как `TRUST_BUILD_PACKAGE`); в dev VSIX по умолчанию НЕ собирается (принудительный
  `-DTRUST_BUILD_VSIX=ON` допустим для разработки расширения). Сборка VSIX (обе цели: `vsix-trust-lang`
  и npx `extension_vsix`) требует чистого дерева (cmake/require_clean_tree.cmake): имя/версия .vsix
  несут эффективную `TRUST_VERSION` (в dev — с git-хешем), поэтому ошибка при незакоммиченных изменениях
  в release и warning при принудительном включении в dev.

