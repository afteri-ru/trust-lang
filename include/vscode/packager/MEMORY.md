# MEMORY.md

> scope: include/vscode/packager
> role: persistent-memory
> last_reviewed: 2026-11-05
> review_period: 30
> max_size: 2500

## Architecture

Упаковка расширения VS Code: `package_vsix.cmake` (сборка `.vsix` из staging), `set_version.cmake`
(подстановка версии/ID в `package.json`), `merge_theme.py` (слияние темы с базовой Light+),
`content_types.xml.template` + `extension.vsixmanifest.template` (манифест VSIX).

## Facts and invariants

- **⚠ `package_vsix.cmake` НЕ читает `.vscodeignore`** — копируется весь staging, поэтому
  лишние файлы (исходники темы) удаляются на шаге `copy-vscode-sources`.
- **⚠ Версия берётся из `TRUST_VERSION`** (dev-сборка несёт git-хеш), поэтому упаковка требует
  чистого дерева (`require_clean_tree.cmake`: error в release, warning в dev).

## Decisions

## Relations

- `include/vscode/syntaxes`, `include/vscode/extensions/trust-lang`, `cmake/version.cmake`.
