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

Authoritative settings state belongs to the settings runtime model.

Window/editor state belongs to the App/UI layer.

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
- Dialog titles must be uppercase and must not use ellipses.
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
