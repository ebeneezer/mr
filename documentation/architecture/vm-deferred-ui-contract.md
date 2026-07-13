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
- `MMP_CANVAS(x, y, width, height, canvasId)` adds one retained canvas to the current modeless definition.
- `MMP_CANVAS_CLEAR`, `MMP_CANVAS_TEXT`, `MMP_CANVAS_GLYPH`, `MMP_CANVAS_LINE`, `MMP_CANVAS_BOX` and `MMP_CANVAS_FILL` mutate the retained canvas scene addressed by window id and canvas id.
- `MMP_CANVAS_COMMIT(windowId, canvasId)` redraws exactly the addressed retained canvas through its existing TVision view.
- `MMP_CANVAS_HOTSPOT(canvasId, x, y, width, height, controlId, macroSpec)` adds a retained canvas-local left-click region to the current modeless definition.
- `MMP_ACTION_BUTTON(x, y, width, controlId, caption, macroSpec)` combines a native modeless button declaration with its callback binding.
- `MMP_MENU_CLEAR(menuId)`, `MMP_MENU_ITEM(menuId, label, value, detail)` and `MMP_ACTION_MENU(x, y, width, height, controlId, caption, menuId, macroSpec)` compose a native modeless action menu from the existing named item list, grid and callback route.
- `MMP_TEXT_FIELD(x, y, width, fieldId, label, initialText)`, `MMP_TEXT_VALUE(windowId, fieldId)` and `MMP_TEXT_SET(windowId, fieldId, text)` compose a named native text input without exposing a control id or TVision input view.
- `MMP_BOOL_FIELD(x, y, fieldId, caption, initialValue)`, `MMP_BOOL_VALUE(windowId, fieldId)` and `MMP_BOOL_SET(windowId, fieldId, value)` compose a named native boolean input without exposing a control id or TVision view.
- `MMP_INT_FIELD(x, y, width, fieldId, label, initialValue, minimum, maximum)`, `MMP_INT_VALUE(windowId, fieldId)` and `MMP_INT_SET(windowId, fieldId, value)` compose a named range-bounded integer input without exposing a control id or TVision view.
- `MMP_PROGRESS_FIELD(x, y, width, fieldId, label, total, initialValue)`, `MMP_PROGRESS_VALUE(windowId, fieldId)` and `MMP_PROGRESS_SET(windowId, fieldId, value)` compose a named retained progress display without exposing a TVision view.
- `MMP_LOG_FIELD(x, y, width, height, logId, label, capacity)`, `MMP_LOG_APPEND(windowId, logId, text)`, `MMP_LOG_CLEAR(windowId, logId)` and `MMP_LOG_COUNT(windowId, logId)` compose a bounded retained event log without exposing a TVision view.
- `MMP_SELECT_FIELD(x, y, width, height, fieldId, label, initialValue)`, `MMP_SELECT_OPTION(fieldId, option)`, `MMP_SELECT_VALUE(windowId, fieldId)` and `MMP_SELECT_SET(windowId, fieldId, value)` compose a named native selection field without exposing a control id or TVision view.
- `MMP_STATUS_FIELD(x, y, width, statusId, initialText)` declares one named modeless status field.
- `MMP_STATUS_SET(windowId, statusId, text)` updates a named status field without exposing its display index.
- `MMP_TIMER_START(windowId, timerId, intervalMs, macroSpec)` registers one logical timer for an existing modeless window through the runtime scheduler.
- `MMP_TIMER_STOP(windowId, timerId)` removes that logical timer without exposing its scheduler consumer id.
- `MMP_WINDOW_EXISTS`, `MMP_WINDOW_X`, `MMP_WINDOW_Y`, `MMP_WINDOW_WIDTH` and `MMP_WINDOW_HEIGHT` expose typed modeless-window state without raw K/V traversal.
- `UI_MESSAGEBOX(text)` opens a modal message box.
- `SET_CLIPBOARD_TEXT(text)` writes text to the clipboard.

The catalog records implemented primitives only. It is not approval for future
generic window handles, paint callbacks or direct drawing operations. Additions
require a dedicated architecture decision.

## MMP Layering and Usability

MMP is a two-layer macro UI contract:

- Core primitives provide the small, typed and safe operations needed to retain
  modeless UI state, request execution-session callbacks and project the model
  through TVision.
- Composed MMP constructs package recurring user-visible UI work such as
  buttons, status indicators, forms, menus and control panels from those core
  primitives.

Future macro authors must normally be able to choose a composed construct that
expresses their UI intent directly. They must not need to reproduce canvas
rendering, hotspot geometry, callback ownership, timer lifetime, focus handling
or TVision mechanics merely to create an ordinary interactive control.

Core primitives remain available for genuinely custom UI, but they are not a
requirement for ordinary modeless applications. A composed construct may own
its internal drawing, callbacks and timing; it must preserve the core boundary:
the calling macro receives only typed MRMAC values and logical identifiers,
never a TVision object, window handle, pointer, drawing context or raw event.

