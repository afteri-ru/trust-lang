---
disabled: true
---
# Rule of plan execution

## Record all actions to `.tasklog/<taskid>.md`

- **After switching to ACT mode, append the plan to the `.tasklog/<taskid>.md`.**
- Always append data to the `.tasklog/<taskid>.md` file. The file may be modified by other tools, so it **needs to be re-read** each time new data is added.
- Until the work plan is finally approved and switched to ACT mode, only reading of the data for analysis and plan development is permitted; editing or changing the data is prohibited.
- Always append the results **of each step of the plan tasks** in the `.tasklog/<taskid>.md` file, including any negative results.
- After **several unsuccessful** attempts to solve a problem, **stop and analyze the problems**.
- If the problem persists and requires a change in the architecture or the solution is outside the scope of the approved plan, **stop task execution** and provide a report on the problem encountered with several proposed solutions to choose from.

## Structure your prompt for efficient LLM context caching.

- Place static content (`AGENTS.md`, `README.md`, etc.) first - they form a cacheable prefix.
- Place dynamic content (`.tasklog/<taskid>.md`, current file, command results) after static content.
- When updating `.tasklog/<taskid>.md`, send it at the start of a new turn, not mid-conversation.
- In long sessions, periodically re-send static blocks (`AGENTS.md`, `README.md`, etc.) to prevent cache eviction.

## Before plan complete and stop task execution

- Re-Read `.tasklog/<taskid>.md` and review the original plan for completeness and append any items not completed and the reasons why they were not completed.
- Systematize the errors that occurred during the tasks plan and suggest improvements to prevent them in the future.
- **Append all results** of the plan execution in the `.tasklog/<taskid>.md` file with a brief description of the results.

---

## Read structures before refactoring

**Do not refactor code without reading the full definition of every type involved.**

- Before modifying or replacing a type - read the complete header file and understand **all** fields, their semantics, and relationships.
- Do not assume which fields are "unnecessary". If you don't understand a field - ask.
- Pay special attention to types with semantics - they may carry ownership or lifetime constraints.
- When a task says "remove dependency on X" - first check what data from X is actually used in downstream code.

## Verify before completion

**Do not call `attempt_completion` until all verifications pass.**

After ANY change that removes types/fields/tokens, run:
1. `grep -rn` for the removed/changed name across the entire project - fix all remaining references. 
2. Do NOT skip test files - tests must also use the new code.
3. Call `make build` - project must compile without errors.
   - Опции сборки — в корневом `CMakeLists.txt`; политика параллелизма/OOM — `cmake/parallelism.cmake`:
     `-j` вручную НЕ передаётся, число задач вычисляет `_build/trust-build.sh` (см. AGENTS.md п.14).
4. Run all tests with a timeout (never without): `timeout 900 make test`
   (эквивалент `timeout 900 cmake --build _build --target run_tests`; timeout обязателен,
   чтобы поймать зависание).
5. Фоновая сборка/тесты: `_build/trust-build.sh --background <pid> <log>`; ожидание — ОДИН
   вызов `_build/trust-build.sh --wait <pid> <log>`. Не опрашивать повторяющимися одинаковыми
   вызовами: агентский рантайм принимает их за зацикливание.
