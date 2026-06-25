# VM / Intrinsics / Deferred UI Contract

## Scope

Applies to:

- `mrmac/MRVM.cpp`
- `mrmac/MRVM.hpp`
- VM intrinsic dispatch,
- `MRSETUP`,
- `SAVE_SETTINGS`,
- typed `UI_*` procedures,
- MacroCellGrid / MacroCellView / UI facade bridges.

## Authority

The VM owns bytecode execution state.

The central runtime K/V authority for macro-visible runtime state is the VM
global hash store reached through top-level global hash roots. A runtime value
that can be represented as a VM value must not be kept in a second value-bearing
C++ registry beside that K/V store.

For screen operations, the VM is a producer of staged UI commands and facade mutations.
TVision-facing UI code is the consumer/projection layer.

## Invariants

- Do not create competing screen write paths.
- Do not bypass MacroCellGrid / MacroCellView contracts.
- Do not blur producer/consumer boundaries.
- Do not introduce value-bearing runtime registries beside the VM global K/V
  store when the value can be represented as VM scalar, string, array or hash
  data.
- C++ may keep only mechanical handles that cannot be represented as VM values,
  such as live TVision view pointers, projection handles, callback function
  pointers, suspended `VirtualMachine` ownership and coprocessor task handles.
- Do not modify `MRSETUP` startup gating incidentally.
- Do not modify `SAVE_SETTINGS` incidentally.
- Do not change VM error texts without regression awareness.

## MRSETUP

`MRSETUP` is allowed in controlled startup settings mode.
Runtime use restrictions are part of VM semantics.

## Typed UI Procedures

Macro-visible UI operations must be typed procedures.
Screen operations must remain staged/projection-based unless a dedicated architecture decision says otherwise.
Do not add generic TVision escape hatches or untyped UI call opcodes.

Current macro-visible UI primitive catalog:

- `UI_DIALOG(x, y, width, height, title)` starts a dialog/window definition.
- `UI_LABEL(x, y, text)` adds static text to the current definition.
- `UI_BUTTON(x, y, width, id, text)` adds a button to the current definition.
- `UI_DISPLAY(x, y, width, text)` adds a retained display line to the current definition.
- `UI_INPUT(x, y, width, id, name, initialValue)` adds an input field to the current definition.
- `UI_LISTBOX(x, y, width, height, id, label, itemSpec, start)` adds a listbox backed by a named item list.
- `UI_GRID(x, y, width, height, id, label, itemSpec, start)` adds a grid backed by a named item list.
- `UI_LIST_CLEAR(name)` clears a named runtime item list.
- `UI_LIST_ADD(name, value)` appends one value to a named runtime item list.
- `UI_MODELESS_ON(controlId, macroSpec)` binds a modeless control activation to a macro.
- `UI_MODELESS_SHOW(windowId)` shows a modeless window and may select it through TVision focus/Z-order semantics.
- `UI_MODELESS_UPDATE(windowId)` updates an existing modeless window without selecting it or promoting it in Z-order.
- `UI_MODELESS_DISPLAY(windowId, displayIndex, text)` updates one retained display line in an existing modeless window without rebuilding the window definition.
- `UI_MODELESS_CLOSE(windowId)` requests modeless window close through the TVision event path.
- `UI_MESSAGEBOX(text)` opens a modal message box.
- `SET_CLIPBOARD_TEXT(text)` writes text to the clipboard.

The catalog records implemented primitives only. It is not approval for future
generic window handles, paint callbacks, timer primitives or direct drawing
operations. Additions require a dedicated architecture decision.

## Modeless UI Runtime State

Modeless macro UI state is runtime-only VM state under the top-level K/V key
`MODELESSUI`.

Dialog definition staging, modal dialog return values, modeless definition
staging and named UI item lists live under `MODELESSUI/staging`. C++ may build
short-lived `MacroUiDialogDefinition` or TVision objects from that K/V subtree
for projection only; those objects must not become authoritative stores.

`MODELESSUI` data must be grouped by runtime role:

- `MODELESSUI/staging/currentDialog` for the staged dialog definition and
  dialog result values,
- `MODELESSUI/staging/currentDialog/labels`,
  `MODELESSUI/staging/currentDialog/buttons`,
  `MODELESSUI/staging/currentDialog/displays`,
  `MODELESSUI/staging/currentDialog/inputs`,
  `MODELESSUI/staging/currentDialog/listBoxes` and
  `MODELESSUI/staging/currentDialog/grids` for staged controls,
- `MODELESSUI/staging/currentDialog/textValues` and
  `MODELESSUI/staging/currentDialog/indexValues` for macro-visible UI result
  values,
- `MODELESSUI/staging/currentDialog/modelessButtonMacros` for staged modeless
  control bindings,
- `MODELESSUI/staging/itemLists/<listName>` for named UI list data,
- `MODELESSUI/windows/<windowId>` for retained modeless window definitions,
- `MODELESSUI/windows/<windowId>/liveGeometry` for live window geometry.

New modeless runtime data must be placed under a meaningful branch in that
hierarchy, not as flat siblings directly below `MODELESSUI`.

C++ TVision code may keep mechanical live-window pointers and projection
handles that cannot be represented as VM K/V values. It must not keep a second
value-bearing modeless UI registry beside `MODELESSUI`.

## Forbidden without explicit approval

- Direct render facade usage outside whitelisted bridge points.
- New VM persistence side effects.
- New UI side effects hidden in generic intrinsics.
- Changing startup mode conditions.
- Changing deferred UI batching boundaries.

## Required tests

For VM / deferred UI changes, test:

- clean build,
- VM regression checks,
- MRSETUP allowed and forbidden contexts,
- SAVE_SETTINGS behavior if touched,
- typed UI procedure macro cases,
- deferred playback if touched,
- UI facade projection behavior.
