# mrmac Language / Compiler Contract

## Scope

Applies to:

- `mrmac/mrmac.c`
- `mrmac/MRMacroRunner.cpp`
- the canonical bytecode generation path.

## Authority

The mrmac language and compiler define executable macro semantics.

Settings use `MRSETUP(...)` as serialized macro source in controlled startup mode, but language semantics are broader than settings.

## Invariants

- Do not change grammar as incidental cleanup.
- Do not change opcodes without a dedicated migration decision.
- Do not change macro compile behavior during UI or settings refactoring.
- Macro names are not limited to eight characters.
- `mrmac/mrmac.c` is the canonical compiler frontend.
- Snippet commands are VM intrinsics. They must remain runtime-only and must not introduce persistent registries outside the existing macro/VM runtime state.
- Global hashes are volatile macro runtime state. They may hold nested hashes across macro invocations, but they must not be serialized as settings or workspace state.
- Chained hash reads are allowed for expression values, e.g. `Snippet := GLOBAL_HASH('SNIPPETS')['C']['FOR'];`.
- Chained hash assignment is allowed for concrete hash lvalues, e.g. `GLOBAL_HASH('SNIPPETS')['C']['FOR'] := Snippet;`.
- Typed arrays are declared with the existing declaration forms by adding `[]`, for example `DEF_INT(values[])`.
- Hash and array elements are addressed with bracket indexing, for example `snippets['FOR']` and `values[1]`.
- Local arrays live in the VM instance variable space. Global arrays live only in the compilat-wide global variable store when explicitly stored as globals.
- Array writes grow the array for positive indices. Array reads outside the current bounds are runtime errors.
- `LEN(value)` returns string length for string-like values and element count for arrays.
- `KEYS(hash)` and `VALUES(hash)` return string arrays. They must not be emulated as delimited strings.
- `EXISTS(hash, key)` tests key presence.
- `HAS_VALUE(hash, key)` tests key presence with a non-default value.
- Snippet definitions are stored in `GLOBAL_HASH('SNIPPETS')`.
- `SNIPPET_START()` is the Sidekick entry point. It derives language and trigger word from the active editor, reads only the matching subtree in `GLOBAL_HASH('SNIPPETS')`, and opens an ephemeral Sidekick editor without mutating the parent editor.
- Sidekick commit is explicit. Only the Sidekick commit operation may replace the trigger range in the parent editor.
- `SNIPPET_NEXT_PLACEHOLDER(...)` and `SNIPPET_PREV_PLACEHOLDER(...)` are Sidekick placeholder commands. Until placeholder state exists, they are no-ops and must not mutate editor text.
- `SNIPPETS_UNLOAD(language)` removes a language subtree from the volatile global snippet hash.

## Allowed

- Local bug fixes with regression proof.
- Compiler diagnostics improvements when text changes are approved.
- Compiler fixes that preserve existing documented semantics.

## Forbidden without explicit approval

- Grammar rewrites.
- Opcode renumbering or semantic changes.
- Treating settings cleanup as macro-language cleanup.
- Introducing hidden compatibility rewrites.
- Introducing a second compiler frontend or bytecode emission path.

## Required tests

For language/compiler changes, test:

- clean build,
- representative macro compile,
- VM execution of compiled bytecode,
- relevant regression checks.
