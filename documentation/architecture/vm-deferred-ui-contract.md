# VM / Intrinsics / Deferred UI Contract

## Scope

Applies to:

- `mrmac/MRVM.*`,
- VM intrinsic and procedure dispatch,
- `mrmac/ui/conventional/`,
- `mrmac/ui/modeless/`,
- `MRSETUP` and `SAVE_SETTINGS`,
- MacroCellGrid, MacroCellView and UI-facade bridges.

## Authority

The VM owns bytecode execution state. Macro-visible runtime values belong to
the VM global K/V store under approved top-level roots.

For screen operations, the VM produces typed staged commands or facade
mutations. TVision-facing code consumes and projects that state.

## Invariants

- A VM-representable runtime value must not live in a second value-bearing C++
  registry.
- C++ may retain only mechanical handles such as live TVision pointers,
  callbacks, suspended VM ownership and coprocessor task handles.
- MacroCellGrid, MacroCellView and UI-facade bridges form one screen projection
  path; competing write paths are not allowed.
- Producer, retained model, deferred command and TVision projection roles
  remain distinct.
- VM error text is observable behavior.

## Settings intrinsics

`MRSETUP` is executable only in controlled startup settings mode. Its accepted
source and final application follow the
[Settings Bootstrap contract](settings-bootstrap-contract.md).

`SAVE_SETTINGS` invokes the protected path defined by the
[Settings Persistence contract](settings-persistence-contract.md).

Neither intrinsic may acquire incidental UI, persistence or runtime side
effects.

## Typed macro UI

Macro-visible UI operations are typed procedures. Their canonical signatures
come from `mrmac/mrmac.c`; implemented semantics come from the VM and are
documented by the MRMAC reference manual.

- Conventional `UI_*` definitions are consumed modally by `UI_EXEC`.
- The same common definitions may be projected as retained modeless MMP
  windows.
- Screen operations remain staged and projection-based.
- MRMAC receives typed values and logical identifiers, never TVision objects,
  pointers, drawing contexts, raw events or character attributes.
- A new generic window handle, untyped call opcode, paint callback or direct
  drawing escape requires a dedicated architecture decision.

## MMP layering

MMP has two layers:

- Core primitives retain modeless state, request execution-session callbacks
  and project that state through TVision.
- Composed constructs express ordinary controls such as buttons, fields,
  menus, status displays, logs and timers without requiring canvas geometry or
  event mechanics.

Core primitives remain available for custom UI. A new low-level primitive must
state its direct ergonomic use or the composed construct that makes it usable.
Convenience must not create a second model or bypass normal TVision and
execution-session routes.

## Modeless runtime state

Modeless macro UI is runtime-only state under `MODELESSUI`.

| Branch | Ownership |
|---|---|
| `MODELESSUI/staging/currentDialog` | Current modal/modeless definition, controls, bindings and result values |
| `MODELESSUI/staging/itemLists/<listName>` | Named list, tree and table data |
| `MODELESSUI/counters/windowInstances` | Opaque runtime model-id allocation |
| `MODELESSUI/windows/<windowId>` | Retained modeless definition and authoritative field values |
| `MODELESSUI/windows/<windowId>/liveGeometry` | Current projected geometry |
| `MODELESSUI/windows/<windowId>/desktop` | Virtual-desktop, visibility, minimize and restore state |
| `MODELESSUI/windows/<windowId>/canvases/<canvasId>` | Canvas definition, scene and committed generation |
| `MODELESSUI/windows/<windowId>/canvasHotspots` | Retained canvas-local callbacks |

Typed field families and tree/table selection state use meaningful subordinate
branches below the staged definition and retained window. New modeless state
must extend the matching branch, not add flat roots or side registries.

MMP timers are scheduler state under `EXECSESSIONS`, identified by modeless
window owner and logical timer id. `MODELESSUI` must not mirror scheduler
consumer state.

C++ may construct short-lived definition, snapshot and TVision projection
objects from `MODELESSUI`; these are never authoritative value stores. A
canvas view reads its transient scene from `MODELESSUI` during normal
`draw()`. Commit redraws only the addressed view and does not change focus,
desktop selection or Z-order.

## Boundaries

Without explicit maintainer approval:

- No direct render-facade use outside approved bridge points.
- No generic intrinsic with hidden UI or persistence effects.
- No change to startup mode, screen mutation paths or deferred batching as
  local cleanup.

## Related contracts

- [MRMAC Language / Compiler](mrmac-language-contract.md)
- [MRMac Execution Session](mrmac-exec-session-contract.md)
- [Coprocessor / Deferred UI](coprocessor-deferred-ui-contract.md)
- [TVision Integration](tvision-integration-contract.md)
- [MRMAC Reference Manual](mrmac-reference-manual-contract.md)

## Required manual tests

- Exercise allowed and forbidden `MRSETUP` contexts.
- Exercise `SAVE_SETTINGS` when touched.
- Run representative conventional and modeless typed UI macros.
- Update every touched retained-control family.
- Verify modeless callbacks and timers use execution sessions.
- Verify deferred playback ordering when touched.
- Expose, resize and redraw modeless and macro-screen projections.
