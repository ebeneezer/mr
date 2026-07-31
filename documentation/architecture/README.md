# Architecture Contract Router

This file is reached through root [`AGENTS.md`](../../AGENTS.md). It routes a
change to binding contracts; it is not an independent instruction root.

Read every row that matches the files, functions or semantics being changed.
Changes spanning domains require all matching contracts.

| Touched area | Contract to read |
|---|---|
| Application commands, dialogs, validation or dialog layout | [App / UI / Dialogs](app-ui-dialogs-contract.md) |
| TVision views, drawing, events, focus, windows or Bento projection | [TVision Integration](tvision-integration-contract.md) |
| Hex editor document ownership, pane roles, editing or projection | [Hex Editor](hex-editor-contract.md) |
| Runtime settings authority, assignments or dirty state | [Settings Runtime](settings-runtime-contract.md) |
| Startup settings loading, normalization, `MRSETUP` or final VM apply | [Settings Bootstrap](settings-bootstrap-contract.md) |
| Settings, theme or workspace writing and `SAVE_SETTINGS` | [Settings Persistence](settings-persistence-contract.md) |
| Keymap parsing, canonicalization, persistence or resolution | [Keymap](keymap-contract.md) |
| MRMAC grammar, compiler, runtime globals, catalog or debugger data | [MRMAC Language / Compiler](mrmac-language-contract.md) |
| Macro execution lifetime, scheduler, closures or cancellation | [MRMac Execution Session](mrmac-exec-session-contract.md) |
| VM intrinsics, typed macro UI, modeless UI or macro screen projection | [VM / Intrinsics / Deferred UI](vm-deferred-ui-contract.md) |
| Coprocessor workers, owners, packages, affinity or external sources | [Coprocessor Runtime](coprocessor-runtime-contract.md) |
| Deferred macro UI queueing, ordering or playback | [Coprocessor / Deferred UI](coprocessor-deferred-ui-contract.md) |
| Shared and local file/path operations | [File / Path Utilities](file-path-utilities-contract.md) |
| Syntax coloring, parsing, folding, indentation or derived syntax data | [Syntax Analysis](syntax-analysis-contract.md) |
| MRMAC reference manual content or layout | [MRMAC Reference Manual](mrmac-reference-manual-contract.md) |
| Makefile, generated files, clean behavior or regression harness | [Build / Generated Files / Regression](build-regression-contract.md) |

## Cross-domain boundary

Staging, validation, canonicalization, final apply, persistence, rendering and
runtime execution are distinct roles. A shared implementation must not erase
their ownership or ordering.

MR extends existing foundations through specialization, composition,
configuration and dispatch. New infrastructure is justified only when no
existing contract carries the required behavior.