Every proposed low-level MMP primitive must state either its direct ergonomic
use for macro authors or the composed MMP construct that makes its normal UI
use ergonomic. Convenience must not introduce a second model, a second
value-bearing C++ registry or a bypass around normal TVision draw/event and
execution-session routes.

## Modeless UI Runtime State

Modeless macro UI state is runtime-only VM state under the top-level K/V key
`MODELESSUI`.

An MMP timer is scheduler state under `EXECSESSIONS`, identified by its modeless
window owner and logical timer id. `MODELESSUI` must not mirror scheduler
consumer state.

Dialog definition staging, modal dialog return values, modeless definition
staging and named UI item lists live under `MODELESSUI/staging`. C++ may build
short-lived `MacroUiDialogDefinition` or TVision objects from that K/V subtree
for projection only; those objects must not become authoritative stores.

Named boolean fields are staged below `MODELESSUI/staging/currentDialog/boolFields`
and retained below `MODELESSUI/windows/<windowId>/boolFields/<fieldId>`. Their
integer `value` is authoritative; the native checkbox is only its live projection.

Named integer fields are staged below `MODELESSUI/staging/currentDialog/intFields`
and retained below `MODELESSUI/windows/<windowId>/intFields/<fieldId>`. Their
integer `value`, `minimum` and `maximum` are authoritative; the native input is
only its live projection.

Named progress fields are staged below `MODELESSUI/staging/currentDialog/progressFields`
and retained below `MODELESSUI/windows/<windowId>/progressFields/<fieldId>`.
Their integer `total` and `value` are authoritative; the native bar is only its
live projection.

Named log fields are staged below `MODELESSUI/staging/currentDialog/logFields`
and retained below `MODELESSUI/windows/<windowId>/logFields/<logId>`. Their
bounded ring of text lines is authoritative; the non-focusable TVision view is
only its live projection.

Named selection fields are staged below `MODELESSUI/staging/currentDialog/selectFields`
and retained below `MODELESSUI/windows/<windowId>/selectFields/<fieldId>`. Their
string `value` and bounded `options` are authoritative; the native list selection
is only its live projection.

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
- `MODELESSUI/staging/currentDialog/statusFields` for staged logical
  status-field-to-display mappings,
- `MODELESSUI/staging/currentDialog/textFields` for staged logical
  text-field definitions,
- `MODELESSUI/staging/currentDialog/intFields` for staged logical bounded
  integer-field definitions,
- `MODELESSUI/staging/currentDialog/progressFields` for staged logical
  progress-field definitions,
- `MODELESSUI/staging/currentDialog/logFields` for staged logical bounded
  log-field definitions,
- `MODELESSUI/staging/currentDialog/selectFields` for staged logical
  selection-field definitions and options,
- `MODELESSUI/staging/currentDialog/canvasHotspots` for staged canvas-local
  modeless callback regions,
- `MODELESSUI/staging/itemLists/<listName>` for named UI list data,
- `MODELESSUI/counters/windowInstances` for runtime-only opaque MMP model-id
  allocation,
- `MODELESSUI/windows/<windowId>` for retained modeless window definitions,
- `MODELESSUI/windows/<windowId>/liveGeometry` for live window geometry.
- `MODELESSUI/windows/<windowId>/statusFields` for logical
  status-field-to-display mappings,
- `MODELESSUI/windows/<windowId>/textFields/<fieldId>` for retained
  text-field definitions and values,
- `MODELESSUI/windows/<windowId>/intFields/<fieldId>` for retained integer
  field definitions, inclusive range and value,
- `MODELESSUI/windows/<windowId>/progressFields/<fieldId>` for retained
  progress field definitions, total and value,
- `MODELESSUI/windows/<windowId>/logFields/<logId>` for retained log field
  definitions and a bounded chronological line ring,
- `MODELESSUI/windows/<windowId>/selectFields/<fieldId>` for retained
  selection-field definitions, options and value,
- `MODELESSUI/windows/<windowId>/desktop` for runtime-only virtual-desktop,
  manual-visibility, minimize and restore state of a desktop-managed modeless window.
- `MODELESSUI/windows/<windowId>/canvases/<canvasId>/definition` for retained canvas geometry,
- `MODELESSUI/windows/<windowId>/canvases/<canvasId>/scene` for retained canvas commands and
- `MODELESSUI/windows/<windowId>/canvases/<canvasId>/generation` for the committed scene generation.
- `MODELESSUI/windows/<windowId>/canvasHotspots` for retained canvas-local callback regions.

New modeless runtime data must be placed under a meaningful branch in that
hierarchy, not as flat siblings directly below `MODELESSUI`.

C++ TVision code may keep mechanical live-window pointers and projection
handles that cannot be represented as VM K/V values. It must not keep a second
value-bearing modeless UI registry beside `MODELESSUI`.

A canvas view reads a transient scene projection from `MODELESSUI` in its normal
TVision `draw()` override. It must not retain a C++ scene cache. `COMMIT` may
request a redraw of that one view, but must not redraw a desktop, change focus
or modify Z-order.

MMP canvas styles are bounded palette-relative roles. MRMAC must not pass raw
TVision character attributes into retained canvas commands.

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
