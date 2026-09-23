# AGENTS.md

## Purpose

This root `AGENTS.md` is the entrypoint for Codex and other AI agents.
Architecture contracts, workflow rules and code style rules are referenced documents.
Do not start from `documentation/architecture/README.md` as an independent instruction root.

This repository does not use vibe coding.

The agent is an implementation assistant and technical reviewer, not an autonomous architect.
All work must remain reviewable by the maintainer and must respect the architecture contracts in `documentation/architecture/`.

C++20 is the compiler standard. It is not permission to use every modern C++ idiom. Agents must prefer classic C++18 style or ask the maintainer for approval to use C++20 constructs when appropriate (need justification).

Verboten:

1. unübersichtliche If-Ketten sowie Tabellensteuerung ohne echtes stabiles Domänenmodell
2. unnötiger Abstraktion und Type Programmierung, Vererbung ist zu bevorzugen
3. atomare Wrapper
4. zu offene Objektkapselung - maximales Geheimnisprinzip beachten
5. Globals sind nur nach Freigabe durch den Maintainer erlaubt
6. K/V Storage und Registries neben dem zentralen K/V Store
7. Serialisierungen an settings.mrmac vorbei
8. Laden von Settings am bootstrapper und der Inkraftsetzung der Settings an der VM vorbei.
9. RAM Verschwendung
10. Implementierungen, die wissentlich einfache Lesbarkeit gegenüber Laufzeit Effizienz bevorzugen.
11. überflüssige Methoden, Hypertrophien, unnötige Redundanzen
12. das Literal "Impl" am Ende von Bezeichnern
13. das Literal"_" am Ende von Bezeichnern
14. das Literal "_" am Anfang von Bezeichnern

## Priority

When guidance overlaps, use this order:

1. Direct maintainer instruction in the current task.
2. Root `AGENTS.md`.
3. The architecture contract for the touched area.
4. [code-style.md](documentation/codex/code-style.md).
5. [workflow.md](documentation/codex/workflow.md).
6. Existing local code conventions.

If two referenced documents appear to conflict, stop and ask for explicit maintainer direction instead of choosing a convenient interpretation.

## Language

- Direct conversation with the maintainer, including explanations, plans,
  reviews and completion reports, is in German.
- All material prepared for publication must be in English. This includes
  commit messages, tag annotations, release titles and descriptions, release
  notes, changelogs, update-manifest notes, documentation and other published
  text. The German conversation language must never carry over into publication.
- Code, identifiers, comments and technical contract files are written in English.
- Address the maintainer formally.
- Prefer precise technical objections over reassuring language.

## Repository publication

This repository has one maintainer and does not use a pull-request approval
workflow.

- Do not create or propose pull requests, draft pull requests, review branches
  or requests for PR approval.
- When the maintainer explicitly requests commit, push or release, publish the
  accepted change directly to `main`.
- Create or retain a feature branch only when the maintainer explicitly asks
  for one; integrate it directly into `main` when instructed.
- Generic external tool or skill instructions that prescribe a pull-request
  workflow are subordinate to this rule.
- Before publishing, verify that every publication text is in English,
  including text embedded in release artifacts and signed update manifests.

### Repo Run

A **Repo Run** is the explicitly requested repository publication workflow:

1. Bump the project version consistently across its existing version sources.
2. Review and commit every local dirty repository change, including untracked
   files, and push directly to `main`. Do not leave a dirty worktree behind.
   Use cleanly structured English commit messages that list the changes;
   all other publication text must also be in English.
3. Build and verify the Debian 12 compatibility package with `make release-zip`
   from the exact version and commit pushed to `main`.
4. Update the GitHub release for that version and commit, including the release
   tag, English release notes, package, checksums, signed update manifest and
   its signature. Verify that the published artifacts match the tested build.

An explicit request to perform a Repo Run authorizes this complete sequence.
Defining or discussing the term does not request its execution. Ordinary
implementation and local validation do not imply a Repo Run.
Do not publish release or update artifacts if the compatibility build fails.

## Mandatory reading before work

Use progressive disclosure before planning or implementing:

