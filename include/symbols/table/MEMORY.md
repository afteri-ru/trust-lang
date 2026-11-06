# MEMORY.md

> scope: include/symbols/table
> role: persistent-memory
> last_reviewed: 2026-11-10
> review_period: 30

## Architecture

`ScopeStack`/`Scope` (persistent-дерево лексических скоупов + курсор активного пути), `SymbolArena`
(арена СИНТЕТИЧЕСКИХ `Symbol`), `SymbolKeyFactory` (построение `StorageKey`), `symbol.hpp`
(`Symbol`/`ExportRecord`).

## Facts and invariants

- **⚠ `ScopeKind` — роль узла, не глубина:** `Global`/`ModuleRoot`/`Block`. `ModuleRoot` фиксируется
  ОДИН раз (первый `ModuleNode`, иначе первый вложенный скоуп); предикаты модульного уровня —
  по `kind`, арифметика по `depth()` в них не участвует.
- **⚠ `ScopeStack` persistent:** `pop()` НЕ удаляет узел, а помечает `closed` и снимает с активного
  пути; `Scope::symbols` живёт до конца прогона, поэтому объявления (включая локальные) доступны
  после выхода из блока. Индексы `at(i)`/`0..depth()-1` — индексы АКТИВНОГО пути; `node(i)` — узел
  дерева (включая закрытые).
- **⚠ Адреса `Symbol` стабильны:** узлы — `std::deque` (push_back не инвалидирует), элементы —
  `std::map` (move переносит узлы). Поэтому `Symbol*` переживает `pop()` и перемещение таблицы;
  `ModuleSurface` хранит указатель на живой символ скоупа.
- **`SymbolArena` — только синтетика:** слитые наборы перегрузок (`mergeCallableCandidates`) и
  символы без скоупа; снимки объявлений из «снимаемых» скоупов больше не создаются.
- **Скоуп-стек: вход/выход — только `enterScope`/`exitScope`;** прямой `push/pop` из анализаторов
  запрещён (рассинхронизирует хуки).
- **Ключи интернируются;** `StorageKey` — структурный (visibility class, module, namespacePath, name).
- **⚠ Два представления типа — НЕ сводить:** `Symbol::type` (QualType в таблице семантики) и
  `VarDecl::inferredType`/`IdentName::resolvedTypeId` (AST-факты для КОДОГЕНА) — разные потребители
  (кодоген `SymbolTable` не читает). `overloads[].signature` и представитель `Symbol::type` — ПРОЕКЦИЯ
  набора, не дубль.

## Relations

- `symbols/decl`, `symbols/import`, `types/storage_key`, `semantic/core`.
