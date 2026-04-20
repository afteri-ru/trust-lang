## 1. Understand Before Coding

**Think through and understand the task before writing code. Push back when needed.**

- State assumptions explicitly. Present alternatives. Never guess -- ask.
- Transform tasks into verifiable success criteria before writing code.
- For multi-step tasks, state a brief plan with verification points.

## 2. Clearly Plan Large Tasks

**Document step-by-step plans for large tasks or complex refactorings.**

- Create a TASKS.md file before starting complex work.
- Track the successful completion of each step in the plan file.
- Refer to plan items when presenting results.
- After confirming completion of all planned tasks, delete the plan file with a brief description of the results achieved.

## 3. Simplicity First

**Minimum code for the problem. Nothing speculative.**

- No features beyond what was asked. No unrequested abstractions or "flexibility".
- No error handling for impossible scenarios.
- If you write 200 lines and it could be 50, rewrite.

## 4. Surgical Changes

**Touch only what the task requires. Clean up your own orphans.**

- Match existing style. Do not "improve" adjacent code or formatting.
- Remove imports/variables/functions YOUR changes made unused.
- Do not refactor unrelated code. Mention dead code you find -- don't delete it.

## 5. Eliminate Duplication

**Reuse. One source of truth per concern. Prefer compile-time checks.**

- Check for existing implementations before writing new code.
- Avoid duplicating logic or checks.
- Prefer static_assert and type system over runtime checks.

## 6. No Implicit Backward Compatibility

**Don't preserve history unless asked.**

- Never maintain backward compatibility unless explicitly required.
- Remove old code without deprecation layers or comments explaining removal.
- Breaking changes are default unless compatibility is specified.

## 7. Tests Are Non-Negotiable

**Every change needs test coverage. Never disable or skip tests.**

- Write tests for additions and modifications: normal paths, edge cases, errors.
- Never remove, skip, or disable tests without explicit permission.
- Fix underlying code on failure -- do not silence the test.
- Tests MUST never be silently skipped — missing test infrastructure (GTest, lit, python3, etc.) is a BUILD FAILURE, not a silent skip.
- Do not delete generated/output files unless asked.

## 8. Analyze Before Coding

**Read README first. Open only what you need. Respect module boundaries.**

- Start with root README for overview. Read module README before touching its code.
- Implementation files: open only when directly working on them.
- Root README: modules and dependencies only, no specific file paths.
- Module README: its own architecture and conventions belong here.
- Build includes: `target_include_directories` points to `include/`. Use namespaced paths (`"diag/location.hpp"`) to prevent file name conflicts.

---

**These guidelines are working if:** fewer unnecessary changes in diffs, fewer rewrites due to overcomplication, and clarifying questions come before implementation.