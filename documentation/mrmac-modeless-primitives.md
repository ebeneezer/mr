# MRMAC Modeless Primitives

MMP builds retained modeless UI from typed MRMAC procedures. A macro never
receives a TVision view, window pointer or raw event.

## Canvas v1

Define the window with the established `UI_DIALOG` family and add a canvas
before `UI_MODELESS_SHOW`:

```mrmac
UI_DIALOG(0, 0, 60, 18, 'MMP CANVAS');
MMP_CANVAS(2, 3, 56, 10, 'Main');
UI_MODELESS_SHOW('MmpCanvasDemo');
```

Update its retained scene with explicit identifiers, then commit once:

```mrmac
MMP_CANVAS_CLEAR('MmpCanvasDemo', 'Main', MMP_STYLE_SURFACE());
MMP_CANVAS_BOX('MmpCanvasDemo', 'Main', 0, 0, 56, 10, MMP_STYLE_ACCENT());
MMP_CANVAS_TEXT('MmpCanvasDemo', 'Main', 3, 2, MMP_STYLE_TEXT(), 'Retained scene');
MMP_CANVAS_LINE('MmpCanvasDemo', 'Main', 3, 6, 51, 6, MMP_STYLE_ACCENT(), '-');
MMP_CANVAS_COMMIT('MmpCanvasDemo', 'Main');
```

`MMP_CANVAS_GLYPH` writes one glyph and `MMP_CANVAS_FILL` paints a rectangular
background. Canvas coordinates are local, zero-based character cells. Styles
are palette-relative: `MMP_STYLE_SURFACE`, `MMP_STYLE_TEXT`,
`MMP_STYLE_ACCENT` and `MMP_STYLE_MUTED`. Raw TVision character attributes are
not accepted.

## Canvas hotspots

`MMP_CANVAS_HOTSPOT(canvasId, x, y, width, height, controlId, macroSpec)` adds
an invisible left-click region to a canvas declared earlier in the same window
definition. Geometry is local to, and must lie entirely inside, that canvas.
The hotspot does not draw a button; compose its visible surface from the
retained canvas commands.

```mrmac
MMP_CANVAS_TEXT(WindowId, 'Main', 2, 4, MMP_STYLE_TEXT(), 'Refresh');
MMP_CANVAS_HOTSPOT('Main', 2, 4, 8, 1, 41, 'StatusPanel^Refresh');
```

The first matching hotspot consumes the click and starts `macroSpec` through
the existing modeless-window-owned execution-session route. The MMP itself is
activated first. A hotspot has no TVision control, pointer or event handle;
its definition is retained under `MODELESSUI` with the window model.

## Action buttons

For an ordinary modeless button, use the composed declaration instead of
separately pairing `UI_BUTTON` and `UI_MODELESS_ON`:

```mrmac
MMP_ACTION_BUTTON(2, 12, 12, 41, '~R~efresh', 'StatusPanel^Refresh');
```

It creates the existing native TVision button and binds its activation to the
normal window-owned execution-session callback route. The button keeps the
native focus, keyboard and hotkey behavior; no canvas, hotspot or event routing
code is needed. `controlId` must be positive; caption and macro spec must be
non-empty.

## Action menus

An action menu packages the existing named item list, native grid and modeless
callback binding. Each item has a visible label, a stable value delivered to
the callback, and an optional detail line:

```mrmac
MMP_MENU_CLEAR('Actions');
MMP_MENU_ITEM('Actions', 'Refresh', 'REFRESH', 'Read current data');
MMP_MENU_ITEM('Actions', 'Close', 'CLOSE', 'Close this panel');
MMP_ACTION_MENU(2, 3, 34, 7, 61, 'Actions', 'Actions', 'StatusPanel^RunAction');
```

