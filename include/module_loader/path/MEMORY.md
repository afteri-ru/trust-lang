# MEMORY.md

> scope: include/module_loader/path
> role: persistent-memory
> last_reviewed: 2026-11-10
> review_period: 30

## Architecture

`module_path_policy` — ЧИСТАЯ политика пути (резолв `\file`/`\\group`/`\\\pkg`, форма файл/каталог/любая,
обход группы, каноникализация, `TRUST_PACKAGE_PATH`); без `Context` и диагностик (ошибка строкой).

## Facts and invariants

- **Префиксы — тип ПЕРВОГО компонента:** `\` файл, `\\` группа, `\\\` пакет; `\\dir` — нерекурсивно
  `.src`; `\\\pkg()` — first-level модули + корневые `.src`; `dir.src` + `dir/` — ошибка; build-каталог —
  не корень поиска.
- **⚠ Относительные якоря:** `\$$` — self-файл; `\\$$`/`\\\$$` — каталог/пакет (пусто для корня);
  standalone-импорт запрещён.
- **Групповой импорт нерекурсивный** и разворачивается в файловые импорты.

## Relations

- `module_loader` (оркестрация), `package` (пакеты), `pipeline` (scope root/`-I`).
