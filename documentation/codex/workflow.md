# Codex Workflow

## Work categories

Every task must be treated as one of these categories:

1. Orientation
   - understand code,
   - no code changes.

2. Decision note
   - compare options,
   - no code changes.

3. Implementation plan
   - exact files and functions,
   - no code changes.

4. Implementation tranche
   - small, approved, bounded.

5. Review
   - check diff against contracts,
   - no new changes unless separately approved.

## Required preflight

Before implementation, report:

- protected architecture touched: yes/no,
- files/functions affected,
- existing functions reused,
- new functions/types proposed,
- abstractions deliberately avoided,
- expected semantic impact,
- build and regression plan.

If protected architecture is touched incidentally, stop.

## Implementation discipline

Implementation must match the approved plan exactly.

Do not perform:

- opportunistic cleanup,
- nearby warning fixes,
- unrelated renames,
- drive-by formatting,
- architecture preparation,
- “while here” changes.

If further work becomes visible, report it separately.

## Review discipline

After implementation:

- provide `git diff --stat`,
- summarize files changed,
- explain why semantics did not change if the tranche is refactoring,
- run `make clean all CXX=clang++`,
- report warnings.

A tranche is not complete until the build result is known.

## Rejection formula

If a proposed change violates the contracts, respond with:

```text
Zurückweisen.

Verstoß gegen:
- affected contract,
- concrete rule.

Neu planen.
Kein Code.
Minimaler Eingriff.
Keine neuen Helper/Typen ohne Einzelgenehmigung.
```

## Commit discipline

Keep commits narrow:

- build fixes separate from refactorings,
- warnings separate from feature work,
- architecture comments separate from behavior changes,
- protected-architecture changes separate from unrelated work.
