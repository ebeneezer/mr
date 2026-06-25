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
- Chained hash reads are allowed for expression values, e.g. `Value := GLOBAL_HASH('USER_TABLES')['C']['KEY'];`.
- Chained hash assignment is allowed for concrete hash lvalues, e.g. `GLOBAL_HASH('USER_TABLES')['C']['KEY'] := Value;`.
- Typed arrays are declared with the existing declaration forms by adding `[]`, for example `DEF_INT(values[])`.
- Hash and array elements are addressed with bracket indexing, for example `snippets['FOR']` and `values[1]`.
- Local arrays live in the VM instance variable space. Global arrays live only in the compilat-wide global variable store when explicitly stored as globals.
- Array writes grow the array for positive indices. Array reads outside the current bounds are runtime errors.
- `LEN(value)` returns string length for string-like values and element count for arrays.
- `KEYS(hash)` and `VALUES(hash)` return string arrays. They must not be emulated as delimited strings.
- `EXISTS(hash, key)` tests key presence.
- `HAS_VALUE(hash, key)` tests key presence with a non-default value.
- User macros may store local automation tables in volatile global hashes. Such tables are user-controlled runtime state, not a project-maintained snippet catalog.

## LSP and snippets strategy

MR is an LSP client, not a maintained collection of language-specific snippet
libraries.

Language intelligence must come from the configured language server whenever
the language server can provide it. This includes completion items, snippet
completion items, `completionItem/resolve` results, diagnostics, hover
documentation, definitions, declarations, references, document symbols,
workspace symbols, rename support, code actions and formatting where the
language server supports those channels.

MR must not compensate for weak language-server support by adding hardcoded
snippet libraries for individual programming or markup languages. A supported
language may have LSP server candidates, compiler profile configuration and
generic LSP client handling, but it must not receive a private MR snippet pack
as a substitute for missing server quality.

User-authored MRMAC automation is different from shipped MR snippet libraries.
MRMAC must remain capable enough for users to implement local snippet tables,
toolchain actions and editor workflows by reading editor state, active syntax
language and cursor context, then applying user-defined text transformations.
This programmability is a central MR capability. It is not a license for MR to
ship and maintain universal language snippet catalogs.

The strategic boundary is: LSP provides standardized language intelligence;
MRMAC provides user-controlled local workflow automation.

MRExpand must not use C++-parsed sidecar snippet tables. If MRExpand is
reintroduced, snippet mapping and edit-stop selection must run through MRMAC
middleware or another explicitly configured user tool. C++ may host the
middleware invocation and apply the returned edit, but it must not own
language-specific snippet catalogs.

MRExpand middleware snippets must expose semantically useful edit stops instead
of wrapping whole syntax fragments into one coarse placeholder. Delimiters and
content positions that a programmer naturally edits independently are separate
stops. For example, a LaTeX tabular column specification uses stops equivalent
to `@{<1>}<2>@{<3>}` and places the table body at the next stop between
`\begin` and `\end`.

Language-server candidate selection must be evaluated strategically for all
supported languages. Prefer official or de-facto standard servers that provide
the broadest useful set of LSP channels with stable stdio operation on Linux.
When multiple suitable servers exist, auto setup may present a selection
dialog and write the selected executable and arguments into the normal toolchain
configuration. The selected values remain editable text fields; the dialog
choice must not create a hidden language-specific configuration path.

Dead snippet runtime infrastructure must not be kept solely for historical
compatibility if no accepted runtime use remains. Removing such infrastructure
is a dedicated VM/MRMAC contract change and must be planned separately from
ordinary LSP or settings work.

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
