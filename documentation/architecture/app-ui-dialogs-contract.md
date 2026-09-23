# App / UI / Dialogs Contract

## Scope

Applies to:

- `app/MREditorApp.cpp`
- `app/MRCommandRouter.cpp`
- `app/MRMenuFactory.cpp`
- `dialogs/setup/`
- `dialogs/MRWindowList.cpp`
- `dialogs/MRKeymapManager.cpp`
- `dialogs/MRMacroFile.cpp`
- `dialogs/MRCompilerProfiles.cpp`

## Authority

The UI is not authoritative for settings.

Dialogs may hold:

- input buffers,
- temporary selection state,
- TVision view state,
- validation state.

Authoritative settings state belongs to `SETTINGS/runtime` in the central VM
K/V.

Semantic application/UI runtime state belongs to the App/UI layer under the
central K/V root `APPLICATIONUI`. Live TVision objects may retain their own
view-local mechanics, geometry and pointers; they must not become a parallel
store for K/V-representable application state.

The established branches include:

| Branch | Ownership |
|---|---|
| `APPLICATIONUI/messageLine` | Message slots, sequence counters, enable state, Static Mode semaphore and Static Mode progress |
| `APPLICATIONUI/workspace` | Workspace identity, restore and autosave coordination |
| `APPLICATIONUI/virtualDesktops` | Current desktop and configuration projection |
| `APPLICATIONUI/search` | Search dialog, result and multi-file search state |
| `APPLICATIONUI/log` | Runtime log buffer and persistence cursor |
| `APPLICATIONUI/indicators` | Recording and Macro Brain marker state |
| `APPLICATIONUI/performance` | Runtime performance events |
| `APPLICATIONUI/debugger` | GDB session state and debugger pane projections |

## GDB debugger state ownership

GDB debugger administration is central K/V state. `APPLICATIONUI/debugger` owns:

- `sessions/<bufferId>`: GDB backend identity, generation, source, program and
  the context adopted by the UI;
- `sessions/<bufferId>/worker`: the backend's confirmed execution context,
  MI request tokens, pending requests, expansion queue, refresh counters,
  the active value-refresh thread and its remaining pane target;
- `sessions/<bufferId>/threads/<threadId>`: thread metadata, `watches` with
  expressions and GDB object identities, and `localVariableRoots`;
- `sessions/<bufferId>/breakpoints/<gdbNumber>`: confirmed GDB breakpoint
  source, line and condition for the current session;
- `sessions/<bufferId>/threads/<threadId>/frames/0/localVariables` and
  `watchVariables`: variable trees for the selected stopped frame;
- `breakpoints/bySource/<source>/lines` and `asserts`: source breakpoint
  definitions and optional assertion text, keyed by source line and retained
  across debugger shutdown and rebuild;
- `sessions/<bufferId>/variablesThreadId` and `watchesThreadId`: independently
  confirmed pane contexts. Worker selections remain separate from UI adoption;
- `views/<bufferId>`: GDB variable/watch row projections and `valueInput`
  target (thread and GDB object). These are UI adoption state.

Backend progress and UI adoption are distinct versions in the same store.
Only generation-checked events may replace an adopted projection. Temporary
command, result and rendering objects may carry immutable values across these
boundaries; they must not be retained as class-level semantic stores.

The backend session node exists before its worker starts. A thread snapshot
updates metadata without replacing live frame subtrees. Resume/context changes
invalidate the current value tree. Worker exit releases backend requests,
watch definitions, object roots and frame trees; closing/restarting the debugger
detaches its session node. A still-running worker retains that same node under
`retiring/<generation>` until its destructor releases the tree. Detachment
transfers the owning K/V reference without copying or deleting its children;
it never waits for a worker while holding the VM mutex. Destroying a Bento
removes its view node. Runtime
thread identities and these subtrees are never serialized.

C++ retains process/pipe/PTY handles, parser buffers, bounded transport
messages, live views and their geometry. GDB watch and breakpoint definitions,
retained results and refresh administration belong to the central K/V store.
All central K/V access uses the VM execution mutex. GDB pipe I/O, worker joins
and waits must not hold that mutex.

Selecting a thread in Variables or Watches pins that pane independently of
Source for the current stop. Every new execution stop resets both panes to
the stopped Source thread and frame zero. Until explicitly selected again,
a value pane follows Source. F7/Shift-F7
operate on the Watches context; value editing uses Variables, while stepping,
run-to-cursor use Source; breakpoint toggling uses the source location without
binding it to the selected thread. Selection changes invalidate
outstanding value replies using stop and context generations. Each value event
also carries its thread, and adoption checks the destination pane's context.

GDB character arrays display their individual byte values and an additional
ASCII line, with non-printable bytes shown as dots. Scalar array values are
right-aligned to a common width within each array; index ranges align across
wrapped lines. These are view projections of the existing GDB values.

