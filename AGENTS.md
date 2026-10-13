# Agent Guidelines

## Planning

### 1. Planning tasks

**Think through and understand the task before writing code. Step back when necessary.**

- State assumptions clearly. Present alternatives. Never guess - ask.
- Convert tasks into verifiable success criteria before writing code.
- For multi-step tasks, outline a brief plan with checkpoints.

---

## Rules for coding

### 2. Simplicity First

**Minimum code for the problem. Nothing speculative.**

- No features beyond what was asked. No unrequested abstractions or "flexibility".
- No error handling for impossible scenarios.
- If you write 200-500 lines and it could be 50, rewrite.
- Если размер исходного файла превышает 50-100кБ, то это кандидат для рефакторинга и раздления файла на по зонам ответственности (класам или логике).
- Если размер исходного стал более 150кБ, то должен быть предложен план по его рефакторингу для упрощения (раздлениею) на неслколько частей.
- Documentation is expressed by names first: variables, functions, parameters, types and
  constants must be self-describing (meaning, units, ownership). A comment explains WHY
  (invariant, constraint, non-obvious decision), not WHAT the code does. A comment that
  restates the code must be replaced by a rename or an extracted function.

### 3. Surgical Changes

**Touch only what the task requires. Clean up your own orphans.**

- Match existing style. Do not "improve" adjacent code or formatting.
- Remove imports/variables/functions YOUR changes made unused.
- Do not refactor unrelated code.
- Delete obsolete CODE: an implementation replaced by a new one after a refactor, a duplicated
  helper, or an unused utility (hasher, free function, alias of another operation) MUST be
  removed in the same change - not kept "for the future" and not hidden behind a deprecation
  wrapper.
- Data structures are NOT dead code. Such a field is removed only by an explicit architecture decision, never as a side effect of "dead code" cleanup.
- If an architectural field has no reader, document its purpose/contract instead of deleting it;
  never introduce a second ad-hoc representation of the same data.

### 4. Eliminate Duplication

**Reuse. One source of truth per concern. Prefer compile-time checks.**

- Check for existing implementations before writing new code.
- Avoid duplicating logic or checks.
- Prefer `static_assert` and type system over runtime checks.

### 5. No fallback for invalid data - raise an error (use FAULT and EXPECT with a diagnostic)

**Invalid input → FAULT or EXPECT, never use a silent fallback.**

- Any function that receives invalid data **must** call FAULT() or EXPECT(), not return {}, nulllopt, or any silent default result.

- When changing the API contract, update your tests to reflect the new contract. Fallback code cannot be added to satisfy old test requirements.

- An empty value for std::optional is only allowed when the value is missing. It is invalid when invalid input data is passed (an error/FAULT/EXPECT must be returned).

- Every `EXPECT()` and `FAULT()` call must include a human-readable diagnostic string:
  - `FAULT("description", args...)` - description is the first argument;
  - `EXPECT(expr && "description")` - description is added via `&& "..."`.
  - A bare `EXPECT(expr)` without a description is allowed for obvious checks only.

### 6. No Implicit Backward Compatibility

**Don't preserve history unless asked.**

- Never maintain backward compatibility unless explicitly required.
- Remove old code without deprecation layers or comments explaining removal.
- Breaking changes are default unless compatibility is specified.

### 7. Read and obey comments

**Always read comments and execute explicit requirements stated in them.**

- Comments may contain critical constraints (e.g. "The output directory cannot be deleted!").
- Never ignore, override, or remove a comment that states an explicit requirement.
- If a comment contradicts your intended change, stop and reconsider.


### 8. Use of Git or `.git` is prohibited.

**Use `.git` or any changes from `git`, or modifying or reorganizing Git data in any way, is strictly prohibited.**

- Any use of the `git` (including reading history ) is prohibited.
- Deny reading of the `.git` directory.
- Not allowed reading `git` history or creating stash.
- `grep` MUST **exclude** `.tasklog`, `.git`, `_build`, `.trust`, `node_modules` and other temporary/derived dirs (never scan them for patterns; never `grep -r` over the whole repo so they get included). **Обязательная форма** - явные `--exclude-dir` для всех временных/производных каталогов, а лучше и сужение корня поиска:
  - `grep -rn '<pattern>' src include test examples cmake --exclude-dir={_build,.tasklog,.git,_trust,.trust,node_modules}`
  - Нельзя: `grep -rn '<pattern>' .` (без исключений), `grep -rn '<pattern>' src` (если в `src` попадают производные файлы), поиск в `_build`/`.tasklog`/`.git` целиком.
  - `search_codebase`/поиск по коду - только по исходным каталогам (`src`, `include`, `test`, `examples`), без `_build`, `.tasklog`, `.git`.

