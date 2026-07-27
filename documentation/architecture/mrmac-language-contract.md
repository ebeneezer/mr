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
- Explicit MRMac globals are the values addressed through `CREATE_GLOBAL_STR`,
  `SET_GLOBAL_STR`, `SET_GLOBAL_INT`, `SET_GLOBAL_HASH`, `GLOBAL_STR`,
  `GLOBAL_INT`, `GLOBAL_HASH`, `FIRST_GLOBAL` and `NEXT_GLOBAL`.
- The VM global hash store is the central K/V store for macro-visible runtime
  values. Macro-visible runtime data must not be duplicated into value-bearing
  C++ registries when it can be represented as VM scalar, string, array or hash
  data.
- C++ may keep mechanical artifacts that are not macro-visible values, including
  compiled bytecode storage, live VM ownership, TVision view pointers, callback
  function pointers and coprocessor task handles. Those artifacts must not
  become hidden semantic stores for macro-visible data.
- Chained hash reads are allowed for expression values, e.g. `Value := GLOBAL_HASH('USER_TABLES')['C']['KEY'];`.
- Chained hash assignment is allowed for concrete hash lvalues, e.g. `GLOBAL_HASH('USER_TABLES')['C']['KEY'] := Value;`.
- Typed arrays are declared with the existing declaration forms by adding `[]`, for example `DEF_INT(values[])`.
- Hash and array elements are addressed with bracket indexing, for example `snippets['FOR']` and `values[1]`.
- `DEF_*` declarations create VM variables for the current execution context.
  They are not file-wide globals. MRMac must not introduce namespaces whose
  identity is derived from source file boundaries in the filesystem.
- Local arrays live in the VM instance variable space. Global arrays live only in the explicit global variable store when stored as globals.
- Array writes grow the array for positive indices. Array reads outside the current bounds are runtime errors.
- `LEN(value)` returns string length for string-like values and element count for arrays.
- `KEYS(hash)` and `VALUES(hash)` return string arrays. They must not be emulated as delimited strings.
- `EXISTS(hash, key)` tests key presence.
- `HAS_VALUE(hash, key)` tests key presence with a non-default value.
- User macros may store local automation tables in volatile global hashes. Such tables are user-controlled runtime state, not a project-maintained snippet catalog.

## Macro Globals Runtime State

Explicit MRMac globals are runtime-only VM state under the top-level K/V key
`MACROGLOBALS`. They are process-global macro language values, not
execution-session state and not file-scoped state.

Approved macro-visible runtime roots such as `EXECSESSIONS`, `MODELESSUI`,
`MACROCATALOG` and `MACRODEBUGGER` are separate top-level K/V roots. They must
not be stored as explicit globals under `MACROGLOBALS/runtime`, but
`GLOBAL_HASH('<rootName>')` may expose them for runtime inspection and existing
macro compatibility.

`MACROGLOBALS` data must be grouped by runtime role:

- `MACROGLOBALS/runtime/byName/<globalName>` for explicit global scalar, string,
  array or hash values,
- `MACROGLOBALS/runtime/order/<index>` for `FIRST_GLOBAL` and `NEXT_GLOBAL`
  enumeration order,
- `MACROGLOBALS/runtime/enumeration/globalIndex` for global enumeration state.

The key `<globalName>` is the normalized MRMac global name. It must not be
derived from a source file path. Two macro files that explicitly write the same
global name address the same process-global MRMac value by language design.

Normal `DEF_*` variables in a finite macro execution remain VM-local. In a
long-lived execution session, `DEF_*` variables are session state and belong
under `EXECSESSIONS`, not under `MACROGLOBALS`.

C++ `GlobalEntry`-like records are transfer objects only. They must be rebuilt
from `MACROGLOBALS` and must not become a second value-bearing global registry.

## Macro Catalog Runtime State

Loaded macro file and macro definition state is runtime-only VM state under the
top-level K/V key `MACROCATALOG`. It is not execution-session state: it is the
runtime catalog from which macro execution, assigned-key dispatch,
`INQ_MACRO`, `FIRST_MACRO`, `NEXT_MACRO`, lazy indexed macro loading and
runtime menu lookup resolve loaded macros.

`MACROCATALOG` data must be grouped by runtime role:

- `MACROCATALOG/files/byKey/<fileKey>` for loaded macro files,
- `MACROCATALOG/files/byKey/<fileKey>/bytecode` for the resident bytecode
  image when it is loaded,
- `MACROCATALOG/files/byKey/<fileKey>/profile` for the analyzed execution
  profile,
- `MACROCATALOG/files/byKey/<fileKey>/macroNames` for the macro keys provided
  by that file,
- `MACROCATALOG/files/byKey/<fileKey>/sourceMap` for source spans generated by
  a debugger-start compile of the loaded macro file,
- `MACROCATALOG/macros/byName/<macroKey>` for macro metadata,
- `MACROCATALOG/macros/order` for macro enumeration and reverse assigned-key
  dispatch order,
