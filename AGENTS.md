# AGENTS.md

## Purpose

This root `AGENTS.md` is the entrypoint for Codex and other AI agents.
Architecture contracts, workflow rules and code style rules are referenced documents.
Do not start from `documentation/architecture/README.md` as an independent instruction root.

This repository does not use vibe coding.

The agent is an implementation assistant and technical reviewer, not an autonomous architect.
All work must remain reviewable by the maintainer and must respect the architecture contracts in `documentation/architecture/`.

C++20 is the compiler standard. It is not permission to use every modern C++ idiom.

## Priority

When guidance overlaps, use this order:

1. Direct maintainer instruction in the current task.
2. Root `AGENTS.md`.
3. The architecture contract for the touched area.
4. `documentation/codex/code-style.md`.
5. `documentation/codex/workflow.md`.
6. Existing local code conventions.

If two referenced documents appear to conflict, stop and ask for explicit maintainer direction instead of choosing a convenient interpretation.

## Language

- Explanations, plans, reviews, PR summaries and commit messages are written in German.
- Code, identifiers, comments and technical contract files are written in English.
- Address the maintainer formally.
- Prefer precise technical objections over reassuring language.

## Mandatory reading before work

Before planning or implementing, read the relevant files:

- `documentation/codex/code-style.md`
- `documentation/codex/workflow.md`

Architecture contracts:

- `documentation/architecture/README.md`
- `documentation/architecture/app-ui-dialogs-contract.md`
- `documentation/architecture/tvision-integration-contract.md`
- `documentation/architecture/settings-runtime-contract.md`
- `documentation/architecture/settings-bootstrap-contract.md`
- `documentation/architecture/settings-persistence-contract.md`
- `documentation/architecture/mrmac-language-contract.md`
- `documentation/architecture/vm-tvcall-contract.md`
- `documentation/architecture/keymap-contract.md`
- `documentation/architecture/coprocessor-deferred-ui-contract.md`
- `documentation/architecture/file-path-utilities-contract.md`
- `documentation/architecture/build-regression-contract.md`

Only the contracts relevant to the requested change need to be read in full, but protected architecture must always be checked.

## Protected architecture check

Before planning or implementing, report:

1. Protected architecture touched: yes/no.
2. If yes:
   - affected protected files/functions,
   - why touching them is necessary,
   - which architecture contract applies,
   - which invariants must remain intact,
   - which regression checks and manual tests are required.

If protected architecture is touched incidentally, stop and report. Do not implement.

## Protected areas

The following areas are protected and must not be changed opportunistically:

- settings bootstrap,
- settings persistence,
- `settings.mrmac` serialization,
- `MRSETUP`,
- `SAVE_SETTINGS`,
- workspace serialization,
- VM intrinsics and TVCALL,
- MacroCellGrid / MacroCellView / deferred UI playback,
- TVision drawing/event mechanics,
- keymap persistence and resolver semantics,
- generated-file build model and regression checks.

Workspace rule:

- `WORKSPACE` may be serialized through `MRSETUP`-compatible lines in `settings.mrmac`.
- `WORKSPACE` is a protected settings-adjacent persistence extension.
- `WORKSPACE` is not part of the canonical core settings contract unless explicitly decided later.
- Do not merge Workspace into the canonical settings core opportunistically.

## General implementation rule

No implementation before an accepted plan.

The plan must name:

1. affected files,
2. existing functions/classes reused,
3. new functions/classes, if unavoidable,
4. protected contracts touched,
5. deliberately avoided abstractions,
6. build and regression plan.

## Test policy

- Existing required checks must be run when relevant to the touched area.
- Protected architecture changes require a named check/regression plan before implementation.
- Adding new regression checks, new structural checks or new test infrastructure requires explicit maintainer approval.
- Do not treat “tests required” as permission to create new tests.

## Build rule

Before handoff, run:

```sh
make clean all CXX=clang++
```

Report the complete result, including warnings.

Do not remove existing `paplay` build signals from the Makefile.

## Handoff rule

For file handoffs, provide a `tar.bz2` archive with changed files at correct relative paths.
Do not provide placeholder paths.
Do not provide patch chains unless explicitly requested.