Source breakpoints apply to all threads. An optional assertion is evaluated
by GDB at the breakpoint in the hitting thread's context. Assertions are
passed unchanged to GDB and use its automatic inferior-language selection.
MR reserves no assertion identifiers and supplies no thread-name predicate.
GDB convenience variables such as `$_thread` are available through ordinary
GDB expression syntax. False conditions continue through GDB's native
breakpoint machinery, without a stop/resume cycle in MR.

Right-clicking a source breakpoint line opens the existing text input dialog
with its assertion. Empty input removes the condition; cancellation preserves
it. Only successful GDB mutations update definitions in the central K/V.
Breakpoint snapshots are runtime projections and never replace those
definitions. Rapid toggles and assertion changes share the K/V mutation queue.
Execution waits for pending breakpoint mutations and restoration replies;
failed mutations cancel that deferred execution. Rebuild restores definitions
and assertions before execution. No runtime
thread identity is rebound. Thread exit releases its runtime subtree and an
orphaned pane selection falls back to Source at the next thread snapshot.

## Data flow

User event -> TVision event -> App / CommandRouter -> Dialog or Command -> domain-specific path.

Dialogs may trigger:

- settings updates,
- history updates,
- keymap loading,
- workspace operations,
- message-line output.

## Invariants

- Dialogs must remain TVision-native.
- Dialogs must not create shadow settings stores.
- Dialog and application code must not add semantic file globals or
  function-static histories beside `APPLICATIONUI` or `SETTINGS/history`.
- Dialogs must not persist settings through ad hoc paths.
- Dialog validators must not display blocking error dialogs.
- Message-line or marquee feedback is preferred for validation warnings.
- Dirty state must be set only for real changes.

## Dialog layout design rules

These rules apply to new dialogs and to layout changes in existing dialogs.

- Radio-button clusters must have a left-aligned heading ending with a colon.
- Checkbox options that belong together must be grouped into one checkbox cluster with a left-aligned heading ending with a colon.
- Dialog content must keep equal visual distance to the left and right dialog borders.
- Text input rows belong to visual field groups. Within one group, labels,
  input starts, input ends and optional comfort controls must align
  consistently.
- Text inputs with comfort controls such as history drop lists or browse
  glyphs may extend near the right dialog border, but must leave at least one
  column of frame spacing. Two columns are acceptable only when the matching
  left spacing and neighboring rows use the same group rhythm.
- Text inputs without comfort controls use the same plain input width as their
  group peers. They do not extend into the reserved comfort-control columns.
- Comfort-control columns are reserved outside the input field. The highlighted
  input area must not run underneath history or browse glyphs.
- Dialog titles must be uppercase, right-aligned and enclosed in square brackets
  through `MRFrame`; title strings themselves must not contain brackets, padding
  or ellipses.
- Dialog frames, titles, empty interior cells and buttons must use the configured
  `Dialog Frame`, `Dialog Text`, `Dialog Background` and `Dialog Button` palette
  roles respectively.
- Dialog button rows must be horizontally centered.
- Dialogs must not add a Cancel button by default. Closing without applying changes is handled by the dialog close action.
- Dialogs that can change settings must use clean dirty gating. Applying unchanged data must not mark settings dirty or trigger save prompts.
- Dialogs whose fixed layout may not fit the current terminal must use
  `MRScrollableDialog` / `MRDialogFoundation` or provide a local proof that all
  controls remain reachable.
- Options inside radio-button and checkbox clusters must leave at least one trailing space inside the highlighted cluster area after the longest visible option text.
- Neighboring clusters must keep two columns of horizontal spacing.
- Stacked widgets must keep one empty row of vertical spacing unless they form one logical multi-line control.
- Button rows must keep one empty row of spacing to the dialog frame.
- Maintainer-approved Window List exception: the workspace autosave retention
  slider has no separate label and follows the workspace button row without
  an extra blank row. The bottom Help/Done row has no extra blank row before
  the frame. The Auto/Load workspace pair is shifted one column
  left within the otherwise centered workspace row.

## Boundaries

Without explicit maintainer approval:

- New dialog architecture.
- New generic setup framework.
- New local settings registry.
- Overlay hacks.
- Direct screen-buffer manipulation.
- Opportunistic changes to history behavior.
- Changes to keymap, workspace or settings persistence from dialog code.

## Related contracts

- [TVision Integration](tvision-integration-contract.md)
- [Settings Runtime](settings-runtime-contract.md)
- [Keymap](keymap-contract.md)

## Required manual tests

For dialog changes, test:

- open dialog,
- initial validation,
- invalid input,
- valid input,
- save/apply path,
- cancel path,
- terminal-size constraints when relevant, including reachability of all interactive controls for fixed-size setup dialogs below nominal width or height.
