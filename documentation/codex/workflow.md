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

Before implementation, complete the protected-architecture check in root
[`AGENTS.md`](../../AGENTS.md). Every implementation plan must name:

- files/functions affected,
- existing functions reused,
- new functions/types proposed,
- protected contracts touched,
- abstractions deliberately avoided,
- expected semantic impact,
- expected churn quantity,
- regression risk estimate,
- build and manual test plan.

If protected architecture or scope expansion is discovered incidentally, stop.

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
- apply the root build rule and report warnings,
- perform a sight review of the changed code,
- propose regression checks only after that review, if they still look useful.

A tranche is not complete until the native CachyOS build result is known and
the current native executable and required runtime assets are available in
the working repository for maintainer review. A Debian 12 compatibility build
is required only for an explicitly requested repository publication/update,
including a Repo Run; it does not replace native validation.

Bug fixes require a same-family impact audit before handoff. Treat a defect as
evidence of a potentially affected class, not as an isolated symptom. Check the
inverse path, sibling values or operations, alternate entry points and
lifecycle transitions. Regression timing and scope follow the root test policy.

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

## Publication discipline

This is a single-maintainer repository. It has no pull-request review or
approval workflow.

- Do not create pull requests, draft pull requests or `agent/*` publication
  branches.
- An explicit maintainer instruction to commit, push or release means direct
  publication to `main`.
- Use a feature branch only when the maintainer explicitly requests it.
- Apply the English-only publication rule in root `AGENTS.md` to every
  publication surface: commits, tag annotations, release titles and bodies,
  release notes, changelogs, documentation and update-manifest notes.
- Review publication text for English before committing, tagging, packaging,
  signing or publishing it. German conversation text is not publication copy.

### Repo Run

The canonical definition and authorization scope of **Repo Run** are in root
[`AGENTS.md`](../../AGENTS.md#repo-run).

Execute the requested sequence completely: bump the version, review and commit
every local dirty repository change with a structured English change list, and
push directly to `main` using English publication text. Build
the Debian 12 compatibility package from that exact pushed commit, and update
the corresponding GitHub release and signed update artifacts.

Use the native CachyOS build for local maintainer review before publication.
Keep the Debian build and packaging in a separate system-temporary source
snapshot. The snapshot must contain exactly the published source state.
Publish artifacts only after a successful compatibility build and artifact
verification. Report a failed publication step precisely; do not claim that
the Repo Run completed when required work remains.
