# Rule of plan execution

## After switching to ACT mode, append the plan to the `.tasklog/<taskid>.md` file

- Always add first step to any plan **After switching to ACT mode, appent the plan to the `.tasklog/<taskid>.md`.**
- Until the work plan is finally approved and switched to ACT mode, only reading of the data for analysis and plan development is permitted; editing or changing the data is prohibited.
- Always append data to the `.tasklog/<taskid>.md` file. The file may be modified by other tools, so it **needs to be re-read** each time new data is added.

## Record all actions to `.tasklog/<taskid>.md`

- **After switching to ACT mode, append the plan to the `.tasklog/<taskid>.md`.**
- Always append the results **of each step of the plan tasks** in the `.tasklog/<taskid>.md` file, including any negative results.
- After **several unsuccessful** attempts to solve a problem, **stop and analyze the problems**.
- If the problem persists and requires a change in the architecture or the solution is outside the scope of the approved plan, **stop task execution** and provide a report on the problem encountered with several proposed solutions to choose from.

## Structure your prompt for efficient LLM context caching.

- Place static content (`AGENTS.md`, `ARCH.md`, etc.) first — they form a cacheable prefix.
- Place dynamic content (`.tasklog/<taskid>.md`, current file, command results) after static content.
- When updating `.tasklog/<taskid>.md`, send it at the start of a new turn, not mid-conversation.
- In long sessions, periodically re-send static blocks (`AGENTS.md`, `ARCH.md`, etc.) to prevent cache eviction.

## Before plan complete and stop task execution

- Re-Read `.tasklog/<taskid>.md` and review the original plan for completeness and append any items not completed and the reasons why they were not completed.
- Systematize the errors that occurred during the tasks plan and suggest improvements to prevent them in the future.
- **Append all results** of the plan execution in the `.tasklog/<taskid>.md` file with a brief description of the results.

---

## Read structures before refactoring

**Do not refactor code without reading the full definition of every type involved.**

- Before modifying or replacing a type — read the complete header file and understand **all** fields, their semantics, and relationships.
- Do not assume which fields are "unnecessary". If you don't understand a field — ask.
- Pay special attention to types with semantics — they may carry ownership or lifetime constraints.
- When a task says "remove dependency on X" — first check what data from X is actually used in downstream code.

## Verify before completion

**Do not call `attempt_completion` until all verifications pass.**

After ANY change that removes types/fields/tokens, run:
1. `find` `grep <removed_name>` over the entire project — fix all remaining references.
2. `cmake --build _build` — project must compile without errors.
3. `make run_unit_tests` or `make run_tests` for all tests — must pass.

If the change affects — add a roundtrip test (save → load → compare) before completing.

Do not call `attempt_completion` if you haven't run build + tests. "Trusting" that unrelated files don't reference the removed code is not sufficient — verify.

If build or tests fail — do NOT declare completion. Fix the underlying issue.

## Scope boundaries

**Do not expand the task without explicit agreement.**

- If architectural plan discussion exceeds 5 iterations — split into subtasks and agree separately.
- Do not mix adding functionality with API refactoring in a single task.
- Do not rework caller APIs if the task does not require it.
- If a simple task ("add an attribute") grows into an API redesign ("change substituteArgs to class") — stop and request a separate task.

## Premature completion

**Do NOT declare a task completed if sections "Verify before completion" and "Scope boundaries" have not been fulfilled.**

Characteristic signs of premature completion:
- Only 1-2 files changed, although the task involves conceptually related components.
- No `find` `grep` check for remaining references.
- No build check (`cmake --build _build`).
- No test run (`make run_unit_tests` or `make run_tests` for all tests).

If after declaring TaskComplete the user points out unfinished work — it means sections 2-3 were not fulfilled.