---

## Quality Assurance

### 9. Follow CODESTYLE

**All code must follow [CODESTYLE.md](CODESTYLE.md) - naming conventions, formatting, prohibited and required patterns.**

- The naming conventions, formatting, prohibited and required patterns, code rules must follow [CODESTYLE.md](CODESTYLE.md)
- For compiler options and build targent in `CMakeLists.txt`.
- Run `clang-format` before committing.
- Run `clang-tidy` and fix all warnings.

### 10. Tests Are Non-Negotiable

**Every change needs test coverage. Never disable or skip tests.**

- Write tests for additions and modifications: normal paths, edge cases, errors.
- Never remove, skip, or disable tests without explicit permission.
- Fix underlying code on failure -- do not silence the test.
- Tests **MUST** never be silently skipped - missing test infrastructure (GTest, lit, python3, etc.) is a **BUILD FAILURE, not a silent skip or GTEST_SKIP()**.
- Do not delete generated/output files unless asked.
- All tests **MUST** be executed with a timeout (e.g., via `timeout` command) to prevent infinite loops or resource exhaustion.
- **Ожидание фоновой сборки/тестов:** при опросе фоновых команд использовать РАЗНЫЕ интервалы и не повторять одинаковые паузы подряд; одинаковые повторяющиеся вызовы могут быть ошибочно приняты за зацикливание модели.
- **Tests are ALWAYS adapted to code changes, NEVER the reverse.** If a change intentionally alters behavior/structure (incl. AST/term shape, grammar, codegen), update the affected tests to reflect the new contract. Do NOT revert/weaken a correct change to keep outdated tests passing. This also applies to parser tests that pin internal term/AST structure: they must be updated alongside the change.



### 11. Architecture and Persistent Memory (`MEMORY.md`)

**Do not read header or source files to analyze architecture.**

- The `MEMORY.md` file, when present, is the **sole source** of architectural information and of
  persistent memory for its scope (architecture + facts, decisions, relations). The component root
  `include/<component>/MEMORY.md` describes **only the public interface and general component
  rules**; implementation details live in `include/<component>/<internal-dir>/MEMORY.md` (each
  internal-dir file also covers the matching `src/<component>/<internal-dir>/`). Read the root file
  first, then the internal-dir file for the touched files.
- **A `MEMORY.md` is optional for a small implementation** (≤3 source/header files, ≤16 KiB in
  total): its non-obvious invariants are expressed as WHY comments in those files instead.
- **The minimum useful size is 1000 bytes.** A file carrying less information that cannot be
  derived from code/API/docs is not created; an existing smaller file is deleted and its knowledge
  moved to code comments or the directory `README.md`.
- When present, the file keeps the mandatory header and sections defined in
  `.clinerules/README.md`.
- A root `MEMORY.md` at the project root holds project-level facts, decisions, and
  cross-component relations.
- Read individual source/header files only when the task explicitly requires it or when modifying that specific file.
- Do not scan the project for "understanding" - read `MEMORY.md` or `README.md` first.
- **Exception**: when the task requires modifying/refactoring a specific type, read its complete
  definition (all fields, semantics, relationships) first - see `.clinerules/TASK.md`
  («Read structures before refactoring»). This does not contradict the rule: full type reading
  is done for the type being changed, not for analyzing the architecture.


### 11.1 Раскладка компонента (internal-подкаталоги)

- Корень `include/<c>/` — только объявленные `PUBLIC_HEADERS`, `CMakeLists.txt`, `MEMORY.md`, `README.md`.
  Документы разработки (`*.md`) и детали реализации в корне запрещены.
- Детали — в подкаталогах: `INTERNAL_DIRS` (`include/<c>/<dir>` и/или `src/<c>/<dir>`) или `PUBLIC_DIRS`
  (намеренно публичные, напр. `runtime/trust`). Подкаталог рекурсивен, классификация — по длиннейшему
  объявленному префиксу (`cmake/internal_dirs.cmake`).
- Реализация не лежит в корне `src/<c>/` у немалых компонентов (малый — ≤4 TU, ≤32 КиБ, ≤4 публичных
  заголовков; порог в `cmake/internal_dirs.cmake`).
- Корень компонента не добавляется в публичные include-пути (`INCLUDE_PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}`
  запрещён): иначе граница обходится короткой формой `#include "<dir>/..."`.
- Гейты: `subdir_layout_guard` (раскладка), `internal_boundary_guard` (чужие internal-подкаталоги),
  `public_api_guard` (полнота/доступность API), `internal_memory_guard` (память). Существующий долг
  фиксируется allowlist'ом в `cmake/check_subdir_layout.cmake` и может только сокращаться:
  неиспользованная запись — ошибка.
