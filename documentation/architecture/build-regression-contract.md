# Build / Generated Files / Regression Contract

## Scope

Applies to:

- `Makefile`
- `generate_help_markdown.sh`
- `generate_about_quotes.sh`
- `regression/mr-regression-checks.cpp`
- generated headers,
- build and clean targets.

## Authority

The Makefile is the build orchestrator.
Regression checks are optional verification tools, not protected architecture.
They may describe behavior or structure, but they do not create protected structure by themselves.

## Generated files

Versioned generated headers are treated as tracked source artifacts for clean-build stability.

`make clean` must not delete tracked generated headers unless the full one-shot clean build remains proven safe.

## Invariants

- The normal visible regression suite must contain at most 20 `runTest(...)` entries.
- Regression checks must target stable behavioral or structural invariants with low maintenance cost.
- Tracked generated headers remain available to a one-shot clean build.
- Build targets must not create a new directory inside the repository without
  explicit maintainer approval.
- Temporary build files and staging directories belong below the system
  temporary directory, not inside the repository.
- Version-specific release notes are publication metadata maintained directly
  on GitHub and must not be stored as files in the repository.

Build invocation, regression timing and `paplay` requirements are defined once
in root [`AGENTS.md`](../../AGENTS.md).

## Boundaries

Without explicit maintainer approval:

- Changing generated-file policy incidentally.
- Changing clean semantics beyond the requested build fix.
- Adding overly case-specific regression checks where one sufficiently abstract invariant check can protect the same contract.
- Adding broad test infrastructure or rewriting the regression harness without explicit maintainer scope.

## Required manual tests

For build changes, test:

- `make clean all CXX=clang++`,
- one-shot clean build,
- repeated incremental build,
- generated headers after clean,
- failure path if the change affects failure behavior.

For regression-suite changes, test:

- `make regression-probe CXX=clang++`,
- `regression/mr-regression-checks`,
- verify that the normal visible suite reports no more than 20 checks.
