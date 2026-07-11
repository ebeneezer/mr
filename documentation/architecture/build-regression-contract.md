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

- `make clean all CXX=clang++` must work.
- Existing `paplay` build signals must not be removed.
- The normal visible regression suite must contain at most 20 `runTest(...)` entries.
- Regression checks must target stable behavioral or structural invariants with low maintenance cost.
- Regression checks are planned only after implementation and sight review of the finished tranche.
- Regression checks directly tied to the current change do not require separate maintainer approval.

## Forbidden without explicit approval

- Changing generated-file policy incidentally.
- Changing clean semantics beyond the requested build fix.
- Adding or expanding regression checks before the finished tranche has been reviewed.
- Adding overly case-specific regression checks where one sufficiently abstract invariant check can protect the same contract.
- Adding broad test infrastructure or rewriting the regression harness without explicit maintainer scope.
- Removing Makefile audio feedback.

## Required tests

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
