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
Regression checks encode both behavior and protected structure.

## Generated files

Versioned generated headers are treated as tracked source artifacts for clean-build stability.

`make clean` must not delete tracked generated headers unless the full one-shot clean build remains proven safe.

## Invariants

- `make clean all CXX=clang++` must work.
- Existing `paplay` build signals must not be removed.
- Regression checks must not be weakened to make a refactoring pass.
- Structure checks are contracts unless deliberately changed.

## Forbidden without explicit approval

- Removing regression checks to permit cleanup.
- Changing generated-file policy incidentally.
- Changing clean semantics beyond the requested build fix.
- Replacing text structure checks with no equivalent protection.
- Removing Makefile audio feedback.

## Required tests

For build/regression changes, test:

- `make clean all CXX=clang++`,
- regression target if available,
- one-shot clean build,
- repeated incremental build,
- generated headers after clean,
- failure path if the change affects failure behavior.
