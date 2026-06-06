# Autonomous Refactoring Plan

## Mission

Each session: find the **single most critical issue** in the codebase,
fix it, validate, and create a PR. Repeat across sessions until the
codebase reaches acceptable quality.

---

## Phase 1: Discovery

Scan the codebase and build a scored list of issues.
Use all available static analysis tools and compiler output.
Do not assume tools — check what is available in the project first.

### What to look for

**Correctness & Safety** (weight: 3.0)
- Memory leaks, dangling pointers, buffer overflows
- Data races, missing synchronization
- Undefined behaviour, uninitialized variables
- Unchecked return values, missing error handling
- Resource management without RAII

**Technical Debt** (weight: 2.0)
- Functions with cyclomatic complexity >15
- Copy-pasted logic (>10 lines, >2 occurrences)
- Functions >150 lines, classes >1000 lines
- Magic numbers, dead code, unused includes

**Performance** (weight: 1.5)
- Only flag with evidence: profiler output, benchmark data,
  or obvious algorithmic issues (e.g. O(n²) in hot path)
- Otherwise defer

**Modernization** (weight: 1.0)
- Raw `new`/`delete` where smart pointers apply
- C-style casts, manual resource management
- Only address after Correctness and Debt are under control

### Scoring

Score each issue 1–10:
- **9–10**: Confirmed bug or vulnerability
- **7–8**: High-probability defect, unsafe pattern in critical path
- **4–6**: Significant debt or measurable inefficiency
- **1–3**: Low-risk cosmetic or theoretical issue

**Priority = Score × Weight / Effort**

Effort: Low = 1.0 · Medium = 1.5 · High = 2.0

### Selection rule

1. Pick the issue with the highest Priority score
2. On tie: prefer lower effort
3. Always prefer Correctness issues over equal-priority Debt issues

Record the full ranked list in `session.json` under `discovery`.
This allows the next session to continue from where this one stopped.

---

## Phase 2: Refactoring

### Scope

Target exactly **one issue** from Phase 1.
Define before starting:
- Which files and functions are in scope
- Which interfaces must remain stable
- What the measurable end state looks like

### Constraints

| Parameter     | Limit       |
|---------------|-------------|
| Diff size     | 100–500 lines |
| Files touched | 5–15        |
| Execution time | 45 minutes  |
| Fix attempts  | max 3 per commit |

### Process

1. Read all relevant files before writing anything
2. Break work into 3–7 atomic commits
3. After each commit: run tests + linter
4. If verification fails: fix and retry (max 3 attempts)
5. If still failing after 3 attempts: revert and try a different approach
6. If diff approaches 500 lines: stop and finalize current state

---

## Phase 3: Validation

Before marking the session complete, verify:

- [ ] All existing tests pass
- [ ] Linter passes with zero errors
- [ ] Run sanitizers if available (ASan, UBSan, TSan)
- [ ] Target metric improved (complexity reduced / duplication removed / etc.)
- [ ] No unintended behaviour changes

---

## Phase 4: Session Continuity

After each session, update `session.json`:

```json
"discovery": {
  "issues_ranked": [...],       // full ranked list from Phase 1
  "completed": ["CRIT-001"],    // issues fixed in previous sessions
  "deferred": ["PERF-003"],     // issues requiring human decision
  "selected_this_session": "CRIT-002"
}
```

The next session re-runs Phase 1 but uses the existing ranked list
as a baseline — only re-scanning areas affected by the previous PR.
This avoids full re-analysis every time.

**Stopping criteria:**
- No Correctness issues with score ≥ 8 remain
- No Technical Debt issues with Priority > 10 remain
- Remaining issues are flagged as `deferred` with reasoning

---

## Failure Recovery

| Situation | Action |
|-----------|--------|
| Phase 1 finds nothing above threshold | Lower threshold by 1, rescan |
| Phase 2 fails after 3 attempts | Reduce scope, try again; if still failing — defer and move to next issue |
| Validation fails repeatedly | Revert all commits, mark issue as `deferred`, explain why in session.json |
| Diff exceeds 500 lines | Split: finalize current partial fix as PR, remainder becomes next session's target |

---

## Integration Notes

This plan is consumed by **ManagerAgent**.
ManagerAgent delegates execution to **RefactorAgent** (Phase 2)
and **ReviewAgent** (Phase 3 review pass).

Results flow through `.openhands/`:
- `refactor_result.json` — RefactorAgent output
- `review_result.json` — ReviewAgent output
- `session.json` — persistent state across all phases and sessions

ManagerAgent makes all prioritization and deferral decisions.
RefactorAgent and ReviewAgent have no access to session.json directly.