- Термин для каталога — «internal-подкаталог» / «публичный подкаталог» («зона»/`zone` — запрещены).

### 12. Keep file `MEMORY.md` and `README.md` Synchronized

**`MEMORY.md` and `README.md` must always reflect the actual implementation.**

- Optionality (§11) does not relax synchronization: a `MEMORY.md` that exists must match the code.
- If a code change reveals a discrepancy between `MEMORY.md`, `README.md` and the code - stop and report it.
- Update `MEMORY.md` in the same change set as the code modification.
- Mismatch between `MEMORY.md` or `README.md` and implementation is treated as a bug.

### 13. Source of documentation `docs/content`

**The Russian-language section `docs/content/ru` is the ONLY original source of documentation.**

- `docs/content/en` is a synchronous translation of `docs/content/ru` and is never an original source:
  it is edited only together with the corresponding `ru` change. English pages must not link to
  Russian-language originals (ru.wikipedia, habr.com/ru, etc.); use English equivalents or drop the link.
- The documentation serves as the basis for creating a site (taking into account the language prefix), so all links and anchors are written in English letters.
- The documentation describes the project's vision for its ultimate goal and may differ from the current implementation. Unrealized capabilities should be clearly identified (highlighted).
- **`docs/content` — только пользовательская документация. Материалы разработки (правила сборки, внутренние инструкции, соглашения об инструментах) туда писать ЗАПРЕЩЕНО:** их место — `AGENTS.md`, `CODESTYLE.md`, `README.md`, `MEMORY.md` компонентов и `.tasklog/`.
- Documentation serves only as a reference. A more important source of information for work is the task description or architecture.
---

**These guidelines are working if:** fewer unnecessary changes in diffs, fewer rewrites due to overcomplication, and clarifying questions come before implementation.

---

### 14. Build: options in root `CMakeLists.txt`, memory-aware parallelism

Опции сборки объявлены в корневом [`CMakeLists.txt`](CMakeLists.txt). Политика параллелизма
реализована ОДИН раз в `cmake/parallelism.cmake` (числа и формула — только там); соглашения о путях,
манифесте компонента и публичном API проверяются автоматически (`trust_component` + `cmake/check_*.cmake`).

- Сборка: `make` (корневой `Makefile` делегирует в `_build/trust-build.sh`); `-j` вручную
  НЕ передаётся — число задач вычисляется из ДОСТУПНОЙ памяти (MemAvailable) и запаса в cgroup.
- Тесты: `timeout 900 make test` (эквивалент `timeout 900 cmake --build _build --target run_tests`).
- cgroup опционален: `systemd-run --user --scope -p MemoryMax=…` (запас внутри лимита сам уменьшит
  число задач). Одновременные сборки сериализуются `flock` (`_build/.trust-build.lock`).
- Фоновая сборка: `_build/trust-build.sh --background <pid> <log>`; ожидание — ОДИН вызов
  `_build/trust-build.sh --wait <pid> <log>` (внутри разные интервалы). Повторяющиеся
  одинаковые вызовы запрещены: агентский рантайм принимает их за зацикливание.
- Бинарник нулевого размера — след OOM: удалить файл и пересобрать цель.

## Persistent Memory (`MEMORY.md`)

Persistent memory is stored in `MEMORY.md` files together with the architecture documentation:
one per component/directory plus a root project-level `MEMORY.md`. There is **no separate
memory server**.

### Scope

- Root `MEMORY.md` - project-level facts, decisions, and cross-component relations.
- Per-component `MEMORY.md` - the component's architecture and persistent facts relevant to
  that directory (its context/scope).
- **Any directory/component may contain its own `MEMORY.md`**; the file describes the context
  of the directory where it is located. Root component files live in `include/<component>/`;
  implementation-detail files live in `include/<component>/<internal-dir>/` (internal directories
  have no own `CMakeLists.txt` and are not components); `test/` has its own.

### Rules (summary)

- Authoritative format - mandatory header metadata (`scope`, `role`, `last_reviewed`,

`review_period`, `max_size`) and mandatory sections (`## Architecture`, `## Facts and
invariants`, `## Decisions`, `## Relations`), as well as full rules for size limits and periodic
checks - are defined in `.clinerules/README.md`.

- Detailed criteria for what can/should be stored in `MEMORY.md` - see `.clinerules/TASK.md`

("Component Knowledge Check").

- Key principles: store only brief facts that are **difficult to determine** from code/API/documentation;
no timestamps, no error/task statuses, no plans (these should be in `.tasklog/`); the file must adhere to the `max_size` limit; Before adding, check for duplicates and, if necessary, shorten the information in the file, keeping the most important information in a more compact form; update the `last_reviewed` value after each review;