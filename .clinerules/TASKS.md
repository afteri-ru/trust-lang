# Rule of pla execution

## Wite plan in `TASK.md` file before execution.**

- All stages of the current task must be saved in `TASK.md` before starting execution.
- If `TASK.md` already exists, do not execute tasks not listed or related to the current plan. Propose updating the plan, or let the user delete `TASK.md` manually.

## Record all actions and results to `TASK.md`.**

- Record the results of each step of the plan in the `TASK.md` file, including any negative results.

- After **several unsuccessful** attempts to solve a problem, **stop, analyze the problems**, and propose several solutions.

- If the problem persists and requires a change in the architecture or the solution is outside the scope of the approved plan, stop execution and provide a report on the problem encountered with several proposed solutions to choose from.

## Structure your prompt**

**Structure your prompt for efficient LLM context caching.**

- Place static content (`AGENTS.md`, `ARCH.md`) first — they form a cacheable prefix.
- Place dynamic content (`TASK.md`, current file, command results) after static content.
- When updating `TASK.md`, send it at the start of a new turn, not mid-conversation.
- In long sessions, periodically re-send static blocks (`AGENTS.md`, `ARCH.md`) to prevent cache eviction.

### Fragment-Only File Changes

**Modify files only by replacing or adding fragments. Never rewrite entire files.**

- Use `replace_in_file` for all edits to existing files.
- Use `write_to_file` only when creating a new file or when changes are so extensive that fragment replacement would be more error-prone.
- Never overwrite a file in full for a local patch.

## Before plan complete and stop execution

- Execute and check the "Quality Assurance" section after completing the plan .
- **Record the results** of the plan execution in the `TASK.md` file with a brief description of the results.