- `MACROCATALOG/enumeration/macroIndex` for global macro enumeration state,
- `MACROCATALOG/indexed/files/order` for lazily indexed macro files,
- `MACROCATALOG/indexed/bindings/order` for lazily indexed assigned-key
  bindings,
- `MACROCATALOG/indexed/warmup/cursor` and
  `MACROCATALOG/indexed/warmup/attemptedFiles/<fileKey>` for indexed warmup.

C++ `MacroRef`, `LoadedMacroFile` and `IndexedBoundMacroEntry` objects are
transfer objects only. They must be rebuilt from `MACROCATALOG` and must not
become a second value-bearing macro registry.

## Macro Debugger Runtime State

Debugger-controlled and debugger-generated runtime data is runtime-only VM
state under the top-level K/V key `MACRODEBUGGER`. It is not macro catalog
state and not execution-session ownership state.

`MACRODEBUGGER` data must be grouped by runtime role:

- `MACRODEBUGGER/sessions/byId/<debugSessionId>` for active debugger session
  state,
- `MACRODEBUGGER/breakpoints/byMacro/<macroKey>` for debugger-owned
  breakpoint definitions,
- `MACRODEBUGGER/breakpoints/byMacro/<macroKey>/byLine/<line>` for line-bound
  breakpoints normalized through the macro source map,
- `MACRODEBUGGER/watches/byMacro/<macroKey>/byExpression/<expression>` for
  debugger-owned watch definitions,
- `MACRODEBUGGER/sessions/byId/<debugSessionId>/snapshots` for generated
  debugger snapshots.

Source maps are generated only when a macro is started for debugging. Normal
macro load, lazy resident bytecode refresh, assigned-key dispatch and runtime
menu lookup must not create source maps. Source maps that belong to loaded macro
files remain under `MACROCATALOG`.
Debugger breakpoints must store normalized source-map binding data under
`MACRODEBUGGER`, including enabled state, source line, source span, bytecode
offset and debuggable kind. This live binding data must not be stored under
`EXECSESSIONS`, `MACROCATALOG` or settings. A debugger Bento may persist only
the cold breakpoint definition in its protected `WORKSPACE` extension:
normalized macro key, source identity, source line, enabled state and optional
condition text. Restore must recompile the source map and rebind that
definition into `MACRODEBUGGER`; bytecode offsets, source spans and generated
source maps are never persistence truth.
Watch definitions contain the source expression and enabled state only. Watch
values and errors are derived from the paused live VM and must not become
persisted debugger truth. The cold expression and enabled state may be stored
with the debugger Bento's `WORKSPACE` configuration and must be written back to
`MACRODEBUGGER` on a later debug start. A watch expression uses the canonical
compiler frontend in a restricted pure-expression mode; it must not introduce
a second parser, a second bytecode format, procedures, assignments, UI
operations or file/process side effects.

Debugger variable mutation is live-session state, never debugger persistence.
Only a paused debug VM may change an existing value. The VM validates the
projected root name, scope, type and complete hash/array path again at write
time, writes the existing local, closure, session or application-global
backing store where applicable, and returns a fresh full variable snapshot.
Scalars may be replaced with a value of their existing type. Hash entries may
be added, renamed or removed; array elements may be appended or removed; and
nested scalar values may be replaced. The debugger exposes the complete
hierarchical hash/array projection, including cycle markers, rather than a
value-bearing UI shadow. The UI keeps no writable variable copy.

MRMac has no file-global variable scope. `DEF_*` variables are local, closure
or execution-session state according to their actual execution context;
explicit globals are application globals under `MACROGLOBALS`. The debugger
must show these actual scopes and must not invent a file-global category.

The stable source identity is the normalized resolved source path plus macro
name. The UI may render its familiar form as `Filename^MacroName`; internal
ownership, breakpoint rebinding and the one-debugger-per-source rule use the
normalized identity.
C++ debugger objects are transfer objects only. They must be rebuilt from
`MACRODEBUGGER` and must not become a second value-bearing debugger registry.

## Compiler Support Macros

Compiler support macro files, including
`mrmac/macros/compilersupport/MRCompilerMiddleware.mrmac`, are libraries of
concrete build-hook routines.

A compiler profile must reference the exact macro routine it intends to run,
for example `compilersupport/MRCompilerMiddleware.mrmac^LatexMKPostBuild`.

Compiler support macros must not introduce a generic post-build dispatcher such
as `MRCompilerPostBuild` that switches on `MR_BUILD_TOOLCHAIN`.

Concrete routines must be named for the build tool or workflow they implement,
for example `LatexMKPostBuild`.

Shared helper macros are allowed only when they factor repeated mechanics. They
must not hide toolchain selection, profile selection or post-build routing.

Auto-detected compiler profiles may set a default hook only by writing the
same explicit macro spec that the user can inspect and replace in the compiler
profile dialog.

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
