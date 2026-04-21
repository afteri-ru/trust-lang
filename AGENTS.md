# Agent Guidelines

## Before Coding

### 1. Understand the Task Before Writing Code

**Think through and understand the task before writing code. Step back when necessary.**

- State assumptions clearly. Present alternatives. Never guess — ask.
- Convert tasks into verifiable success criteria before writing code.
- For multi-step tasks, outline a brief plan with checkpoints.

### 2. Plan and Document Task Execution Clearly

**Document and track task execution in a work plan.**

- Work on only one task from the plan at a time.
- All stages of the current task must be saved in `TASK.md` before starting execution.
- If `TASK.md` already exists, follow exactly the tasks and sequence specified in it.
- Do not execute tasks not listed or related to the current plan. Propose updating the plan, or let the user delete `TASK.md` manually.
- Record the result of each step, including negative results.
- After multiple failed attempts to solve a problem — stop, analyze the issues, and propose several solutions.
- Upon completing the current task, move the file to `.TASKS` directory, renaming it using the pattern `TASK-%Y-%m-%d_%H%M.md` with a brief summary of results.

---

## While Coding

### 4. Simplicity First

**Minimum code for the problem. Nothing speculative.**

- No features beyond what was asked. No unrequested abstractions or "flexibility".
- No error handling for impossible scenarios.
- If you write 200 lines and it could be 50, rewrite.

### 5. Surgical Changes

**Touch only what the task requires. Clean up your own orphans.**

- Match existing style. Do not "improve" adjacent code or formatting.
- Remove imports/variables/functions YOUR changes made unused.
- Do not refactor unrelated code. Mention dead code you find -- don't delete it.

### 6. Eliminate Duplication

**Reuse. One source of truth per concern. Prefer compile-time checks.**

- Check for existing implementations before writing new code.
- Avoid duplicating logic or checks.
- Prefer `static_assert` and type system over runtime checks.

### 7. No Implicit Backward Compatibility

**Don't preserve history unless asked.**

- Never maintain backward compatibility unless explicitly required.
- Remove old code without deprecation layers or comments explaining removal.
- Breaking changes are default unless compatibility is specified.

---

## Quality Assurance

### 8. Tests Are Non-Negotiable

**Every change needs test coverage. Never disable or skip tests.**

- Write tests for additions and modifications: normal paths, edge cases, errors.
- Never remove, skip, or disable tests without explicit permission.
- Fix underlying code on failure -- do not silence the test.
- Tests MUST never be silently skipped — missing test infrastructure (GTest, lit, python3, etc.) is a BUILD FAILURE, not a silent skip.
- Do not delete generated/output files unless asked.

---

**These guidelines are working if:** fewer unnecessary changes in diffs, fewer rewrites due to overcomplication, and clarifying questions come before implementation.