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

Approved macro-visible runtime roots such as `EXECSESSIONS`, `MODELESSUI` and
`MACROCATALOG` are separate top-level K/V roots. They must not be stored as
explicit globals under `MACROGLOBALS/runtime`, but `GLOBAL_HASH('<rootName>')`
may expose them for runtime inspection and existing macro compatibility.

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