The native menu keeps keyboard focus, arrow navigation, Enter and double-click
activation. The callback runs through the existing window-owned execution
session and reads the selected stable value with `UI_TEXT(61)`; `UI_INDEX(61)`
is its one-based selection index. Menu ids, labels and values must be non-empty
and may not contain tab characters. `detail` may be empty. The action menu is
declared before `UI_MODELESS_SHOW`; it exposes no TVision object, event or
pointer.

## Text fields

Use named text fields for modeless forms. Callbacks use the stable field id;
they never depend on a numeric UI control id:

```mrmac
MMP_TEXT_FIELD(2, 3, 28, 'Host', 'Host', 'localhost');
MMP_ACTION_BUTTON(2, 6, 12, 41, '~C~onnect', 'ConnectionPanel^Connect');
UI_MODELESS_SHOW(WindowId);

Host := MMP_TEXT_VALUE(WindowId, 'Host');
MMP_TEXT_SET(WindowId, 'Host', 'example.org');
```

Declare fields before `UI_MODELESS_SHOW`. `fieldId` and `label` must be
non-empty; ids are unique within the window definition. The native text input
keeps ordinary TVision focus, editing and selection behavior. Its current text
is mirrored immediately into the retained `MODELESSUI` model, so callbacks and
timers read it safely with `MMP_TEXT_VALUE`. `MMP_TEXT_SET` updates the retained
value and the existing input view. Unknown window or field targets are reported
as MMP errors. No input pointer, control id or TVision view crosses into MRMAC.

### Boolean fields

```mrmac
MMP_BOOL_FIELD(2, 8, 'UseTls', '~T~LS', 1);
UI_MODELESS_SHOW(WindowId);

UseTls := MMP_BOOL_VALUE(WindowId, 'UseTls');
MMP_BOOL_SET(WindowId, 'UseTls', 0);
```

`MMP_BOOL_FIELD` creates a named native checkbox. Its value is always `0` or
`1` and is mirrored immediately into the retained `MODELESSUI` model. Callbacks
and timers use `MMP_BOOL_VALUE`; `MMP_BOOL_SET` changes the authoritative value
and its existing checkbox projection. No checkbox handle or control id is
available to MRMAC.

### Integer fields

```mrmac
MMP_INT_FIELD(2, 10, 6, 'Retries', 'Retries', 3, 0, 9);
UI_MODELESS_SHOW(WindowId);

Retries := MMP_INT_VALUE(WindowId, 'Retries');
MMP_INT_SET(WindowId, 'Retries', 5);
```

`MMP_INT_FIELD` creates a named native integer input with an inclusive range.
Only values inside that range enter the retained `MODELESSUI` model.
`MMP_INT_SET` rejects an out-of-range value. While a user edits, a transient
non-integer or out-of-range text stays local to the input; on focus loss it is
replaced with the last retained valid value. Callbacks and timers therefore
always read a valid integer through `MMP_INT_VALUE`. No input pointer, control
id or TVision view reaches MRMAC.

### Progress fields

```mrmac
MMP_PROGRESS_FIELD(2, 12, 18, 'Scan', 'Scan', 100, 0);
UI_MODELESS_SHOW(WindowId);

MMP_PROGRESS_SET(WindowId, 'Scan', 42);
Done := MMP_PROGRESS_VALUE(WindowId, 'Scan');
```

`MMP_PROGRESS_FIELD` creates a labeled, non-focusable retained progress bar.
`total` is positive and the initial value as well as `MMP_PROGRESS_SET` values
must lie between `0` and `total`, inclusive. The update changes exactly the
addressed bar; it neither selects nor raises its MMP window. Timers and action
callbacks use `MMP_PROGRESS_SET` directly, without canvas geometry, redraw
logic or a TVision view.

### Event logs

```mrmac
MMP_LOG_FIELD(2, 15, 34, 4, 'Events', 'Events', 16);
UI_MODELESS_SHOW(WindowId);

MMP_LOG_APPEND(WindowId, 'Events', 'Connection established');
MMP_LOG_CLEAR(WindowId, 'Events');
Count := MMP_LOG_COUNT(WindowId, 'Events');
```

