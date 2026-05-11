# Agent Guidelines

## Planning

### 1. Planning tasks

**Think through and understand the task before writing code. Step back when necessary.**

- State assumptions clearly. Present alternatives. Never guess — ask.
- Convert tasks into verifiable success criteria before writing code.
- For multi-step tasks, outline a brief plan with checkpoints.

---

## Rules for coding

### 2. Simplicity First

**Minimum code for the problem. Nothing speculative.**

- No features beyond what was asked. No unrequested abstractions or "flexibility".
- No error handling for impossible scenarios.
- If you write 200-500 lines and it could be 50, rewrite.

### 3. Surgical Changes

**Touch only what the task requires. Clean up your own orphans.**

- Match existing style. Do not "improve" adjacent code or formatting.
- Remove imports/variables/functions YOUR changes made unused.
- Do not refactor unrelated code. Mention dead code you find -- don't delete it.

### 4. Eliminate Duplication

**Reuse. One source of truth per concern. Prefer compile-time checks.**

- Check for existing implementations before writing new code.
- Avoid duplicating logic or checks.
- Prefer `static_assert` and type system over runtime checks.

### 5. No fallback for invalid data — raise an error (use FAULT and EXPECT with a diagnostic)

**Invalid input → FAULT or EXPECT, never use a silent fallback.**

- Any function that receives invalid data **must** call FAULT() or EXPECT(), not return {}, nulllopt, or any silent default result.

- When changing the API contract, update your tests to reflect the new contract. Fallback code cannot be added to satisfy old test requirements.

- An empty value for std::optional is only allowed when the value is missing. It is invalid when invalid input data is passed (an error/FAULT/EXPECT must be returned).

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

---

## Quality Assurance

### 8. Follow CODESTYLE

**All code must follow [CODESTYLE.md](CODESTYLE.md) — naming conventions, formatting, prohibited and required patterns.**

- The naming conventions, formatting, prohibited and required patterns, code rules must follow [CODESTYLE.md](CODESTYLE.md)
- For compiler options and build targent in `CMakeLists.txt`.
- Run `clang-format` before committing.
- Run `clang-tidy` and fix all warnings.

### 9. Tests Are Non-Negotiable

**Every change needs test coverage. Never disable or skip tests.**

- Write tests for additions and modifications: normal paths, edge cases, errors.
- Never remove, skip, or disable tests without explicit permission.
- Fix underlying code on failure -- do not silence the test.
- Tests **MUST** never be silently skipped — missing test infrastructure (GTest, lit, python3, etc.) is a **BUILD FAILURE, not a silent skip or GTEST_SKIP()**.
- Do not delete generated/output files unless asked.

### 10. Architecture From `ARCH.md` Only

**Do not read header or source files to analyze architecture.**

- The `ARCH.md` file in each component's directory is the sole source of architectural information.
- Read individual source/header files only when the task explicitly requires it or when modifying that specific file.
- Do not scan the project for "understanding" — read `ARCH.md` or `README.md` first.

### 11. Keep file `ARCH.md` Synchronized

**`ARCH.md` must always reflect the actual implementation.**

- `ARCH.md` must always reflect the actual implementation:
- If a code change reveals a discrepancy between `ARCH.md` and the code — stop and report it.
- Update `ARCH.md` in the same change set as the code modification.
- Mismatch between `ARCH.md` and implementation is treated as a bug.

---

**These guidelines are working if:** fewer unnecessary changes in diffs, fewer rewrites due to overcomplication, and clarifying questions come before implementation.