6. If the change adds/moves a `.cpp`/`.hpp` or changes `#include` directives - make sure every
   needed include is explicit. The shared project PCH was removed, so no PCH can mask a missing
   `#include` (only libtorch's `torch_pch` remains, and only under `WITH_TORCH=ON`).

If build or tests fail - do NOT declare completion. Fix the underlying issue.

## Scope boundaries

**The no-expansion rule applies to the AGREED scope only - never to the proposal.**

### Scope model

- **Negotiation (plan mode)** - scope is free-form: the agent proposes any volume, any
  composition and any number of stages. Volume is NOT limited by this rule.
- **Agreed scope** - the explicit list of stages/items approved by the user and recorded in
  `.tasklog/<taskid>.md` at the Plan→ACT transition. This list is the contract for the ACT phase.
- **Stage** - an independently verifiable unit of work with its own definition of done
  (build + tests). The agreed scope may be one stage or many stages.

### Rules

1. During negotiation the agent MUST present the full scope it considers correct, decomposed
   into a sequence of verifiable stages (each with DoD, risks and rejected alternatives). At
   this stage there is no "too large" scope - only an undivided one.
2. The user selects which stages form the agreed scope. Stages not selected are NOT executed in
   this task. Creating `.clinerules/backlog/` files to defer work ("later", "future", "not now")
   is FORBIDDEN without explicit user approval: ask the user first and create only the files the
   user approved.
3. After approval, adding an unplanned stage, item, file zone or behaviour is a scope
   expansion: stop, report the finding, and request explicit approval. Never continue silently.
4. Clarifications, fixing defects inside the agreed stage, and mechanical consequences of the
   stage (registering a new TU in CMake, `#include` updates, tests for the touched code) are
   NOT expansion.
5. If the agreed stage turns out to be impossible without changing the agreed architecture -
   stop and report 2-3 options (see "Rule of plan execution").
6. If plan discussion exceeds 5-8 iterations without convergence - do not drop the scope: split
   the proposal into independent stages and agree them one by one (earlier stages may start
   while later ones are still under discussion).
7. Do not mix unrelated goals inside one stage. Adding functionality together with an API
   refactor is allowed only when the approved plan explicitly states that the stage's DoD
   requires both; an unplanned combination is an expansion.
8. Do not rework caller APIs outside the approved scope. If an approved stage requires a caller
   API change, that change must be listed in the agreed scope.


## Component knowledge check

**Перед планированием задачи, затрагивающей компонент (ast, parser, diag, types, runtime и др.) - обязательно выполнить:**

1. Прочитать **`MEMORY.md` компонента** (корневой `MEMORY.md` и `include/<компонент>/MEMORY.md`) и учесть разделы «Architecture», «Facts and invariants», «Decisions», «Relations». Корневой файл компонента содержит **только публичный интерфейс и общие правила**.
2. Для затронутых файлов internal-подкаталога прочитать `include/<компонент>/<internal-dir>/MEMORY.md` — там детали реализации. Если файла нет, реализация мала (≤3 файлов и ≤16 KiB): искать трудно-выводимые инварианты в WHY-комментариях самих файлов.
3. Если информация касается затрагиваемой области - учесть её в плане до передачи пользователю.

**Что сохраняется в `MEMORY.md` (критерии включения):**
- Только **семантические инварианты-ловушки**, которые:
  - вызвали **≥3 циклов доработок** (reopened задачи);
  - **не выводятся однозначно** из чтения исходного кода или описания MEMORY.md;
  - не являются описанием полей структур (состав полей - читать исходный код).
- Сохранять в `MEMORY.md` следует **только очень кратко** самые важные семантические инварианты-ловушки, которые сложно вывести из API/MEMORY.md/исходного кода.

**Где хранить и сколько:**
- Корень компонента — только публичный интерфейс и общие правила; детали реализации — в `MEMORY.md` соответствующей зоны.
- `MEMORY.md` не создаётся, если уникальной информации меньше 1000 байт: перенести её в WHY-комментарии кода или `README.md` каталога.
- Для малой реализации (≤3 файлов и ≤16 KiB) файл памяти необязателен: инварианты живут в комментариях самих файлов.

**Что ЗАПРЕЩЕНО сохранять в `MEMORY.md`:**
- **запрещено сохранять любую информацию о багах или задачах** (в т.ч. статус/итоги выполнения, найденные проблемы, планы, «отдельные задачи», списки оставшихся работ) - всё это должно быть в `.tasklog`;
- информация о новой функциональности, которая уже выбрана пользователем и/или зафиксирована в коде или архитектуре;
- объяснения архитектуры пользователем на стадии планирования (они уже в `.tasklog`);
- состав и описание полей данных - для этого есть исходный код.

**Порядок пополнения `MEMORY.md`:**

- При каждом случае "ложного завершения" (≥3 TaskComplete) - если причина не покрыта существующими наблюдениями и не выводится из исходного кода - добавить наблюдение в `MEMORY.md` соответствующего компонента и обновить `last_reviewed` при ревизии.
- Если в ходе обычной задачи найден новый семантический инвариант-ловушка, отсутствующий в `MEMORY.md`, - дописать его в раздел «Facts and invariants» соответствующего компонента и обновить `last_reviewed`.
- Если `last_reviewed` в `MEMORY.md` затрагиваемого компонента старше `review_period` - предложить провести ревизию (сверить содержимое с кодом, обновить `last_reviewed`) как отдельный пункт плана.

---