`MMP_LOG_FIELD` creates a labeled, non-focusable retained event log. `height`
is the visible row count; `capacity` bounds retained lines and must be at least
that height and no greater than 256. `MMP_LOG_APPEND` accepts one line of at
most 512 bytes; once full, the oldest entry is discarded. The projection shows
the newest visible rows and redraws only the addressed log. Timers and action
callbacks therefore publish asynchronous diagnostics without canvas geometry,
view handles or unbounded runtime storage.

### Selection fields

```mrmac
MMP_SELECT_FIELD(2, 10, 24, 3, 'Mode', 'Mode', 'Normal');
MMP_SELECT_OPTION('Mode', 'Normal');
MMP_SELECT_OPTION('Mode', 'Safe');
MMP_SELECT_OPTION('Mode', 'Fast');
UI_MODELESS_SHOW(WindowId);

Mode := MMP_SELECT_VALUE(WindowId, 'Mode');
MMP_SELECT_SET(WindowId, 'Mode', 'Safe');
```

`MMP_SELECT_FIELD` creates a labeled native selection list. Its options are
declared with `MMP_SELECT_OPTION` before `UI_MODELESS_SHOW`; each is unique
within the field. The selected string is retained in `MODELESSUI`, updated by
mouse and keyboard selection, and exposed through `MMP_SELECT_VALUE`.
`MMP_SELECT_SET` accepts only a declared option and updates the existing list.
An initial value not declared as an option falls back to the first option; an
empty option list has no selected value. No list id, control id, pointer or
TVision view reaches MRMAC.

## Status fields

Status fields have stable logical ids, so callbacks do not need to depend on
the declaration order of display rows:

```mrmac
MMP_STATUS_FIELD(2, 10, 34, 'Activity', 'Ready');
MMP_STATUS_SET(WindowId, 'Activity', 'Refreshing');
```

Declare a status field before `UI_MODELESS_SHOW`. Its id is scoped to the
window definition and must be unique there. `MMP_STATUS_SET` updates only that
field of an existing MMP window; an unknown window or id is reported as an MMP
error. Timer and action-button callbacks can use the same operation without
knowing or preserving a display index.

`MMP_WINDOW_INSTANCE(prefix)` returns a unique logical model id. It is valid
only as an MMP window id and never exposes a TVision object or pointer. Build a
definition with `UI_DIALOG`, then pass that id to `UI_MODELESS_SHOW` to create
another independent instance.

## Timers

Timers are owned by an existing MMP window and run their callback through the
normal macro execution-session route:

```mrmac
MMP_TIMER_START(WindowId, 'Refresh', 1000, 'StatusPanel^Refresh');
MMP_TIMER_STOP(WindowId, 'Refresh');
```

`timerId` is a logical id scoped to `WindowId`; starting it again replaces its
previous schedule. The minimum interval is 100 ms. A running callback causes
the next tick to be skipped according to the scheduler's normal overrun rule.
Stopping prevents future ticks but does not forcibly stop an already running
callback; stopping an absent timer is harmless and returns `0`. Closing the MMP
removes all of its timers and requests cooperative cancellation of callbacks
owned by that window. The macro never receives a scheduler, TVision or pointer
handle.

Window geometry is queried through `MMP_WINDOW_EXISTS`, `MMP_WINDOW_X`,
`MMP_WINDOW_Y`, `MMP_WINDOW_WIDTH` and `MMP_WINDOW_HEIGHT`.

The scene is runtime-only under `MODELESSUI`. `MMP_CANVAS_COMMIT` redraws only
the addressed canvas; it neither selects the window nor changes desktop Z-order.
One canvas scene is bounded to 2,048 commands; `MMP_CANVAS_CLEAR` replaces the
previous scene before subsequent commands are appended.
