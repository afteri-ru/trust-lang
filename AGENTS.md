# Agent Guidelines

## Planning

### 1. Planning tasks

**Think through and understand the task before writing code. Step back when necessary.**

- State assumptions clearly. Present alternatives. Never guess — ask.
- Convert tasks into verifiable success criteria before writing code.
- For multi-step tasks, outline a brief plan with checkpoints.

---

## Rules for coding

### 3. Simplicity First

**Minimum code for the problem. Nothing speculative.**

- No features beyond what was asked. No unrequested abstractions or "flexibility".
- No error handling for impossible scenarios.
- If you write 200-500 lines and it could be 50, rewrite.

### 4. Surgical Changes

**Touch only what the task requires. Clean up your own orphans.**

- Match existing style. Do not "improve" adjacent code or formatting.
- Remove imports/variables/functions YOUR changes made unused.
- Do not refactor unrelated code. Mention dead code you find -- don't delete it.

### 5. Eliminate Duplication

**Reuse. One source of truth per concern. Prefer compile-time checks.**

- Check for existing implementations before writing new code.
- Avoid duplicating logic or checks.
- Prefer `static_assert` and type system over runtime checks.

### 6. No Implicit Backward Compatibility

**Don't preserve history unless asked.**

- Never maintain backward compatibility unless explicitly required.
- Remove old code without deprecation layers or comments explaining removal.
- Breaking changes are default unless compatibility is specified.

---

## Quality Assurance

### 7. Tests Are Non-Negotiable

**Every change needs test coverage. Never disable or skip tests.**

- Write tests for additions and modifications: normal paths, edge cases, errors.
- Never remove, skip, or disable tests without explicit permission.
- Fix underlying code on failure -- do not silence the test.
- Tests **MUST** never be silently skipped — missing test infrastructure (GTest, lit, python3, etc.) is a **BUILD FAILURE, not a silent skip or GTEST_SKIP()**.
- Do not delete generated/output files unless asked.

### 8. Architecture From `ARCH.md` Only

**Do not read header or source files to analyze architecture.**

- The `ARCH.md` file in each component's directory is the sole source of architectural information.
- Read individual source/header files only when the task explicitly requires it or when modifying that specific file.
- Do not scan the project for "understanding" — read `ARCH.md` or `README.md` first.

### 9. Keep file `ARCH.md` Synchronized

**`ARCH.md` must always reflect the actual implementation.**

- `ARCH.md` must always reflect the actual implementation:
- If a code change reveals a discrepancy between `ARCH.md` and the code — stop and report it.
- Update `ARCH.md` in the same change set as the code modification.
- Mismatch between `ARCH.md` and implementation is treated as a bug.

---

**These guidelines are working if:** fewer unnecessary changes in diffs, fewer rewrites due to overcomplication, and clarifying questions come before implementation.