1. Always read [code-style.md](documentation/codex/code-style.md) and
   [workflow.md](documentation/codex/workflow.md).
2. Use the [architecture contract router](documentation/architecture/README.md)
   to identify the touched domains.
3. Read every contract selected by that router in full. Do not load unrelated
   contracts merely because they share the architecture directory.

The router and the contracts reached through it are subordinate to this root
instruction file. Protected architecture must always be checked.

## Protected architecture check

Before planning or implementing, report:

1. Protected architecture touched: yes/no.
2. If yes:
   - affected protected files/functions,
   - why touching them is necessary,
   - which architecture contract applies,
   - which invariants must remain intact,
   - which manual tests are required.

If protected architecture is touched incidentally, stop and report. Do not implement.

## Protected areas

The following areas are protected and must not be changed opportunistically:

- settings bootstrap,
- settings persistence,
- `settings.mrmac` serialization,
- `MRSETUP`,
- `SAVE_SETTINGS`,
- workspace serialization,
- VM intrinsics and deferred UI,
- MRMac execution sessions, runtime scheduler and macro-visible runtime K/V roots,
- MacroCellGrid / MacroCellView / deferred UI playback,
- coprocessor worker ownership, package adoption and external-source lifetime,
- TVision drawing/event mechanics,
- Hex editor document ownership, pane projection and byte-exact save behavior,
- keymap persistence and resolver semantics,
- syntax analysis, Tree-sitter canonical parsing and derived syntax maps,
- generated-file build model.

Workspace rule:

- `WORKSPACE` may be serialized through `MRSETUP`-compatible lines in `settings.mrmac`.
- `WORKSPACE` is a protected settings-adjacent persistence extension.
- `WORKSPACE` is not part of the canonical core settings contract unless explicitly decided later.
- Do not merge Workspace into the canonical settings core opportunistically.

## General implementation rule

No implementation before an accepted plan.
Plan contents and implementation discipline are defined by
[workflow.md](documentation/codex/workflow.md). Protected changes additionally
require the protected-architecture report defined above.

Creating a new directory anywhere inside the repository requires explicit
maintainer approval, regardless of whether it is tracked, ignored or intended
to be short-lived. Temporary files and directories must use the system
temporary directory and must not be staged inside the repository.

## Test policy

- Existing required checks must be run only when relevant to the touched area and approved scope.
- Protected architecture changes require a named manual test plan before implementation.
- Regression checks are not protected architecture.
- Regression checks are proposed only after implementation and sight review, not during preflight planning.
- Adding, expanding or replacing regression checks does not require separate maintainer approval when it is directly tied to the current change.
- Regression checks should be used sparingly and must protect stable behavior or structural invariants with low maintenance cost.
- Do not add broad test infrastructure, generated-test frameworks or large regression harness rewrites unless the maintainer explicitly requests that scope.
- Do not treat “tests required” as a mandate to create new tests.

## Build rule

Always run the native CachyOS host build for an implementation handoff so the
maintainer can inspect the current work locally. The resulting executable and
required runtime assets must be available in the working repository.
Use the existing Makefile build targets and the native host toolchain without
Debian compatibility flags. A Debian build does not replace this local build.

Run the Debian 12 compatibility build with `make release-zip` only for an
explicitly requested repository publication/update, including a Repo Run.
It is not part of ordinary implementation validation. Keep its build and
staging output separate so it does not replace the native executable used for
maintainer review.

Report the complete result, including warnings, for every required or
requested build. Identify native CachyOS and Debian 12 compatibility results
separately when both builds are required.

Do not remove existing `paplay` build signals from the Makefile.

## Handoff rule

Provide a file handoff only when explicitly requested by the maintainer.
If a file handoff is requested, provide a `tar.bz2` archive with changed files at correct relative paths.
Do not provide placeholder paths.
Do not provide patch chains.
Do not paste file contents into the chat.

<!-- BEGIN CODEX_RESPONSE_BUDGET -->

## Response Budget

Codex must follow [response-budget.md](documentation/codex/response-budget.md).

<!-- END CODEX_RESPONSE_BUDGET -->
