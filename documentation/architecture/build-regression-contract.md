# Build / Generated Files / Regression Contract

## Scope

Applies to:

- `Makefile`
- `generate_help_markdown.sh`
- `generate_about_quotes.sh`
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

- A regression check must protect a concrete maintainer-facing behavior or
  stable structural invariant against a credible failure. It must be
  deterministic, proportionate to the risk and cheaper to maintain than the
  protection it provides. Test counts and generated coverage are not goals.
- Prefer the narrowest existing test mechanism that observes the failure.
  Checks that merely repeat implementation logic or duplicate coverage do not
  qualify. Use a focused manual scenario when that gives better protection.
- Tracked generated headers remain available to a one-shot clean build.
- Build targets must not create a new directory inside the repository without
  explicit maintainer approval.
- Temporary build files and staging directories belong below the system
  temporary directory, not inside the repository.
- Version-specific release notes are publication metadata maintained directly
  on GitHub and must not be stored as files in the repository.
- All published release text must be in English, as required by root
  `AGENTS.md`. This includes GitHub release titles and bodies, external
  release-notes input files, and notes embedded in release artifacts or signed
  update manifests. Verify the language before packaging and signing.

## Native validation and repository publication

Root [`AGENTS.md`](../../AGENTS.md#build-rule) defines build invocation,
regression timing and `paplay` requirements.

- Every implementation handoff requires the native CachyOS build and a current
  native executable with its required runtime assets in the working repository
  for maintainer review.
- The Debian 12 compatibility build is reserved for explicitly requested
  repository publication/updates, including the
  [Repo Run](../../AGENTS.md#repo-run) workflow. It does not replace the native
  build and is not required for ordinary implementation validation.
- A Repo Run bumps the version, commits and pushes the accepted changes to
  `main` with English publication text, builds the Debian 12 compatibility
  package, and updates the corresponding GitHub release and signed update
  artifacts.
- Build publication artifacts from the exact pushed commit in a separate
  system-temporary source snapshot. Preserve the working repository's native
  executable for local review.
- Release tags, package versions, release notes, checksums and signed update
  metadata must refer to the same published source state and verified build.
- A failed compatibility build blocks publication of release/update artifacts.

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
